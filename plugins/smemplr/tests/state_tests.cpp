// Headless tests for Smemplr's saved state (StateIO.cpp): the modulation's mappings from version 18 on,
// and older states, which have none; the rack's Sub and High bands without buttons from 19, its Gentlr Slope from 20. Run:
// ./smemplr_state_tests
#include "Modulation.h"
#include "Params.h"
#include "Rack.h"
#include "plugin/StateIO.h"
#include "smacheratr/src/core/TailExt.h"

#include "public.sdk/source/common/memorystream.h"

#include <cmath>
#include <cstdio>
#include <cstring>

using namespace smemplr;
using namespace Steinberg;

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

static PluginState someState ()
{
    PluginState st;
    for (uint32_t id = 0; id < kNumParams; ++id)
    {
        st.norm[id] = defaultNormalized (id);
        st.has[id] = true;
    }
    st.norm[kFilterFreq] = 0.4;
    st.norm[modLfoParam (1, kModRate)] = 0.7;
    st.samplePath = "/samples/loop.wav";
    st.edits.manual = {0.25, 0.5};
    return st;
}

static bool roundTrip (const PluginState& in, PluginState& out, int32 version = -1, int32 cutBytes = 0)
{
    MemoryStream s;
    if (!writeState (&s, in))
        return false;
    if (version >= 0)
        std::memcpy (s.getData () + 4, &version, sizeof (version)); // (the version, after the magic number)
    MemoryStream t (s.getData (), s.getSize () - cutBytes);
    t.seek (0, IBStream::kIBSeekSet, nullptr);
    return readState (&t, out);
}

int main ()
{
    // the mappings come back with the parameters
    {
        PluginState st = someState (), back;
        addMapping (st.mods, 0, kFilterFreq, 0.5, -1);
        addMapping (st.mods, 3, slotBlockParam (0, 2), -0.25, kFxSmacheratr);
        CHECK (roundTrip (st, back), "read");
        CHECK (back.mods.list.size () == 2 && back.mods.list[0] == st.mods.list[0] && back.mods.list[1] == st.mods.list[1],
               "mappings: %zu", back.mods.list.size ());
        CHECK (back.norm[kFilterFreq] == 0.4 && back.norm[modLfoParam (1, kModRate)] == 0.7 && back.samplePath == st.samplePath &&
                   back.edits.manual == st.edits.manual,
               "the rest of the state");
        // none saved: none read
        PluginState none = someState (), backNone;
        CHECK (roundTrip (none, backNone) && backNone.mods.list.empty (), "no mappings");
    }
    // a state from before version 18 (16, or 17 from the other branch) has no mappings: whatever follows
    // the loop fade flag is not read as mappings, and the state reads as it always did
    for (int32 version : {16, 17})
    {
        PluginState st = someState (), back;
        addMapping (st.mods, 0, kFilterFreq, 0.5, -1);
        CHECK (roundTrip (st, back, version), "read version %d", version);
        CHECK (back.mods.list.empty (), "version %d: no mappings (%zu)", version, back.mods.list.size ());
        CHECK (back.norm[kFilterFreq] == 0.4 && back.samplePath == st.samplePath && back.edits.manual == st.edits.manual,
               "version %d: the rest", version);
    }
    // a version 16 state as 0.9 wrote it (nothing after the loop fade flag): the LFOs at their defaults
    {
        PluginState st = someState (), back;
        for (uint32_t id = kModLfoBase; id < kNumParams; ++id)
            st.has[id] = false; // (they did not exist)
        const size_t mapBytes = 4 + encodeModMap (st.mods).size ();
        CHECK (roundTrip (st, back, 16, (int32)mapBytes), "read a 0.9 state");
        CHECK (back.mods.list.empty (), "no mappings");
        for (uint32_t id = kModLfoBase; id < kNumParams; ++id)
            CHECK (!back.has[id], "LFO parameter %u read from an old state", id);
    }
    // a version 18 state (before the Sub and High bands lost their buttons): in slot 0 a Smacheratr with
    // its Sub band off (a Range of 12 dB) and its High band on (10 dB), in slot 1 a Gentlr the other way
    // round. A band that was off gets Range 0, one that was on keeps its Range: the same sound. A version
    // 19 state keeps them as saved
    for (int32 version : {18, 19})
    {
        PluginState st = someState (), back;
        st.norm[slotParam (0, kSlotType)] = toNormalized (slotParam (0, kSlotType), kFxSmacheratr);
        st.norm[slotParam (1, kSlotType)] = toNormalized (slotParam (1, kSlotType), kFxGentlr);
        auto set = [&] (int slot, uint32_t id, double v) { st.norm[slotBlockParam (slot, id)] = v; };
        const double r12 = smacheratr::toNormalized (smacheratr::kClaritySubRange, 12.0), r10 = smacheratr::toNormalized (smacheratr::kClarityHighRange, 10.0);
        set (0, smacheratr::kClaritySub, 0.0), set (0, smacheratr::kClaritySubRange, r12);
        set (0, smacheratr::kClarityHigh, 1.0), set (0, smacheratr::kClarityHighRange, r10);
        set (1, gentlr::kSubOn, 1.0), set (1, gentlr::kSubRange, r12);
        set (1, gentlr::kHighOn, 0.0), set (1, gentlr::kHighRange, r10);
        CHECK (roundTrip (st, back, version), "read version %d", version);
        auto at = [&] (int slot, uint32_t id) { return back.norm[slotBlockParam (slot, id)]; };
        const bool old = version < 19;
        CHECK (at (0, smacheratr::kClaritySubRange) == (old ? 0.0 : r12) && at (0, smacheratr::kClarityHighRange) == r10 &&
                   back.has[slotBlockParam (0, smacheratr::kClaritySubRange)],
               "version %d: the Smacheratr's Sub (off) %s, High (on) kept", version, old ? "at 0" : "kept");
        CHECK (at (1, gentlr::kSubRange) == r12 && at (1, gentlr::kHighRange) == (old ? 0.0 : r10),
               "version %d: the Gentlr's Sub (on) kept, High (off) %s", version, old ? "at 0" : "kept");
        CHECK (back.norm[kFilterFreq] == 0.4, "version %d: the rest", version);
    }
    // a version 19 state (before Gentlr's band Slope): its Smacheratr (slot 0) and Gentlr (slot 1) get
    // Classic, the shape their bands had; a Para (slot 2) keeps what its own place holds. A version 20
    // state keeps them as saved (over the Slope's three choices then: Signature at 0.5), and a new slot
    // has Signature (12 / 12 before 23)
    const double signature = smacheratr::toNormalized (smacheratr::kClaritySlope, smacheratr::kSlopeSignature);
    for (int32 version : {19, 20})
    {
        PluginState st = someState (), back;
        st.norm[slotParam (0, kSlotType)] = toNormalized (slotParam (0, kSlotType), kFxSmacheratr);
        st.norm[slotParam (1, kSlotType)] = toNormalized (slotParam (1, kSlotType), kFxGentlr);
        st.norm[slotParam (2, kSlotType)] = toNormalized (slotParam (2, kSlotType), kFxPara);
        const uint32_t smSlope = slotBlockParam (0, smacheratr::kClaritySlope), gtSlope = slotBlockParam (1, gentlr::kSlope);
        const uint32_t paraSame = slotBlockParam (2, smacheratr::kClaritySlope);
        st.norm[smSlope] = st.norm[gtSlope] = 0.5; // Signature
        st.norm[paraSame] = 0.3;
        CHECK (roundTrip (st, back, version), "read version %d", version);
        const bool old = version < 20;
        const double want = old ? smacheratr::classicSlopeNorm () : signature;
        CHECK (back.norm[smSlope] == want && back.norm[gtSlope] == want && back.has[smSlope] && back.has[gtSlope],
               "version %d: the Smacheratr's and the Gentlr's Slope %s", version, old ? "Classic" : "as saved");
        CHECK (back.norm[paraSame] == 0.3, "version %d: the Para's own place as saved", version);
    }
    CHECK (fxBlockTable (kFxSmacheratr).defaultNormalized (smacheratr::kClaritySlope) == signature &&
               fxBlockTable (kFxGentlr).defaultNormalized ((uint32_t)fxBlockOf (kFxGentlr, gentlr::kSlope)) == signature,
           "a new slot: Signature");
    // Alt Signature (24): a state from before has the Slope over three choices (0, 0.5, 1: 12 / 12,
    // Signature, Classic), read as the same choice; a version 24 state keeps it as saved, Alt Signature too
    for (int32 version : {23, 24})
    {
        const uint32_t smSlope = slotBlockParam (0, smacheratr::kClaritySlope), gtSlope = slotBlockParam (1, gentlr::kSlope);
        const uint32_t paraSame = slotBlockParam (2, smacheratr::kClaritySlope);
        const bool old = version < 24;
        const double saved[3] = {0.0, 0.5, 1.0};
        const int was[3] = {smacheratr::kSlope12, smacheratr::kSlopeSignature, smacheratr::kSlopeClassic};
        for (int i = 0; i < 3; ++i)
        {
            PluginState st = someState (), back;
            st.norm[slotParam (0, kSlotType)] = toNormalized (slotParam (0, kSlotType), kFxSmacheratr);
            st.norm[slotParam (1, kSlotType)] = toNormalized (slotParam (1, kSlotType), kFxGentlr);
            st.norm[slotParam (2, kSlotType)] = toNormalized (slotParam (2, kSlotType), kFxPara);
            st.norm[smSlope] = st.norm[gtSlope] = saved[i];
            st.norm[paraSame] = 0.5;
            CHECK (roundTrip (st, back, version), "read version %d", version);
            const double want = old ? smacheratr::toNormalized (smacheratr::kClaritySlope, was[i]) : saved[i];
            CHECK (back.norm[smSlope] == want && back.norm[gtSlope] == want && back.norm[paraSame] == 0.5,
                   "version %d, Slope saved at %.1f: %s (the Para's place untouched)", version, saved[i],
                   smacheratr::paramTable ().toText (smacheratr::kClaritySlope, smacheratr::toPlain (smacheratr::kClaritySlope, want)).c_str ());
        }
        if (!old)
        {
            PluginState st = someState (), back;
            st.norm[slotParam (0, kSlotType)] = toNormalized (slotParam (0, kSlotType), kFxSmacheratr);
            st.norm[smSlope] = smacheratr::toNormalized (smacheratr::kClaritySlope, smacheratr::kSlopeAltSignature);
            CHECK (roundTrip (st, back) && std::lround (smacheratr::toPlain (smacheratr::kClaritySlope, back.norm[smSlope])) ==
                                               smacheratr::kSlopeAltSignature,
                   "Alt Signature in a slot: as saved");
        }
    }
    // Gentlr on and the Signature Slope by default (23): a new Smemplr's first slot (Smacheratr) and a new
    // Smacheratr or Gentlr slot have them; a state from before 23 keeps what it saved, and where it lacks
    // them in the first slot (the defaults then) Gentlr off and 12 / 12
    {
        const uint32_t clarity = slotBlockParam (0, smacheratr::kClarity), slope = slotBlockParam (0, smacheratr::kClaritySlope);
        CHECK (defaultNormalized (clarity) == 1.0 && defaultNormalized (slope) == signature, "a new Smemplr's Smacheratr: Gentlr on, Signature");
        CHECK (fxBlockTable (kFxSmacheratr).defaultNormalized (smacheratr::kClarity) == 1.0,
               "a new Smacheratr slot (Editor::addFx: the effect's own defaults): Gentlr on");
        for (int32 version : {22, 23})
        {
            PluginState st = someState (), back;
            st.has[clarity] = st.has[slope] = false; // (a state without them)
            CHECK (roundTrip (st, back, version), "read version %d", version);
            const bool old = version < 23;
            // (what a state lacks, the processor and the controller take from the defaults: on, Signature)
            CHECK (old ? back.norm[clarity] == 0.0 && back.norm[slope] == 0.0 && back.has[clarity] && back.has[slope]
                       : !back.has[clarity] && !back.has[slope],
                   "version %d without them: %s", version, old ? "off, 12 / 12" : "the defaults");
            PluginState saved = someState (), backSaved;
            saved.norm[clarity] = 1.0;
            saved.norm[slope] = 1.0; // (Classic, over the three choices then)
            CHECK (roundTrip (saved, backSaved, version) && backSaved.norm[clarity] == 1.0 &&
                       backSaved.norm[slope] == smacheratr::classicSlopeNorm (),
                   "version %d with them: as saved", version);
        }
        // another effect in the first slot (a Para): its places are its own, untouched
        {
            PluginState st = someState (), back;
            st.norm[slotParam (0, kSlotType)] = toNormalized (slotParam (0, kSlotType), kFxPara);
            st.has[clarity] = st.has[slope] = false;
            CHECK (roundTrip (st, back, 22) && !back.has[clarity] && !back.has[slope], "version 22, a Para in the first slot: untouched");
        }
        // the old saturator after the rack keeps its defaults: off, its Gentlr off
        CHECK (defaultNormalized (kTailBase + pk::kTailOn) == 0.0 && defaultNormalized (kTailExtBase + pk::kTailExtClarity) == 0.0,
               "the old saturator after the rack: off, its Gentlr off");
    }
    // the hidden MIDI parameters are never saved (whatever the state holds for them), and a state from
    // before the parameters after them (the Grid) reads without them: their defaults
    {
        PluginState st = someState (), back;
        st.norm[kMidiSustain] = 1.0;
        st.norm[kMidiPitchBend] = 0.9;
        for (uint32_t id = kGridOn; id < kNumParams; ++id)
            st.has[id] = false;
        CHECK (roundTrip (st, back), "read");
        CHECK (!back.has[kMidiSustain] && !back.has[kMidiPitchBend] && !back.has[kMidiModWheel], "MIDI parameters saved");
        for (uint32_t id = kGridOn; id < kNumParams; ++id)
            CHECK (!back.has[id], "parameter %u read from a state without it", id);
        PluginState st2 = someState (), back2;
        st2.norm[kGridOn] = 1.0;
        st2.norm[kGridSize] = toNormalized (kGridSize, 4);
        CHECK (roundTrip (st2, back2) && back2.has[kGridOn] && back2.norm[kGridOn] == 1.0 &&
                   back2.norm[kGridSize] == toNormalized (kGridSize, 4),
               "the Grid saved");
        // the loop's Sync and Beat (1017, 1018): saved, and off in a state without them
        PluginState st3 = someState (), back3;
        st3.norm[kLoopSync] = 1.0;
        st3.norm[kLoopBeat] = 1.0;
        CHECK (roundTrip (st3, back3) && back3.has[kLoopSync] && back3.norm[kLoopSync] == 1.0 && back3.has[kLoopBeat] &&
                   back3.norm[kLoopBeat] == 1.0,
               "Sync and Beat saved");
        CHECK (defaultNormalized (kLoopSync) == 0.0 && defaultNormalized (kLoopBeat) == 0.0, "Sync and Beat off by default");
    }
    // a version 21 state (before Oversampling): its Smacheratrs' Hi-Quality (slot 0 on at 0.7, slot 1 off
    // at 0.3) becomes 4x and Off, a Levlr's drives (slot 2; the place held 0) get 4x, and the old
    // saturator after the rack's Hi-Quality too. A version 22 state keeps them as saved (2x)
    for (int32 version : {21, 22})
    {
        PluginState st = someState (), back;
        st.norm[slotParam (0, kSlotType)] = toNormalized (slotParam (0, kSlotType), kFxSmacheratr);
        st.norm[slotParam (1, kSlotType)] = toNormalized (slotParam (1, kSlotType), kFxSmacheratr);
        st.norm[slotParam (2, kSlotType)] = toNormalized (slotParam (2, kSlotType), kFxLevlr);
        const uint32_t os0 = slotBlockParam (0, smacheratr::kOversampling), os1 = slotBlockParam (1, smacheratr::kOversampling);
        const uint32_t lv = slotBlockParam (2, (uint32_t)fxBlockOf (kFxLevlr, levlr::kDriveOversampling));
        const uint32_t end = kTailExtBase + pk::kTailExtOversampling;
        const bool old = version < 22;
        st.norm[os0] = old ? 0.7 : 0.5;
        st.norm[os1] = old ? 0.3 : 0.5;
        st.norm[lv] = old ? 0.0 : 0.5;
        st.norm[end] = old ? 0.0 : 0.5;
        CHECK (roundTrip (st, back, version), "read version %d", version);
        if (old)
            CHECK (back.norm[os0] == 1.0 && back.norm[os1] == 0.0 && back.norm[lv] == 1.0 && back.has[lv] && back.norm[end] == 0.0,
                   "version 21: Hi-Quality on 4x, off Off, the Levlr's drives 4x (%.2f %.2f %.2f %.2f)", back.norm[os0], back.norm[os1],
                   back.norm[lv], back.norm[end]);
        else
            CHECK (back.norm[os0] == 0.5 && back.norm[os1] == 0.5 && back.norm[lv] == 0.5 && back.norm[end] == 0.5, "version 22: as saved");
    }
    // cut short in the mappings: the rest of the state still loads, without them
    {
        PluginState st = someState (), back;
        addMapping (st.mods, 2, kVolume, 0.5, -1);
        CHECK (roundTrip (st, back, -1, 7), "read a state cut short");
        CHECK (back.mods.list.empty () && back.norm[kFilterFreq] == 0.4, "cut short: %zu mappings", back.mods.list.size ());
    }
    std::printf ("state tests: %d checks, %d failures\n", gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}
