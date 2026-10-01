// Detonatr's saved state (plugin/State.cpp) on its own: a round trip of every parameter, a state with
// parameters missing, an old Detonatr's state (version 1 or 2: it loads as the defaults) and a stream
// that is not Detonatr's. Run: ./detonatr_state_tests
#include "Harness.h"
#include "Params.h"
#include "plugin/State.h"

#include "public.sdk/source/common/memorystream.h"

#include <cmath>

using namespace Steinberg;
using namespace detonatr;

TEST (round_trip)
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
    CHECK (!back.fromOldDetonatr, "a version 3 state");
}

TEST (missing_parameters_take_their_defaults)
{
    State st;
    st.norm[kVocBands] = 0.25;
    st.has[kVocBands] = true;
    MemoryStream s;
    writeState (&s, st);
    s.seek (0, IBStream::kIBSeekSet, nullptr);
    State back;
    CHECK (readState (&s, back), "read");
    CHECK (back.norm[kVocBands] == 0.25, "the one saved");
    CHECK (back.norm[kTr1Base + kTrGain] == defaultNormalized (kTr1Base + kTrGain) && !back.has[kTr1Base + kTrGain], "the rest at their defaults");
}

TEST (old_detonatr_loads_as_the_defaults)
{
    // an old Detonatr's state: other parameters under the same IDs (and its recordings after them)
    for (int32 version : {1, 2})
    {
        State old;
        for (uint32_t id = 0; id < kNumParams; ++id)
        {
            old.norm[id] = 0.9;
            old.has[id] = true;
        }
        MemoryStream s;
        CHECK (writeState (&s, old, version), "write version %d", version);
        int32 recMagic = 0x52454353, slots = 0; // the old recordings' section: four empty slots
        s.write (&recMagic, 4, nullptr);
        slots = 4;
        s.write (&slots, 4, nullptr);
        s.seek (0, IBStream::kIBSeekSet, nullptr);
        State back;
        CHECK (readState (&s, back), "version %d reads", version);
        bool defaults = true;
        for (uint32_t id = 0; id < kNumParams; ++id)
            defaults = defaults && back.norm[id] == defaultNormalized (id);
        CHECK (defaults && back.fromOldDetonatr, "version %d: every parameter at its default", version);
    }
}

TEST (not_a_detonatr_state)
{
    MemoryStream s;
    int32 junk[3] = {0x12345678, 3, 0};
    s.write (junk, sizeof (junk), nullptr);
    s.seek (0, IBStream::kIBSeekSet, nullptr);
    State back;
    CHECK (!readState (&s, back), "refused");
}

DETONATR_TEST_MAIN
