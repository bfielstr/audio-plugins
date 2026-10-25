// Pinchr's saved state (plugin/State.cpp) on its own: a round trip of every parameter, a partial state, a
// state from a newer build (parameters this one does not know), a stream that is not Pinchr's, and the
// parameter table's defaults and texts. Run: ./pinchr_state_tests
#include "Params.h"
#include "plugin/State.h"

#include "base/source/fstreamer.h"
#include "public.sdk/source/common/memorystream.h"

#include <cmath>
#include <cstdio>
#include <string>

using namespace Steinberg;
using namespace pinchr;

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
                   back.norm[kRatio] == defaultNormalized (kRatio),
               "partial: Drive set, the rest at the defaults");
    }
    // a newer build's state: its extra parameters are skipped, the known ones read
    {
        MemoryStream s;
        {
            IBStreamer w (&s, kLittleEndian);
            w.writeInt32 (0x48434E50);
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
    // not Pinchr's
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
        CHECK (t.info (kInput).def == 0.0 && t.info (kRatio).def == 4.0 && t.info (kCurve).def == (double)kGermSoft && t.info (kAttack).def == 3.0 &&
                   t.info (kRelease).def == 3.0 && t.info (kScFilter).def == 0.0 && t.info (kDirty).def == 0.0 && t.info (kMix).def == 1.0 &&
                   t.info (kDrive).def == 5.0 && t.info (kFeedback).def == 5.0 && t.info (kOutput).def == 0.0 && t.info (kLink).def == 1.0 &&
                   t.info (kMode).def == (double)kStereo && t.info (kExtSc).def == 0.0 && t.info (kScGain).def == 0.0 && t.info (kSaturation).def == 0.5 &&
                   t.info (kSoftClip).def == 0.0 && t.info (kHardClip).def == 0.0,
               "Input 0 dB, Ratio 4, Germ Soft, Attack and Release 3, SC Filter off, clean, Mix 100 %%, Drive 5, Feedback 5, Output "
               "0 dB, linked, stereo, Ext SC off at 0 dB, Saturation 50 %%, the clips off");
        // the old end saturator's blocks: still there (saved IDs stay put), retired; nothing else is
        int wrong = 0;
        for (uint32_t id = 0; id < kNumParams; ++id)
            wrong += t.info (id).retired != (id >= kTailBase && id < kTailExt4Base + pk::kTailExt4Fields);
        CHECK (wrong == 0, "the retired parameters are the old end saturator's (%d wrong)", wrong);
        CHECK (t.toText (kCurve, kZenerHard) == "Zener Hard" && t.toText (kScFilter, 5) == "300 Hz" && t.toText (kMode, kMidSide) == "Mid/Side" &&
                   t.toText (kDrive, 7.0) == "7",
               "the choices' names (%s, %s, %s, %s)", t.toText (kCurve, kZenerHard).c_str (), t.toText (kScFilter, 5).c_str (),
               t.toText (kMode, kMidSide).c_str (), t.toText (kDrive, 7.0).c_str ());
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
