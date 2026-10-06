// Ciphr's saved state (plugin/State.cpp) on its own: a round trip of every parameter, a partial state, a
// state from a newer build (parameters this one does not know), a stream that is not Ciphr's, and the
// parameter table's fixed points. Run: ./ciphr_state_tests
#include "Params.h"
#include "plugin/State.h"

#include "base/source/fstreamer.h"
#include "public.sdk/source/common/memorystream.h"

#include <cmath>
#include <cstdio>
#include <string>

using namespace Steinberg;
using namespace ciphr;

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
    // a partial state: the rest at their defaults
    {
        State st;
        st.norm[kVariant] = toNormalized (kVariant, 42.0);
        st.has[kVariant] = true;
        MemoryStream s;
        CHECK (writeState (&s, st), "write partial");
        s.seek (0, IBStream::kIBSeekSet, nullptr);
        State back;
        CHECK (readState (&s, back), "read partial");
        CHECK (std::lround (toPlain (kVariant, back.norm[kVariant])) == 42 && back.norm[kRegen] == defaultNormalized (kRegen) &&
                   !back.has[kRegen],
               "partial: Variant 42, the rest at the defaults");
    }
    // a newer build's state: its extra parameters are skipped, the known ones read
    {
        MemoryStream s;
        {
            IBStreamer w (&s, kLittleEndian);
            w.writeInt32 (0x52485043);
            w.writeInt32 (7);
            w.writeInt32 (2);
            w.writeInt32u (kNumParams + 5);
            w.writeDouble (0.5);
            w.writeInt32u (kBlend);
            w.writeDouble (0.9);
        }
        s.seek (0, IBStream::kIBSeekSet, nullptr);
        State back;
        CHECK (readState (&s, back), "a newer version reads");
        CHECK (back.norm[kBlend] == 0.9 && back.has[kBlend], "its known values kept");
    }
    // a 0.17 state (IDs 0 .. 71 only): the parameters added since read their defaults, so the project
    // sounds as it did (Disperse off, the Classic waves, no Stretch)
    {
        State st;
        for (uint32_t id = 0; id < kStretch; ++id)
        {
            st.norm[id] = defaultNormalized (id);
            st.has[id] = true;
        }
        st.norm[kVariant] = toNormalized (kVariant, 9.0);
        MemoryStream s;
        CHECK (writeState (&s, st), "write a 0.17 state");
        s.seek (0, IBStream::kIBSeekSet, nullptr);
        State back;
        CHECK (readState (&s, back), "read a 0.17 state");
        bool defaults = true;
        for (uint32_t id = kStretch; id < kNumParams; ++id)
            defaults = defaults && !back.has[id] && back.norm[id] == defaultNormalized (id);
        CHECK (defaults, "0.18's parameters at their defaults");
        CHECK (back.norm[kDisperseOn] == 0.0 && back.norm[kWaveSet] == 0.0 && back.norm[kStretch] == 0.0,
               "Disperse off, Wave Set Classic, Stretch 0");
        CHECK (std::lround (toPlain (kVariant, back.norm[kVariant])) == 9, "its own values kept");
    }
    // the new parameters there and back at values of their own
    {
        State st;
        st.norm[kDisperseOn] = 1.0;
        st.norm[kDisperse] = 0.42;
        st.norm[kDisperseBands] = toNormalized (kDisperseBands, 27.0);
        st.norm[kDisperseSeed] = toNormalized (kDisperseSeed, 100.0);
        st.norm[kDisperseWidth] = 0.3;
        st.norm[kDisperseMix] = 0.8;
        st.norm[kStretch] = 0.6;
        st.norm[kWaveSet] = toNormalized (kWaveSet, 2.0);
        for (uint32_t id = kStretch; id < kNumParams; ++id)
            st.has[id] = true;
        MemoryStream s;
        CHECK (writeState (&s, st), "write");
        s.seek (0, IBStream::kIBSeekSet, nullptr);
        State back;
        CHECK (readState (&s, back), "read");
        CHECK (back.norm[kDisperse] == 0.42 && std::lround (toPlain (kDisperseBands, back.norm[kDisperseBands])) == 27 &&
                   std::lround (toPlain (kDisperseSeed, back.norm[kDisperseSeed])) == 100 && back.norm[kDisperseOn] == 1.0 &&
                   std::lround (toPlain (kWaveSet, back.norm[kWaveSet])) == 2 && back.norm[kStretch] == 0.6,
               "Disperse, Wave Set and Stretch back as they were");
    }
    // not Ciphr's
    {
        MemoryStream s;
        {
            IBStreamer w (&s, kLittleEndian);
            w.writeInt32 (0x5442524F); // orbitr's
            w.writeInt32 (1);
            w.writeInt32 (0);
        }
        s.seek (0, IBStream::kIBSeekSet, nullptr);
        State back;
        CHECK (!readState (&s, back), "another plug-in's state is refused");
    }
    // the table: the defaults the README describes, every name unique
    {
        const auto& t = paramTable ();
        CHECK (t.size () == kNumParams, "%u parameters", t.size ());
        CHECK (std::lround (t.info (kVariant).def) == 1 && t.info (kCross).def == 0.0 && t.info (kDrift).def == 0.0 &&
                   t.info (kInput).def == 0.0,
               "Variant 1, Cross centred, no Drift, no Input");
        CHECK (t.info (kDisperseOn).def == 0.0 && t.info (kStretch).def == 0.0 && t.info (kWaveSet).def == 0.0 &&
                   std::lround (t.info (kDisperseBands).def) == 16 && t.info (kDisperseBands).min == kMinBands &&
                   t.info (kDisperseBands).max == kMaxBands,
               "Disperse off, no Stretch, the Classic waves, 16 of 4 .. 32 bands");
        for (uint32_t a = 0; a < kNumParams; ++a)
        {
            CHECK (t.info (a).id == a, "entry %u has its own ID", a);
            for (uint32_t b = a + 1; b < kNumParams; ++b)
                CHECK (std::string (t.info (a).name) != t.info (b).name, "names %u and %u differ (%s)", a, b, t.info (a).name);
        }
    }
    std::printf ("%d checks, %d failures\n", gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}
