// Gentlr's saved state (plugin/State.cpp) on its own: a round trip of every parameter, a state from a
// newer Gentlr (IDs this one does not know), a state with parameters missing, and a stream that is
// not Gentlr's. Run: ./gentlr_state_tests
#include "Params.h"
#include "plugin/State.h"

#include "base/source/fstreamer.h"
#include "public.sdk/source/common/memorystream.h"

#include <cmath>
#include <cstdio>
#include <utility>
#include <vector>

using namespace Steinberg;
using namespace gentlr;

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
        // the Sub band's, the High band's and the end saturator's last block among them
        CHECK (back.norm[kSubOn] == st.norm[kSubOn] && back.norm[kSubThreshold] == st.norm[kSubThreshold] &&
                   back.norm[kHighFreq] == st.norm[kHighFreq] && back.norm[kNoOverlap] == st.norm[kNoOverlap] &&
                   back.norm[kNumParams - 1] == st.norm[kNumParams - 1],
               "the Sub band, the High band, No Overlap and the last block");
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
    // a state saved before the High band and No Overlap (its 54 parameters, IDs 0 - 53): they load off,
    // so it sounds as it did
    {
        State st;
        for (uint32_t id = 0; id < kHighOn; ++id)
        {
            st.norm[id] = 1.0;
            st.has[id] = true;
        }
        MemoryStream s;
        CHECK (writeState (&s, st), "write");
        s.seek (0, IBStream::kIBSeekSet, nullptr);
        State back;
        CHECK (readState (&s, back), "read");
        CHECK (!back.has[kHighOn] && toPlain (kHighOn, back.norm[kHighOn]) == 0.0 && toPlain (kNoOverlap, back.norm[kNoOverlap]) == 0.0 &&
                   toPlain (kTailExt3Base + pk::kTailExt3High, back.norm[kTailExt3Base + pk::kTailExt3High]) == 0.0 &&
                   toPlain (kTailExt3Base + pk::kTailExt3NoOverlap, back.norm[kTailExt3Base + pk::kTailExt3NoOverlap]) == 0.0,
               "an old state: High and No Overlap off (Gentlr's and the end saturator's)");
    }
    // a version 1 state (before the Sub and High bands lost their On): a band that was off gets Range 0,
    // one that was on keeps its Range (or, not saved, the old default: Sub 8 dB, High 6 dB); the end
    // saturator's the same. The same sound
    {
        auto readV1 = [] (const std::vector<std::pair<uint32_t, double>>& values, State& back) {
            MemoryStream s;
            {
                IBStreamer w (&s, kLittleEndian);
                w.writeInt32 (0x474E544C);
                w.writeInt32 (1);
                w.writeInt32 ((int32)values.size ());
                for (const auto& [id, v] : values)
                {
                    w.writeInt32u (id);
                    w.writeDouble (v);
                }
            }
            s.seek (0, IBStream::kIBSeekSet, nullptr);
            return readState (&s, back);
        };
        auto plain = [] (const State& st, uint32_t id) { return toPlain (id, st.norm[id]); };
        const uint32_t tSubOn = kTailExt2Base + pk::kTailExt2Sub, tSubRange = kTailExt2Base + pk::kTailExt2SubRange;
        const uint32_t tHighOn = kTailExt3Base + pk::kTailExt3High, tHighRange = kTailExt3Base + pk::kTailExt3HighRange;
        State back;
        CHECK (readV1 ({{kSubOn, 0.0},
                        {kSubRange, toNormalized (kSubRange, 12.0)},
                        {kHighOn, 1.0},
                        {kHighRange, toNormalized (kHighRange, 10.0)},
                        {tSubOn, 1.0},
                        {tSubRange, toNormalized (tSubRange, 5.0)},
                        {tHighOn, 0.0},
                        {tHighRange, toNormalized (tHighRange, 9.0)}},
                       back),
               "read");
        CHECK (plain (back, kSubRange) == 0.0 && std::fabs (plain (back, kHighRange) - 10.0) < 1e-9 && back.has[kSubRange],
               "Sub (off): Range 0; High (on): 10 dB (%.2f / %.2f)", plain (back, kSubRange), plain (back, kHighRange));
        CHECK (std::fabs (plain (back, tSubRange) - 5.0) < 1e-9 && plain (back, tHighRange) == 0.0,
               "the end saturator's Sub (on): 5 dB; High (off): 0 (%.2f / %.2f)", plain (back, tSubRange), plain (back, tHighRange));
        CHECK (!bandWorks (kSub, plain (back, kSubOn), plain (back, kSubRange)) && bandWorks (kHigh, plain (back, kHighOn), plain (back, kHighRange)),
               "the same bands work");
        State missing;
        CHECK (readV1 ({{kSubOn, 1.0}, {kHighOn, 1.0}}, missing), "read");
        CHECK (std::fabs (plain (missing, kSubRange) - 8.0) < 1e-9 && std::fabs (plain (missing, kHighRange) - 6.0) < 1e-9,
               "on, their Ranges not saved: the old defaults (%.2f / %.2f)", plain (missing, kSubRange), plain (missing, kHighRange));
        State none;
        CHECK (readV1 ({{bandParam (0, kFreq), 0.5}}, none), "read");
        CHECK (plain (none, kSubRange) == 0.0 && plain (none, kHighRange) == 0.0 && plain (none, tSubRange) == 0.0 &&
                   plain (none, tHighRange) == 0.0,
               "nothing saved for them (off by default then): Range 0");
        CHECK (defaultNormalized (kSubRange) == 0.0 && defaultNormalized (kHighRange) == 0.0, "a new instance: Sub and High at Range 0");
    }
    // a state saved by Gently (Gentlr's name until 0.11: the same class IDs and the same magic, GNTL,
    // at version 2), written by hand as Gently wrote it: every value it saved loads into Gentlr
    {
        MemoryStream s;
        const uint32_t gentlyParams = 64; // every ID Gently had (0.11: the end saturator's fourth block last, 59 - 63)
        {
            IBStreamer w (&s, kLittleEndian);
            w.writeInt32 (0x474E544C); // GNTL
            w.writeInt32 (2);
            w.writeInt32 ((int32)gentlyParams);
            for (uint32_t id = 0; id < gentlyParams; ++id)
            {
                w.writeInt32u (id);
                w.writeDouble (std::fmod (0.071 * (id + 3), 1.0));
            }
        }
        s.seek (0, IBStream::kIBSeekSet, nullptr);
        State back;
        CHECK (readState (&s, back), "Gently's state read");
        int wrong = 0;
        for (uint32_t id = 0; id < gentlyParams; ++id)
            wrong += back.has[id] && back.norm[id] == std::fmod (0.071 * (id + 3), 1.0) ? 0 : 1;
        CHECK (wrong == 0, "every value Gently saved, as it was (%d not)", wrong);
        // and its bands' Slope (and the end saturator's), which Gently did not have: Classic, the shape
        // its bands had, so the project sounds as it did
        CHECK (std::lround (toPlain (kSlope, back.norm[kSlope])) == smacheratr::kSlopeClassic && back.has[kSlope] &&
                   std::lround (toPlain (kTailExt3Base + pk::kTailExt3Slope, back.norm[kTailExt3Base + pk::kTailExt3Slope])) ==
                       smacheratr::kSlopeClassic,
               "Gently's bands: Classic (Gentlr's and the end saturator's)");
    }
    // the Slope from version 3 on: as saved; not saved (and in a new instance), 12 / 12
    {
        MemoryStream s;
        {
            IBStreamer w (&s, kLittleEndian);
            w.writeInt32 (0x474E544C);
            w.writeInt32 (3);
            w.writeInt32 (1);
            w.writeInt32u (kSlope);
            w.writeDouble (toNormalized (kSlope, (double)smacheratr::kSlopeSignature));
        }
        s.seek (0, IBStream::kIBSeekSet, nullptr);
        State back;
        CHECK (readState (&s, back), "read");
        CHECK (std::lround (toPlain (kSlope, back.norm[kSlope])) == smacheratr::kSlopeSignature &&
                   std::lround (toPlain (kTailExt3Base + pk::kTailExt3Slope, back.norm[kTailExt3Base + pk::kTailExt3Slope])) == smacheratr::kSlope12,
               "version 3: Signature as saved, the end saturator's (not saved) 12 / 12");
        CHECK (defaultNormalized (kSlope) == 0.0 && defaultNormalized (kTailExt3Base + pk::kTailExt3Slope) == 0.0, "a new instance: 12 / 12");
    }
    // a state from a newer Gentlr: the IDs this one does not know are skipped, the rest read
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
    // not Gentlr's
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
    std::printf ("gentlr state: %d checks, %d failures\n", gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}
