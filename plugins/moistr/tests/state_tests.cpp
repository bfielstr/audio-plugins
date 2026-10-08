// Moistr's saved state (plugin/State.cpp) on its own: a round trip of every parameter, a partial state, a
// state from a newer build (parameters this one does not know), a 0.18 state (version 1), 0.23 to 0.25 states
// (versions 2 to 4: the 0.26 parameters as they keep the old sound), a 0.26 state (version 5: the gestures off),
// the user gestures (version 6: there and back, missing, damaged), a 0.27 state (version 6: its slots kept, the
// one gesture None), the one gesture's user gesture (version 7: there and back, cut, damaged), a stream that is not
// Moistr's, and the parameter table's fixed points. Run: ./moistr_state_tests
#include "Params.h"
#include "plugin/State.h"

#include "base/source/fstreamer.h"
#include "public.sdk/source/common/memorystream.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace Steinberg;
using namespace moistr;

static int gFailures = 0, gChecks = 0;
#define CHECK(cond, ...)                                                   \
    do                                                                     \
    {                                                                      \
        ++gChecks;                                                         \
        if (!(cond))                                                       \
        {                                                                  \
            ++gFailures;                                                   \
            std::printf ("    FAIL %s:%d: %s  ", __FILE__, __LINE__, #cond); \
            std::printf (__VA_ARGS__);                                     \
            std::printf ("\n");                                            \
        }                                                                  \
    } while (0)

int main ()
{
    // every parameter, a value of its own, there and back
    {
        State st;
        for (uint32_t id = 0; id < kNumParams; ++id)
        {
            st.norm[id] = std::fmod (0.137 * (id + 1), 1.0);
            st.has[id] = true;
        }
        MemoryStream s;
        CHECK (writeState (&s, st), "write");
        s.seek (0, IBStream::kIBSeekSet, nullptr);
        State back;
        CHECK (readState (&s, back), "read");
        int wrong = 0;
        for (uint32_t id = 0; id < kNumParams; ++id)
            wrong += back.has[id] && back.norm[id] == st.norm[id] ? 0 : 1;
        CHECK (wrong == 0, "every parameter back as it was (%d not)", wrong);
    }
    // a partial state: the rest at their defaults
    {
        State st;
        st.norm[kSeed] = toNormalized (kSeed, 42.0);
        st.has[kSeed] = true;
        MemoryStream s;
        CHECK (writeState (&s, st), "write partial");
        s.seek (0, IBStream::kIBSeekSet, nullptr);
        State back;
        CHECK (readState (&s, back), "read partial");
        CHECK (std::lround (toPlain (kSeed, back.norm[kSeed])) == 42 && back.norm[kGlue] == defaultNormalized (kGlue) &&
                   !back.has[kGlue],
               "partial: Seed 42, the rest at the defaults");
    }
    // a newer build's state: its extra parameters are skipped, the known ones read
    {
        MemoryStream s;
        {
            IBStreamer w (&s, kLittleEndian);
            w.writeInt32 (0x5453494D);
            w.writeInt32 (7);
            w.writeInt32 (2);
            w.writeInt32u (kNumParams + 5);
            w.writeDouble (0.5);
            w.writeInt32u (kMix);
            w.writeDouble (0.9);
        }
        s.seek (0, IBStream::kIBSeekSet, nullptr);
        State back;
        CHECK (readState (&s, back), "a newer version reads");
        CHECK (back.norm[kMix] == 0.9 && back.has[kMix], "its known values kept");
    }
    // a 0.18 state (version 1, IDs 0 .. 68): every stored value kept, the split's and the shifter's
    // parameters (69 ..) at their defaults from before 0.24 (legacyDefaultNormalized: Sweep off)
    {
        MemoryStream s;
        const uint32_t old = kBandCount; // (0.18's parameters: 0 .. 68)
        {
            IBStreamer w (&s, kLittleEndian);
            w.writeInt32 (0x5453494D);
            w.writeInt32 (1);
            w.writeInt32 ((int32)old);
            for (uint32_t id = 0; id < old; ++id)
            {
                w.writeInt32u (id);
                w.writeDouble (std::fmod (0.311 * (id + 1), 1.0));
            }
        }
        s.seek (0, IBStream::kIBSeekSet, nullptr);
        State back;
        CHECK (readState (&s, back), "a 0.18 state reads");
        int kept = 0, defaults = 0;
        for (uint32_t id = 0; id < kNumParams; ++id)
            if (id < old)
                kept += back.has[id] && back.norm[id] == std::fmod (0.311 * (id + 1), 1.0);
            else
                defaults += !back.has[id] && back.norm[id] == legacyDefaultNormalized (id);
        CHECK (back.norm[kSweep] == 0.0, "a 0.18 state: Sweep off");
        CHECK (kept == (int)old, "every stored value kept (%d of %u)", kept, old);
        CHECK (defaults == (int)(kNumParams - old), "the new parameters at their defaults (%d of %u)", defaults, kNumParams - old);
        // and saved again it is the current version, with everything
        for (uint32_t id = 0; id < kNumParams; ++id)
            back.has[id] = true;
        MemoryStream t;
        CHECK (writeState (&t, back), "write");
        t.seek (0, IBStream::kIBSeekSet, nullptr);
        IBStreamer r (&t, kLittleEndian);
        int32 magic = 0, version = 0, count = 0;
        r.readInt32 (magic);
        r.readInt32 (version);
        r.readInt32 (count);
        CHECK (version == 7 && count == (int32)kNumParams, "saved as version 7 (%d) with %d values", version, count);
    }
    // a 0.23 state (version 2, IDs 0 .. 91, every one): every value kept, the SWEEP stage off and its settings at
    // their defaults; a state from 0.24 (version 3) without them reads the new defaults (Sweep on)
    for (int32 version : {2, 3})
    {
        MemoryStream s;
        const uint32_t old = kSweep; // (0.23's parameters: 0 .. 91)
        {
            IBStreamer w (&s, kLittleEndian);
            w.writeInt32 (0x5453494D);
            w.writeInt32 (version);
            w.writeInt32 ((int32)old);
            for (uint32_t id = 0; id < old; ++id)
            {
                w.writeInt32u (id);
                w.writeDouble (std::fmod (0.173 * (id + 1), 1.0));
            }
        }
        s.seek (0, IBStream::kIBSeekSet, nullptr);
        State back;
        CHECK (readState (&s, back), "a version %d state reads", version);
        int kept = 0, rest = 0;
        for (uint32_t id = 0; id < kNumParams; ++id)
            if (id < old)
                kept += back.has[id] && back.norm[id] == std::fmod (0.173 * (id + 1), 1.0);
            else
                rest += !back.has[id] && back.norm[id] == defaultNormalizedForVersion (id, version);
        CHECK (kept == (int)old && rest == (int)(kNumParams - old), "version %d: %d values kept, %d at their defaults", version, kept,
               rest);
        CHECK ((back.norm[kSweep] >= 0.5) == (version >= 3), "version %d: Sweep %s", version, version >= 3 ? "on" : "off");
    }
    // a 0.24 / 0.25 state (version 3 / 4, IDs 0 .. 118, every one): every value kept, the 0.26 parameters at the
    // values that keep its sound (bells C .. H, Tone, Clean Sub and Sub Boost off, Curve Hard); a 0.26 state
    // (version 5) without them reads the new defaults (the Ocean recipe)
    for (int32 version : {3, 4, 5})
    {
        MemoryStream s;
        const uint32_t old = kSweepCurve; // (0.25's parameters: 0 .. 118)
        {
            IBStreamer w (&s, kLittleEndian);
            w.writeInt32 (0x5453494D);
            w.writeInt32 (version);
            w.writeInt32 ((int32)old);
            for (uint32_t id = 0; id < old; ++id)
            {
                w.writeInt32u (id);
                w.writeDouble (std::fmod (0.191 * (id + 1), 1.0));
            }
        }
        s.seek (0, IBStream::kIBSeekSet, nullptr);
        State back;
        CHECK (readState (&s, back), "a version %d state reads", version);
        int kept = 0;
        for (uint32_t id = 0; id < old; ++id)
            kept += back.has[id] && back.norm[id] == std::fmod (0.191 * (id + 1), 1.0);
        CHECK (kept == (int)old, "version %d: every stored value kept (%d of %u)", version, kept, old);
        const bool before = version < 5;
        int bellsOn = 0;
        for (int b = 2; b < kNumBells; ++b)
            bellsOn += back.norm[bellOnId (b)] >= 0.5;
        CHECK (bellsOn == (before ? 0 : 6), "version %d: bells C .. H %s (%d on)", version, before ? "off" : "on", bellsOn);
        CHECK ((std::lround (toPlain (kSweepCurve, back.norm[kSweepCurve])) == kCurveHard) == before &&
                   (back.norm[kToneOn] < 0.5) == before && back.norm[kCleanSub] < 0.5 && (back.norm[kSubBoost] < 0.5) == before &&
                   back.norm[kAOn] >= 0.5 && back.norm[kBOn] >= 0.5,
               "version %d: Curve %s, Tone and Sub Boost %s, Clean Sub off, A and B on", version, before ? "Hard" : "Soft",
               before ? "off" : "on");
    }
    // a 0.26 state (version 5, IDs 0 .. 184, every one): every value kept, the gestures off (every Target Off,
    // Wobble Amount 0) and no user gestures
    {
        MemoryStream s;
        const uint32_t old = kG1Gesture; // (0.26's parameters: 0 .. 184)
        {
            IBStreamer w (&s, kLittleEndian);
            w.writeInt32 (0x5453494D);
            w.writeInt32 (5);
            w.writeInt32 ((int32)old);
            for (uint32_t id = 0; id < old; ++id)
            {
                w.writeInt32u (id);
                w.writeDouble (std::fmod (0.227 * (id + 1), 1.0));
            }
        }
        s.seek (0, IBStream::kIBSeekSet, nullptr);
        State back;
        CHECK (readState (&s, back), "a 0.26 state reads");
        int kept = 0, off = 0;
        for (uint32_t id = 0; id < old; ++id)
            kept += back.has[id] && back.norm[id] == std::fmod (0.227 * (id + 1), 1.0);
        for (int g = 0; g < kNumGestureSlots; ++g)
        {
            const uint32_t id = gestureId (g, kGestureTarget);
            off += !back.has[id] && std::lround (toPlain (id, back.norm[id])) == kTargetOff;
        }
        CHECK (kept == (int)old, "0.26: every stored value kept (%d of %u)", kept, old);
        CHECK (off == kNumGestureSlots && back.norm[kWobbleAmount] == 0.0, "0.26: every gesture slot Off (%d), Wobble Amount 0", off);
        bool none = true;
        for (const auto& u : back.user)
            none = none && u.empty ();
        CHECK (none, "0.26: no user gestures");
    }
    // the user gestures (version 6): there and back; a state without the block, or with it cut short, has none
    // (its parameters read all the same); one with too many points is left out
    {
        State st;
        for (uint32_t id = 0; id < kNumParams; ++id)
        {
            st.norm[id] = defaultNormalized (id);
            st.has[id] = true;
        }
        st.norm[gestureId (1, kGestureTarget)] = toNormalized (gestureId (1, kGestureTarget), kTargetHighLevel);
        st.user[0].name = "Gate";
        st.user[0].length = 2.0;
        st.user[0].points = {{0.0, 1.0}, {0.5, 1.0}, {0.5, 0.0}, {2.0, 0.0}};
        st.user[2].name = "";
        st.user[2].length = 0.75;
        st.user[2].points = {{0.0, 0.25}, {0.75, 0.5}};
        MemoryStream s;
        CHECK (writeState (&s, st), "write with user gestures");
        int64 full = 0;
        s.tell (&full);
        s.seek (0, IBStream::kIBSeekSet, nullptr);
        State back;
        CHECK (readState (&s, back), "read with user gestures");
        CHECK (back.user[0].name == "Gate" && back.user[0].length == 2.0 && back.user[0].points == st.user[0].points, "slot 1's user gesture back");
        CHECK (back.user[1].empty () && back.user[3].empty (), "slots 2 and 4: none");
        CHECK (back.user[2].name.empty () && back.user[2].length == 0.75 && back.user[2].points == st.user[2].points, "slot 3's back");
        CHECK (std::lround (toPlain (gestureId (1, kGestureTarget), back.norm[gestureId (1, kGestureTarget)])) == kTargetHighLevel,
               "the gesture parameters back");
        // cut short in the gestures' block (or just after the parameters): the parameters read, no user gestures
        std::vector<char> bytes ((size_t)full);
        s.seek (0, IBStream::kIBSeekSet, nullptr);
        int32 got = 0;
        s.read (bytes.data (), (int32)full, &got);
        const int64 slotsEnd = full - 8; // (then the one gesture's block: 'SCNE' and 0 bytes)
        for (int64 cut : {slotsEnd - 8, slotsEnd - 40, (int64)(12 + 12 * kNumParams + 6), (int64)(12 + 12 * kNumParams)})
        {
            MemoryStream c;
            c.write (bytes.data (), (int32)cut, nullptr);
            c.seek (0, IBStream::kIBSeekSet, nullptr);
            State part;
            CHECK (readState (&c, part), "cut at %lld of %lld: reads", (long long)cut, (long long)full);
            bool none = true;
            for (const auto& u : part.user)
                none = none && u.empty ();
            CHECK (none && part.has[kWobbleAmount], "cut at %lld: the parameters, no user gestures", (long long)cut);
        }
        // more points than a gesture holds: left out
        State big = st;
        big.user[0].points.clear ();
        for (int i = 0; i <= kMaxGesturePoints; ++i)
            big.user[0].points.emplace_back (i * 0.001, 0.5);
        MemoryStream b;
        CHECK (writeState (&b, big), "write a gesture too long");
        b.seek (0, IBStream::kIBSeekSet, nullptr);
        State bb;
        CHECK (readState (&b, bb) && bb.user[0].empty () && bb.user[2].points == st.user[2].points, "too many points: left out, the rest read");
    }
    // a 0.27 state (version 6, IDs 0 .. 219, the slots' block): every value kept (the slots play as they did),
    // the one gesture None (its default, not stored) and no user gesture of its own
    {
        MemoryStream s;
        const uint32_t old = kScene; // (0.27's parameters: 0 .. 219)
        {
            IBStreamer w (&s, kLittleEndian);
            w.writeInt32 (0x5453494D);
            w.writeInt32 (6);
            w.writeInt32 ((int32)old);
            for (uint32_t id = 0; id < old; ++id)
            {
                w.writeInt32u (id);
                w.writeDouble (id == gestureId (0, kGestureTarget) ? toNormalized (id, kTargetHighLevel) : std::fmod (0.311 * (id + 1), 1.0));
            }
            // the slots' block: slot 1 with a user gesture
            w.writeInt32 (0x54534547);
            w.writeInt32 (kNumGestureSlots);
            w.writeInt32 (1);
            w.writeInt32 (4);
            w.writeRaw ("Gate", 4);
            w.writeDouble (2.0);
            w.writeInt32 (2);
            for (double v : {0.0, 1.0, 2.0, 0.0})
                w.writeDouble (v);
            for (int k = 1; k < kNumGestureSlots; ++k)
                w.writeInt32 (0);
        }
        s.seek (0, IBStream::kIBSeekSet, nullptr);
        State back;
        CHECK (readState (&s, back), "a 0.27 state reads");
        int kept = 0;
        for (uint32_t id = 0; id < old; ++id)
            kept += back.has[id];
        CHECK (kept == (int)old && std::lround (toPlain (gestureId (0, kGestureTarget), back.norm[gestureId (0, kGestureTarget)])) == kTargetHighLevel,
               "0.27: every stored value kept, slot 1 on High Level (%d of %u)", kept, old);
        CHECK (!back.has[kScene] && std::lround (toPlain (kScene, back.norm[kScene])) == kSceneNone, "0.27: Gesture None");
        CHECK (back.user[0].name == "Gate" && back.user[0].points.size () == 2 && back.scene.empty (), "0.27: slot 1's user gesture, no gesture of lanes");
    }
    // the one gesture's user gesture (version 7): there and back with a slot's; a damaged or cut block reads as none
    {
        State st;
        for (uint32_t id = 0; id < kNumParams; ++id)
        {
            st.norm[id] = defaultNormalized (id);
            st.has[id] = true;
        }
        st.norm[kScene] = toNormalized (kScene, kSceneUser);
        st.user[1].name = "Ramp";
        st.user[1].length = 1.0;
        st.user[1].points = {{0.0, 0.0}, {1.0, 1.0}};
        st.scene.name = "Two \"lanes\"";
        st.scene.length = 4.0;
        SceneLaneData a, b;
        a.target = "High Level";
        a.source = "Track > Rack > chain 2 'HIGH' > Chain Volume";
        a.hasMin = a.hasMax = true;
        a.min = -24.0;
        a.max = 0.0;
        a.points = {{0.0, 1.0}, {2.0, 1.0}, {2.0, 0.0}, {4.0, 0.5}};
        b.target = "Close";
        b.hasMin = true;
        b.min = 400.0;
        b.points = {{0.0, 1.0}, {3.0, 0.25}};
        st.scene.lanes = {a, b};
        MemoryStream s;
        CHECK (writeState (&s, st), "write with a gesture of lanes");
        int64 full = 0;
        s.tell (&full);
        s.seek (0, IBStream::kIBSeekSet, nullptr);
        State back;
        CHECK (readState (&s, back), "read with a gesture of lanes");
        CHECK (std::lround (toPlain (kScene, back.norm[kScene])) == kSceneUser, "Gesture User back");
        CHECK (back.user[1].points == st.user[1].points, "the slot's user gesture back");
        bool same = back.scene.name == st.scene.name && back.scene.length == 4.0 && back.scene.lanes.size () == 2;
        for (size_t k = 0; same && k < 2; ++k)
        {
            const SceneLaneData &x = back.scene.lanes[k], &y = st.scene.lanes[k];
            same = x.target == y.target && x.source == y.source && x.hasMin == y.hasMin && x.hasMax == y.hasMax && x.points == y.points &&
                   (!x.hasMin || x.min == y.min) && (!x.hasMax || x.max == y.max);
        }
        CHECK (same, "the gesture of lanes back as it was (name, length, targets, sources, ranges, points)");
        std::vector<char> bytes ((size_t)full);
        s.seek (0, IBStream::kIBSeekSet, nullptr);
        int32 got = 0;
        s.read (bytes.data (), (int32)full, &got);
        for (int64 cut : {full - 1, full - 30})
        {
            MemoryStream c;
            c.write (bytes.data (), (int32)cut, nullptr);
            c.seek (0, IBStream::kIBSeekSet, nullptr);
            State part;
            CHECK (readState (&c, part) && part.scene.empty () && part.user[1].points == st.user[1].points,
                   "cut in the gesture's block at %lld: none, the rest read", (long long)cut);
        }
        // damaged JSON: none
        std::vector<char> bad = bytes;
        bad[bytes.size () - 3] = '{';
        MemoryStream d;
        d.write (bad.data (), (int32)bad.size (), nullptr);
        d.seek (0, IBStream::kIBSeekSet, nullptr);
        State dd;
        CHECK (readState (&d, dd) && dd.scene.empty () && dd.has[kSceneAmount], "a damaged gesture: none, the parameters read");
    }
    // not Moistr's
    {
        MemoryStream s;
        {
            IBStreamer w (&s, kLittleEndian);
            w.writeInt32 (0x5442524F); // orbitr's
            w.writeInt32 (1);
            w.writeInt32 (0);
        }
        s.seek (0, IBStream::kIBSeekSet, nullptr);
        State back;
        CHECK (!readState (&s, back), "another plug-in's state is refused");
    }
    // the table: the defaults the README describes, every name unique
    {
        const auto& t = paramTable ();
        CHECK (t.size () == kNumParams, "%u parameters", t.size ());
        CHECK (std::lround (t.info (kSeed).def) == 1 && t.info (kLowFreq).def == 180.0 && t.info (kMidFreq).def == 450.0 &&
                   t.info (kHighFreq).def == 3000.0 && t.info (kGap).def == 0.0 && std::lround (t.info (kPasses).def) == kPasses1 &&
                   t.info (kMix).def == 1.0,
               "Seed 1, Low 180 Hz, Mid 450 Hz, High 3 kHz, no Gap, 1 pass, Mix 100 %%");
        CHECK (t.info (kMidMove).def < t.info (kHighMove).def, "High moves more than Mid by default");
        CHECK (std::lround (t.info (kBandCount).def) == kBands3 && t.info (kDepth).def == 24.0 && t.info (kRise).def == 1.0 &&
                   t.info (kFall).def == 1.0 && t.info (kShiftOn).def == 0.0 && t.info (kShift).def == 0.0 &&
                   t.info (kShiftMix).def == 1.0,
               "3 bands, Depth 24 dB, Rise and Fall x1, the shifter off (0 Hz, Mix 100 %%)");
        CHECK (t.info (kSweep).def == 1.0 && t.info (kShelf).def == 0.0 && t.info (kSweepDrive).def == 14.0 && t.info (kDrive).def == 0.0 &&
                   t.info (kMovement).def == 0.0 && t.info (kGlue).def == 0.0 && t.info (kGrit).def == 0.0,
               "0.26: Sweep on, High Shelf off, Drive 14 dB; the bands neutral (Drive, Movement, Glue, Grit 0)");
        CHECK (toPlain (kShelf, defaultNormalized025 (kShelf)) == 1.0 && toPlain (kSweepDrive, defaultNormalized025 (kSweepDrive)) == 18.0 &&
                   std::fabs (toPlain (kAWidth, defaultNormalized025 (kAWidth)) - 0.71) < 1e-9 && defaultNormalized025 (kBPhase) == 0.0,
               "0.24 and 0.25: High Shelf on, Drive 18 dB, A and B at Width 0.71 and Phase 0");
        CHECK (legacyDefaultNormalized (kSweep) == 0.0 && toPlain (kMovement, legacyDefaultNormalized (kMovement)) == 0.5 &&
                   toPlain (kGlue, legacyDefaultNormalized (kGlue)) == 0.4,
               "before 0.24: Sweep off, Movement 50 %%, Glue 40 %%");
        CHECK (t.info (kIntensity).def == 1.0 && t.info (kWobbleAmount).def == 0.0 && t.info (kWobbleRate).def == 2.0,
               "0.27: Intensity 100 %%, Wobble Amount 0, Wobble Rate 2 cycles per beat");
        for (int g = 0; g < kNumGestureSlots; ++g)
            CHECK (std::lround (t.info (gestureId (g, kGestureTarget)).def) == kTargetOff && t.info (gestureId (g, kGestureDepth)).def == 1.0 &&
                       std::lround (t.info (gestureId (g, kGestureMode)).def) == kModeLoop &&
                       std::lround (t.info (gestureId (g, kGestureLength)).def) == 0 && std::lround (t.info (gestureId (g, kGestureSpeed)).def) == 4,
                   "slot %d: Target Off, Depth 100 %%, Loop at its own length, Speed x1", g + 1);
        CHECK (std::lround (t.info (kScene).def) == kSceneNone && t.info (kSceneAmount).def == 1.0 && std::lround (t.info (kSceneMode).def) == kModeLoop &&
                   std::lround (t.info (kSceneLength).def) == 0 && std::lround (t.info (kSceneSpeed).def) == 4 && t.info (kScenePosition).def == 0.0 &&
                   t.info (kSceneSmooth).def == 0.0 && (int)t.info (kScene).choices.size () == kNumFactoryScenes + 2,
               "0.28: Gesture None (then the factory gestures and User), Amount 100 %%, Loop at its own length, Speed x1");
        for (int v = 1; v <= 6; ++v)
            CHECK (std::lround (toPlain (kScene, defaultNormalizedForVersion (kScene, v))) == kSceneNone, "version %d: Gesture None", v);
        for (uint32_t a = 0; a < kNumParams; ++a)
        {
            CHECK (t.info (a).id == a, "entry %u has its own ID", a);
            for (uint32_t b = a + 1; b < kNumParams; ++b)
                CHECK (std::string (t.info (a).name) != t.info (b).name, "names %u and %u differ (%s)", a, b, t.info (a).name);
        }
    }
    std::printf ("%d checks, %d failures\n", gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}
