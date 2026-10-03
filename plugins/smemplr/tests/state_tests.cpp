// Headless tests for Smemplr's saved state (StateIO.cpp): the modulation's mappings from version 18 on,
// and older states, which have none; the rack's Sub and High bands without buttons from 19. Run:
// ./smemplr_state_tests
#include "Modulation.h"
#include "Params.h"
#include "Rack.h"
#include "plugin/StateIO.h"

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
    // its Sub band off (a Range of 12 dB) and its High band on (10 dB), in slot 1 a Gently the other way
    // round. A band that was off gets Range 0, one that was on keeps its Range: the same sound. A version
    // 19 state keeps them as saved
    for (int32 version : {18, 19})
    {
        PluginState st = someState (), back;
        st.norm[slotParam (0, kSlotType)] = toNormalized (slotParam (0, kSlotType), kFxSmacheratr);
        st.norm[slotParam (1, kSlotType)] = toNormalized (slotParam (1, kSlotType), kFxGently);
        auto set = [&] (int slot, uint32_t id, double v) { st.norm[slotBlockParam (slot, id)] = v; };
        const double r12 = smacheratr::toNormalized (smacheratr::kClaritySubRange, 12.0), r10 = smacheratr::toNormalized (smacheratr::kClarityHighRange, 10.0);
        set (0, smacheratr::kClaritySub, 0.0), set (0, smacheratr::kClaritySubRange, r12);
        set (0, smacheratr::kClarityHigh, 1.0), set (0, smacheratr::kClarityHighRange, r10);
        set (1, gently::kSubOn, 1.0), set (1, gently::kSubRange, r12);
        set (1, gently::kHighOn, 0.0), set (1, gently::kHighRange, r10);
        CHECK (roundTrip (st, back, version), "read version %d", version);
        auto at = [&] (int slot, uint32_t id) { return back.norm[slotBlockParam (slot, id)]; };
        const bool old = version < 19;
        CHECK (at (0, smacheratr::kClaritySubRange) == (old ? 0.0 : r12) && at (0, smacheratr::kClarityHighRange) == r10 &&
                   back.has[slotBlockParam (0, smacheratr::kClaritySubRange)],
               "version %d: the Smacheratr's Sub (off) %s, High (on) kept", version, old ? "at 0" : "kept");
        CHECK (at (1, gently::kSubRange) == r12 && at (1, gently::kHighRange) == (old ? 0.0 : r10),
               "version %d: the Gently's Sub (on) kept, High (off) %s", version, old ? "at 0" : "kept");
        CHECK (back.norm[kFilterFreq] == 0.4, "version %d: the rest", version);
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
