// Smacheratr's saved state (plugin/State.cpp) on its own: a round trip, and states from before the Sub
// and High bands lost their buttons loading with the same sound (a band that was off: Range 0). Run:
// ./smacheratr_state_tests
#include "Params.h"
#include "plugin/State.h"

#include "base/source/fstreamer.h"
#include "public.sdk/source/common/memorystream.h"

#include <cmath>
#include <cstdio>
#include <utility>
#include <vector>

using namespace Steinberg;
using namespace smacheratr;

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

// A state as a Smacheratr of state version `version` wrote it: (ID, normalized value) pairs.
static bool readOld (int32 version, const std::vector<std::pair<uint32, double>>& values, State& back)
{
    MemoryStream s;
    {
        IBStreamer w (&s, kLittleEndian);
        w.writeInt32 (0x534D5452);
        w.writeInt32 (version);
        w.writeInt32 ((int32)values.size ());
        for (const auto& [id, v] : values)
        {
            w.writeInt32u (id);
            w.writeDouble (v);
        }
    }
    s.seek (0, IBStream::kIBSeekSet, nullptr);
    return readState (&s, back);
}

static double plain (const State& st, uint32_t id) { return toPlain (id, st.norm[id]); }

int main ()
{
    // every parameter, a value of its own, there and back (this version's own state is not converted)
    {
        State st;
        for (uint32_t id = 0; id < kNumParams; ++id)
        {
            st.norm[id] = std::fmod (0.137 * (id + 1), 1.0);
            st.has[id] = true;
        }
        st.norm[kClaritySub] = st.norm[kClarityHigh] = 0.0; // (unused now: off with a Range keeps the Range)
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
    // a state of this version with nothing in it: Sub and High at Range 0 (the defaults)
    {
        State back;
        CHECK (readOld (5, {}, back), "read");
        CHECK (plain (back, kClaritySubRange) == 0.0 && plain (back, kClarityHighRange) == 0.0, "new defaults: Range 0 dB");
    }
    // version 4 (before): Sub off with a Range of 12 dB, High on with 10 dB -> Sub 0, High 10
    {
        State back;
        CHECK (readOld (4,
                        {{kClarity, 1.0},
                         {kClaritySub, 0.0},
                         {kClaritySubRange, toNormalized (kClaritySubRange, 12.0)},
                         {kClarityHigh, 1.0},
                         {kClarityHighRange, toNormalized (kClarityHighRange, 10.0)}},
                        back),
               "read");
        CHECK (plain (back, kClaritySubRange) == 0.0 && back.has[kClaritySubRange], "Sub was off: Range 0 (%.2f)", plain (back, kClaritySubRange));
        CHECK (std::fabs (plain (back, kClarityHighRange) - 10.0) < 1e-9, "High was on: its Range kept (%.2f)", plain (back, kClarityHighRange));
        CHECK (!claritySubOn (plain (back, kClarity), plain (back, kClaritySubRange)) &&
                   clarityHighOn (plain (back, kClarity), plain (back, kClarityHighRange)),
               "the same bands work");
    }
    // version 4, the buttons on but the Ranges never saved: the old defaults (8 and 6 dB)
    {
        State back;
        CHECK (readOld (4, {{kClarity, 1.0}, {kClaritySub, 1.0}, {kClarityHigh, 1.0}}, back), "read");
        CHECK (std::fabs (plain (back, kClaritySubRange) - 8.0) < 1e-9 && std::fabs (plain (back, kClarityHighRange) - 6.0) < 1e-9,
               "Sub 8 dB, High 6 dB (%.2f / %.2f)", plain (back, kClaritySubRange), plain (back, kClarityHighRange));
    }
    // version 4 without the Sub and High bands' values (saved before they came): both 0 (they were off)
    {
        State back;
        CHECK (readOld (4, {{kClarity, 1.0}, {kClarityRange, toNormalized (kClarityRange, 8.0)}}, back), "read");
        CHECK (plain (back, kClaritySubRange) == 0.0 && plain (back, kClarityHighRange) == 0.0, "both at Range 0");
        CHECK (std::fabs (plain (back, kClarityRange) - 8.0) < 1e-9, "band 1 as saved");
    }
    // version 5 (now): the buttons mean nothing, the Ranges load as saved
    {
        State back;
        CHECK (readOld (5, {{kClaritySub, 0.0}, {kClaritySubRange, toNormalized (kClaritySubRange, 12.0)}}, back), "read");
        CHECK (std::fabs (plain (back, kClaritySubRange) - 12.0) < 1e-9, "a new state's Range as saved");
    }
    std::printf ("smacheratr state: %d checks, %d failures\n", gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}
