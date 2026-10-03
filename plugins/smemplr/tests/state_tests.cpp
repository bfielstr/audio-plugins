// Headless tests for Smemplr's saved state (StateIO.cpp): the modulation's mappings from version 18 on,
// and older states, which have none. Run: ./smemplr_state_tests
#include "Modulation.h"
#include "Params.h"
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
