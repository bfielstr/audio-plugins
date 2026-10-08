#include "State.h"

#include "smacheratr/src/core/TailExt.h"

#include "base/source/fstreamer.h"

#include <algorithm>
#include <memory>

namespace moistr {

using namespace Steinberg;

namespace {
constexpr int32 kMagic = 0x5453494D; // 'MIST'
// 1: the first (0.18: three moving filters).
// 2: 0.19, the multiband split and the frequency shifter (IDs 69 .. 79 appended). A version-1 state keeps every value it stored
//    (the filters' parameters are kept, though the split no longer uses them) and reads the new
//    parameters at their defaults: nothing to convert.
// 3: 0.24, the SWEEP stage (IDs 92 .. 118 appended) and new defaults (Sweep on; Drive, Movement, Glue and
//    Grit at 0). An older state reads what it lacks at the defaults from before 0.24 (legacyDefaultNormalized:
//    Sweep off), so a project or preset from before keeps its sound. (Since 0.18 a saved state has every
//    parameter it knew, so in practice that is Sweep off and the stage's settings at their defaults.)
// A later version that changes what a saved value means converts older states in readState (as the other
// plug-ins do), so projects keep sounding the same.
// 4: 0.25, the end saturator and its Gentlr on by default, Gentlr's Slope Signature (an older state keeps
//    the old defaults where it lacks them: smacheratr::tailOldDefaults below).
// 5: 0.26, the SWEEP stage's eight bells, Curve, Tone, Clean Sub and Sub Boost (IDs 119 .. 184 appended), and
//    the "Ocean" recipe as the new defaults. A version 3 or 4 state reads what it lacks at 0.25's defaults with
//    the new parameters as they leave its sound (bells C .. H, Tone, Clean Sub and Sub Boost off, Curve Hard:
//    defaultNormalizedForVersion), so a 0.24 or 0.25 project sounds as it did.
// 6: 0.27, the gestures (IDs 185 .. 219 appended) and, after the parameters, each slot's user gesture: 'GEST',
//    the slot count, then per slot whether it has one and its name, length (beats) and points (beat, value).
//    An older state reads the gestures off (every Target Off, Wobble Amount 0: defaultNormalizedForVersion) and
//    no user gestures, so it sounds as it did. (An older build reads a version 6 state's parameters and stops
//    before the gestures' block.)
// 7: 0.28, the one gesture (IDs 220 .. 226 appended: Gesture, its Mode, Length, Speed, Position, Smooth and
//    Amount) and, after the slots' block, its user gesture: 'SCNE', the length in bytes and the gesture's JSON
//    (GestureFile.h: sceneJson; 0 bytes: none). An older state reads Gesture None (its default) and keeps its
//    slots, which still play, so a 0.27 project sounds as it did. (A 0.27 build reads a version 7 state's
//    parameters and slots and stops before the new block.)
// 8: 0.29, the LAB (IDs 227 .. 1930 appended: four chains' Level, Mute, Solo, Mono and four kept for later, then 19
//    effects slots of 88: Type, On and a block). An older state reads every slot Empty and every chain at 0 dB (their
//    defaults: defaultNormalizedForVersion), so a 0.28 project sounds as it did, bit for bit. (A 0.28 build reads a
//    version 8 state's first 227 parameters and skips the LAB's.)
constexpr int32 kVersion = 8;
constexpr int32 kGestureMagic = 0x54534547; // 'GEST'
constexpr int32 kSceneMagic = 0x454E4353;   // 'SCNE'
constexpr int32 kMaxSceneBytes = 4 << 20;
constexpr int32 kMaxNameBytes = 1024;
constexpr int32 kNewDefaults = 4;
} // namespace

bool writeState (IBStream* stream, const State& st)
{
    IBStreamer s (stream, kLittleEndian);
    int32 present = 0;
    for (uint32 id = 0; id < kNumParams; ++id)
        present += st.has[id] ? 1 : 0;
    bool ok = s.writeInt32 (kMagic) && s.writeInt32 (kVersion) && s.writeInt32 (present);
    for (uint32 id = 0; ok && id < kNumParams; ++id)
        if (st.has[id])
            ok = s.writeInt32u (id) && s.writeDouble (st.norm[id]);
    // the user gestures
    ok = ok && s.writeInt32 (kGestureMagic) && s.writeInt32 (kNumGestureSlots);
    for (const GestureData& g : st.user)
    {
        const bool has = !g.points.empty () && (int)g.points.size () <= kMaxGesturePoints;
        ok = ok && s.writeInt32 (has ? 1 : 0);
        if (!ok || !has)
            continue;
        const std::string name = g.name.substr (0, (size_t)kMaxNameBytes);
        ok = s.writeInt32 ((int32)name.size ()) && (name.empty () || s.writeRaw (name.data (), (int32)name.size ()) == (int32)name.size ()) &&
             s.writeDouble (g.length) && s.writeInt32 ((int32)g.points.size ());
        for (const auto& [beat, value] : g.points)
            ok = ok && s.writeDouble (beat) && s.writeDouble (value);
    }
    // the one gesture's user gesture
    const std::string scene = st.scene.empty () ? std::string () : sceneJson (st.scene);
    ok = ok && s.writeInt32 (kSceneMagic) && s.writeInt32 ((int32)scene.size ()) &&
         (scene.empty () || s.writeRaw (scene.data (), (int32)scene.size ()) == (int32)scene.size ());
    return ok;
}

namespace {
// the one gesture's user gesture (version 7; none when the block is missing or damaged)
void readScene (IBStreamer& s, State& st)
{
    int32 magic = 0, bytes = 0;
    if (!s.readInt32 (magic) || magic != kSceneMagic || !s.readInt32 (bytes) || bytes <= 0 || bytes > kMaxSceneBytes)
        return;
    std::string text ((size_t)bytes, '\0');
    if (s.readRaw (text.data (), bytes) != bytes)
        return;
    SceneData d;
    std::string error;
    auto check = std::make_unique<Scene> ();
    if (parseSceneJson (text, "User", d, error) && toScene (d, *check)) // (one the engine cannot play is left out)
        st.scene = std::move (d);
}
} // namespace

bool readState (IBStream* stream, State& st)
{
    IBStreamer s (stream, kLittleEndian);
    int32 magic = 0, version = 0, count = 0;
    if (!s.readInt32 (magic) || magic != kMagic || !s.readInt32 (version) || version < 1 || !s.readInt32 (count) ||
        count < 0 || count > 100000)
        return false;
    for (uint32 id = 0; id < kNumParams; ++id)
    {
        st.norm[id] = defaultNormalizedForVersion (id, version);
        st.has[id] = false;
    }
    for (int32 i = 0; i < count; ++i)
    {
        uint32 id = 0;
        double v = 0.0;
        if (!s.readInt32u (id) || !s.readDouble (v))
            return false;
        if (id < kNumParams) // (a newer build's parameters are skipped)
        {
            st.norm[id] = std::clamp (v, 0.0, 1.0);
            st.has[id] = true;
        }
    }
    // the defaults were the end saturator off, its Gentlr off and Gentlr's Slope 12 / 12: a state saved
    // then keeps them where it lacks them
    if (version < kNewDefaults)
        smacheratr::tailOldDefaults (st.norm, st.has, kTailBase, kTailExtBase, kTailExt3Base);
    // the user gestures (version 6; a state without the block has none). A damaged block is left out whole.
    for (GestureData& g : st.user)
        g = {};
    st.scene = {};
    int32 magic2 = 0, slots = 0;
    if (version < 6 || !s.readInt32 (magic2) || magic2 != kGestureMagic || !s.readInt32 (slots) || slots < 0 || slots > 64)
        return true;
    std::array<GestureData, kNumGestureSlots> user {};
    for (int32 k = 0; k < slots; ++k)
    {
        int32 has = 0;
        if (!s.readInt32 (has))
            return true;
        if (!has)
            continue;
        int32 nameBytes = 0, n = 0;
        if (!s.readInt32 (nameBytes) || nameBytes < 0 || nameBytes > kMaxNameBytes)
            return true;
        std::string name ((size_t)nameBytes, '\0');
        if (nameBytes > 0 && s.readRaw (name.data (), nameBytes) != nameBytes)
            return true;
        double length = 0.0;
        if (!s.readDouble (length) || !s.readInt32 (n) || n < 1 || n > kMaxGesturePoints || !(length > 0.0))
            return true;
        GestureData g;
        g.name = name;
        g.length = length;
        for (int32 i = 0; i < n; ++i)
        {
            double b = 0.0, v = 0.0;
            if (!s.readDouble (b) || !s.readDouble (v))
                return true;
            g.points.emplace_back (b, v);
        }
        Gesture check;
        if (k < kNumGestureSlots && toGesture (g, check)) // (one the engine cannot play is left out)
            user[(size_t)k] = std::move (g);
    }
    st.user = std::move (user);
    if (version >= 7)
        readScene (s, st);
    return true;
}

} // namespace moistr
