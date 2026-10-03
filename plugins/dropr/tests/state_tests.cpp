// Dropr's saved state (plugin/State.cpp) on its own: a round trip of every parameter, a state from the
// drawn-shape Dropr (state version 1, never released) loading as the defaults, a partial state and a
// stream that is not Dropr's. Run: ./dropr_state_tests
#include "Params.h"
#include "plugin/State.h"

#include "base/source/fstreamer.h"
#include "public.sdk/source/common/memorystream.h"

#include <cmath>
#include <cstdio>

using namespace Steinberg;
using namespace dropr;

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
    // the drawn-shape Dropr's state (version 1: Sensitivity, Length, ..., the shape's points, its tail):
    // every parameter at its new default
    {
        MemoryStream s;
        {
            IBStreamer w (&s, kLittleEndian);
            w.writeInt32 (0x504F5244);
            w.writeInt32 (1);
            const int32 count = 60;
            w.writeInt32 (count);
            for (int32 id = 0; id < count; ++id)
            {
                w.writeInt32u ((uint32)id);
                w.writeDouble (0.9);
            }
        }
        s.seek (0, IBStream::kIBSeekSet, nullptr);
        State back;
        CHECK (readState (&s, back), "a version 1 state reads");
        int off = 0;
        for (uint32_t id = 0; id < kNumParams; ++id)
            off += std::fabs (back.norm[id] - defaultNormalized (id)) < 1e-12 ? 0 : 1;
        CHECK (off == 0, "and loads as the defaults (%d parameters not)", off);
        CHECK (std::fabs (toPlain (kInput, back.norm[kInput]) - 30.0) < 1e-9 && std::lround (toPlain (kBands, back.norm[kBands])) == 6,
               "Input +30 dB, 6 bands");
    }
    // a partial state: the rest at their defaults
    {
        State st;
        st.norm[kDownThreshold] = 0.25;
        st.has[kDownThreshold] = true;
        MemoryStream s;
        CHECK (writeState (&s, st), "write partial");
        s.seek (0, IBStream::kIBSeekSet, nullptr);
        State back;
        CHECK (readState (&s, back), "read partial");
        CHECK (back.norm[kDownThreshold] == 0.25 && back.norm[kRelease] == defaultNormalized (kRelease), "partial: the rest at the defaults");
    }
    // not Dropr's
    {
        MemoryStream s;
        {
            IBStreamer w (&s, kLittleEndian);
            w.writeInt32 (0x12345678);
            w.writeInt32 (2);
        }
        s.seek (0, IBStream::kIBSeekSet, nullptr);
        State back;
        CHECK (!readState (&s, back), "another plug-in's stream is refused");
    }
    std::printf ("dropr state: %d checks, %d failures\n", gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}
