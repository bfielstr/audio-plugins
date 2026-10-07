// Widr's saved state (plugin/State.cpp) on its own: a round trip of every parameter (the cinema stage's
// among them), a state from before the cinema stage (0.19: its parameters load at their defaults, Cinema
// off, so it sounds as it did), a state from a newer Widr (IDs this one does not know) and a stream that
// is not Widr's. Run: ./widr_state_tests
#include "Params.h"
#include "plugin/State.h"

#include "base/source/fstreamer.h"
#include "public.sdk/source/common/memorystream.h"

#include <cmath>
#include <cstdio>
#include <utility>
#include <vector>

using namespace Steinberg;
using namespace widr;

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

// A state as a Widr of `version` wrote it, with these (id, normalized) values.
static bool readRaw (int32 version, const std::vector<std::pair<uint32_t, double>>& values, State& back)
{
    MemoryStream s;
    {
        IBStreamer w (&s, kLittleEndian);
        w.writeInt32 (0x52444957);
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

int main ()
{
    // every parameter, a value of its own, there and back
    {
        State st;
        for (uint32_t id = 0; id < kNumPluginParams; ++id)
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
        for (uint32_t id = 0; id < kNumPluginParams; ++id)
            wrong += back.has[id] && back.norm[id] == st.norm[id] ? 0 : 1;
        CHECK (wrong == 0, "every parameter back as it was (%d not)", wrong);
        CHECK (back.norm[kCinema] == st.norm[kCinema] && back.norm[kTheatre] == st.norm[kTheatre] &&
                   back.norm[lanePosition (kLaneTones)] == st.norm[lanePosition (kLaneTones)] &&
                   back.norm[laneWidth (kLaneAmbience)] == st.norm[laneWidth (kLaneAmbience)],
               "the cinema stage's among them");
    }
    // a 0.19 state (version 6, the IDs before Cinema, every one saved): the cinema stage at its defaults
    {
        std::vector<std::pair<uint32_t, double>> values;
        for (uint32_t id = 0; id < kCinema; ++id)
            values.push_back ({id, defaultNormalized (id)});
        values[kWidth].second = toNormalized (kWidth, 1.7);
        State back;
        CHECK (readRaw (6, values, back), "read a 0.19 state");
        int wrong = 0;
        for (uint32_t id = kCinema; id < kNumPluginParams; ++id)
            wrong += !back.has[id] && back.norm[id] == defaultNormalized (id) ? 0 : 1;
        CHECK (wrong == 0, "the cinema stage's parameters at their defaults (%d not)", wrong);
        CHECK (toPlain (kCinema, back.norm[kCinema]) == 0.0, "Cinema off: it sounds as it did");
        CHECK (std::fabs (toPlain (kWidth, back.norm[kWidth]) - 1.7) < 1e-9, "its own values kept");
        CHECK (std::lround (toPlain (lanePosition (kLaneVoice), back.norm[lanePosition (kLaneVoice)])) == kPosCentre &&
                   std::lround (toPlain (lanePosition (kLaneTones), back.norm[lanePosition (kLaneTones)])) == kPosWide &&
                   std::lround (toPlain (lanePosition (kLaneAmbience), back.norm[lanePosition (kLaneAmbience)])) == kPosBeyond,
               "the lanes' default positions");
    }
    // a newer Widr's state (an ID this one does not know): read, the unknown one skipped
    {
        State back;
        CHECK (readRaw (6, {{kCinema, 0.5}, {kNumPluginParams + 3, 0.25}}, back), "read a newer state");
        CHECK (back.has[kCinema] && back.norm[kCinema] == 0.5, "Cinema read");
    }
    // not Widr's
    {
        MemoryStream s;
        {
            IBStreamer w (&s, kLittleEndian);
            w.writeInt32 (0x12345678);
            w.writeInt32 (6);
            w.writeInt32 (0);
        }
        s.seek (0, IBStream::kIBSeekSet, nullptr);
        State back;
        CHECK (!readState (&s, back), "another plug-in's stream is refused");
    }
    std::printf ("widr state: %d checks, %d failures\n", gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}
