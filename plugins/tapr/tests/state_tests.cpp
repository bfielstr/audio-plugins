// Tapr's saved state (plugin/State.cpp) on its own: a round trip of every parameter, a partial state, a
// state from a newer build (parameters this one does not know), a stream that is not Tapr's, and the
// parameter table's defaults and texts. Run: ./tapr_state_tests
#include "Params.h"
#include "plugin/State.h"

#include "base/source/fstreamer.h"
#include "public.sdk/source/common/memorystream.h"

#include <cmath>
#include <cstdio>
#include <string>

using namespace Steinberg;
using namespace tapr;

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
        st.norm[kDrive] = 0.85;
        st.has[kDrive] = true;
        MemoryStream s;
        CHECK (writeState (&s, st), "write partial");
        s.seek (0, IBStream::kIBSeekSet, nullptr);
        State back;
        CHECK (readState (&s, back), "read partial");
        CHECK (back.norm[kDrive] == 0.85 && back.has[kDrive] && back.norm[kMix] == defaultNormalized (kMix) && !back.has[kMix] &&
                   back.norm[kCutLo] == defaultNormalized (kCutLo),
               "partial: Drive 85 %%, the rest at the defaults");
    }
    // a newer build's state: its extra parameters are skipped, the known ones read
    {
        MemoryStream s;
        {
            IBStreamer w (&s, kLittleEndian);
            w.writeInt32 (0x52504154);
            w.writeInt32 (7);
            w.writeInt32 (2);
            w.writeInt32u (kNumParams + 5);
            w.writeDouble (0.5);
            w.writeInt32u (kMix);
            w.writeDouble (0.9);
        }
        s.seek (0, IBStream::kIBSeekSet, nullptr);
        State back;
        CHECK (readState (&s, back), "a newer version reads");
        CHECK (back.norm[kMix] == 0.9 && back.has[kMix], "its known values kept");
    }
    // not Tapr's
    {
        MemoryStream s;
        {
            IBStreamer w (&s, kLittleEndian);
            w.writeInt32 (0x52505753); // sweepr's
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
        CHECK (t.info (kTapeOn).def == 1.0 && t.info (kDrive).def == 0.75 && t.info (kSpeed).def == 0.8 && t.info (kCompression).def == 0.35 &&
                   t.info (kCompPlace).def == (double)kPre && t.info (kCutOn).def == 1.0 && t.info (kCutLo).def == 0.0 && t.info (kCutMid).def == 0.0 &&
                   t.info (kCutHi).def == 0.0 && t.info (kCutPlace).def == (double)kPost && t.info (kMix).def == 1.0 && t.info (kOutput).def == -4.4 &&
                   t.info ((uint32_t)kTailBase + pk::kTailOn).def == 0.0 && std::fabs (t.info (kCutMidFreq).def - 982.6) < 1e-9,
               "Tape on, Drive 75 %%, Speed 80 %%, Compression 35 %% Pre, Cut on and flat, Post, Mix 100 %%, Output -4.4 dB, the end "
               "saturator off");
        CHECK (t.toText (kCompPlace, kPre) == "Pre" && t.toText (kCutPlace, kPost) == "Post", "the places' names");
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
