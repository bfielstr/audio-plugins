// Gently's saved state (plugin/State.cpp) on its own: a round trip of every parameter, a state from a
// newer Gently (IDs this one does not know), a state with parameters missing, and a stream that is
// not Gently's. Run: ./gently_state_tests
#include "Params.h"
#include "plugin/State.h"

#include "base/source/fstreamer.h"
#include "public.sdk/source/common/memorystream.h"

#include <cmath>
#include <cstdio>

using namespace Steinberg;
using namespace gently;

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
        // the Sub band's and the end saturator's third block among them
        CHECK (back.norm[kSubOn] == st.norm[kSubOn] && back.norm[kSubThreshold] == st.norm[kSubThreshold] &&
                   back.norm[kNumParams - 1] == st.norm[kNumParams - 1],
               "the Sub band and the last block");
    }
    // a state with only some parameters (an older layout, or a partial one): the rest at their defaults
    {
        State st;
        st.norm[kSubOn] = 1.0;
        st.has[kSubOn] = true;
        st.norm[bandParam (1, kFreq)] = 0.25;
        st.has[bandParam (1, kFreq)] = true;
        MemoryStream s;
        CHECK (writeState (&s, st), "write");
        s.seek (0, IBStream::kIBSeekSet, nullptr);
        State back;
        CHECK (readState (&s, back), "read");
        int wrong = 0;
        for (uint32_t id = 0; id < kNumParams; ++id)
            if (id != kSubOn && id != bandParam (1, kFreq))
                wrong += !back.has[id] && back.norm[id] == defaultNormalized (id) ? 0 : 1;
        CHECK (back.has[kSubOn] && back.norm[kSubOn] == 1.0 && back.norm[bandParam (1, kFreq)] == 0.25, "the two there");
        CHECK (wrong == 0, "the others at their defaults (%d not)", wrong);
    }
    // a state from a newer Gently: the IDs this one does not know are skipped, the rest read
    {
        MemoryStream s;
        {
            IBStreamer w (&s, kLittleEndian);
            w.writeInt32 (0x474E544C);
            w.writeInt32 (7); // a later version
            w.writeInt32 (2);
            w.writeInt32u (kNumParams + 40);
            w.writeDouble (0.5);
            w.writeInt32u (kSubRange);
            w.writeDouble (0.75);
        }
        s.seek (0, IBStream::kIBSeekSet, nullptr);
        State back;
        CHECK (readState (&s, back), "read");
        CHECK (back.has[kSubRange] && back.norm[kSubRange] == 0.75, "the known one read");
    }
    // not Gently's
    {
        MemoryStream s;
        {
            IBStreamer w (&s, kLittleEndian);
            w.writeInt32 (0x12345678);
            w.writeInt32 (1);
            w.writeInt32 (0);
        }
        s.seek (0, IBStream::kIBSeekSet, nullptr);
        State back;
        CHECK (!readState (&s, back), "refused");
    }
    std::printf ("gently state: %d checks, %d failures\n", gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}
