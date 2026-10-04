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
    // the band Slope: a state from before it (version 5) loads Classic, the shape its bands had; a state of
    // version 6 loads it as saved, and one without it (or a new instance) has 12 / 12
    {
        State back;
        CHECK (readOld (5, {{kClarity, 1.0}, {kClarityRange, toNormalized (kClarityRange, 8.0)}}, back), "read");
        CHECK (std::lround (plain (back, kClaritySlope)) == kSlopeClassic && back.has[kClaritySlope], "version 5: Classic");
        State v4;
        CHECK (readOld (4, {{kClarity, 1.0}}, v4) && std::lround (plain (v4, kClaritySlope)) == kSlopeClassic, "version 4: Classic too");
        State now;
        CHECK (readOld (6, {{kClaritySlope, toNormalized (kClaritySlope, kSlopeSignature)}}, now) &&
                   std::lround (plain (now, kClaritySlope)) == kSlopeSignature,
               "version 6: as saved");
        State none;
        CHECK (readOld (6, {{kClarity, 1.0}}, none) && std::lround (plain (none, kClaritySlope)) == kSlope12, "version 6, not saved: 12 / 12");
        CHECK (defaultNormalized (kClaritySlope) == 0.0, "a new instance: 12 / 12");
    }
    // Oversampling (version 7) was the Hi-Quality switch: on -> 4x, off -> Off, a value in between on the
    // end the switch read it as; a state of version 7 keeps 2x; a new instance (and a state without it) 4x
    {
        auto osOf = [] (const State& st) { return oversamplingFactor (plain (st, kOversampling)); };
        State on, off, half, low, none, now;
        CHECK (readOld (6, {{kOversampling, 1.0}}, on) && osOf (on) == 4, "Hi-Quality on: 4x (%d)", osOf (on));
        CHECK (readOld (6, {{kOversampling, 0.0}}, off) && osOf (off) == 1, "Hi-Quality off: Off (%d)", osOf (off));
        CHECK (readOld (6, {{kOversampling, 0.6}}, half) && osOf (half) == 4, "Hi-Quality 0.6 (on): 4x (%d)", osOf (half));
        CHECK (readOld (6, {{kOversampling, 0.4}}, low) && osOf (low) == 1, "Hi-Quality 0.4 (off): Off (%d)", osOf (low));
        CHECK (readOld (3, {{kDrive, 0.5}}, none) && osOf (none) == 4, "not saved: 4x, as Hi-Quality was (%d)", osOf (none));
        CHECK (readOld (7, {{kOversampling, toNormalized (kOversampling, kOs2x)}}, now) && osOf (now) == 2, "version 7: 2x as saved");
        CHECK (oversamplingFactor (toPlain (kOversampling, defaultNormalized (kOversampling))) == 4, "a new instance: 4x");
    }
    std::printf ("smacheratr state: %d checks, %d failures\n", gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}
