// Dropr's saved state (plugin/State.cpp) on its own: a round trip of every parameter, a state from the
// drawn-shape Dropr (state version 1, never released) loading as the defaults, a partial state and a
// stream that is not Dropr's. Run: ./dropr_state_tests
#include "Params.h"
#include "plugin/State.h"

#include "base/source/fstreamer.h"
#include "public.sdk/source/common/memorystream.h"

#include <cmath>
#include <cstdio>
#include <string>

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
    // a version 2 state (before the end saturator's Sub and High bands lost their buttons): the Sub band
    // was on (its Range kept), the High band off (Range 0). The same sound
    {
        const uint32_t subOn = kTailExt2Base + pk::kTailExt2Sub, subRange = kTailExt2Base + pk::kTailExt2SubRange;
        const uint32_t highOn = kTailExt3Base + pk::kTailExt3High, highRange = kTailExt3Base + pk::kTailExt3HighRange;
        MemoryStream s;
        {
            IBStreamer w (&s, kLittleEndian);
            w.writeInt32 (0x504F5244);
            w.writeInt32 (2);
            w.writeInt32 (4);
            const std::pair<uint32, double> values[4] = {
                {subOn, 1.0}, {subRange, toNormalized (subRange, 5.0)}, {highOn, 0.0}, {highRange, toNormalized (highRange, 9.0)}};
            for (const auto& [id, v] : values)
            {
                w.writeInt32u (id);
                w.writeDouble (v);
            }
        }
        s.seek (0, IBStream::kIBSeekSet, nullptr);
        State back;
        CHECK (readState (&s, back), "a version 2 state reads");
        CHECK (std::fabs (toPlain (subRange, back.norm[subRange]) - 5.0) < 1e-9 && toPlain (highRange, back.norm[highRange]) == 0.0,
               "Sub (on): 5 dB, High (off): 0 (%.2f / %.2f)", toPlain (subRange, back.norm[subRange]), toPlain (highRange, back.norm[highRange]));
        CHECK (defaultNormalized (subRange) == 0.0 && defaultNormalized (highRange) == 0.0, "new: both at Range 0");
    }
    // the end saturator's Gentlr Slope: a state from before it (version 3, and 2) loads Classic, the
    // shape its bands had; version 4 as saved; a new instance 12 / 12
    {
        const uint32_t slope = kTailExt3Base + pk::kTailExt3Slope;
        auto read = [&] (int32 version, bool withSlope, State& back) {
            MemoryStream s;
            {
                IBStreamer w (&s, kLittleEndian);
                w.writeInt32 (0x504F5244);
                w.writeInt32 (version);
                w.writeInt32 (withSlope ? 2 : 1);
                w.writeInt32u (kDownThreshold);
                w.writeDouble (0.25);
                if (withSlope)
                {
                    w.writeInt32u (slope);
                    w.writeDouble (toNormalized (slope, 1.0)); // Signature
                }
            }
            s.seek (0, IBStream::kIBSeekSet, nullptr);
            return readState (&s, back);
        };
        auto slopeOf = [&] (const State& st) { return std::lround (toPlain (slope, st.norm[slope])); };
        State v3, v2, v4, v4none;
        CHECK (read (3, false, v3) && read (2, false, v2) && read (4, true, v4) && read (4, false, v4none), "read");
        CHECK (slopeOf (v3) == 2 && slopeOf (v2) == 2 && v3.has[slope], "versions 2 and 3: Classic (%ld / %ld)", slopeOf (v3), slopeOf (v2));
        CHECK (slopeOf (v4) == 1 && slopeOf (v4none) == 0, "version 4: as saved, or 12 / 12 (%ld / %ld)", slopeOf (v4), slopeOf (v4none));
        CHECK (v3.norm[kDownThreshold] == 0.25, "the rest as saved");
        CHECK (defaultNormalized (slope) == 0.0 && std::string (paramTable ().info (slope).name) == "Saturator Gentlr Slope", "a new instance: 12 / 12");
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
