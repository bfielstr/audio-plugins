// Bubr's saved state (plugin/State.cpp) on its own: a round trip of every parameter, a partial state, a
// state from a newer build (parameters this one does not know), a stream that is not Bubr's, and the
// parameter table's defaults and texts. Run: ./bubr_state_tests
#include "Params.h"
#include "plugin/State.h"

#include "base/source/fstreamer.h"
#include "public.sdk/source/common/memorystream.h"

#include <cmath>
#include <cstdio>
#include <string>

using namespace Steinberg;
using namespace bubr;

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
        CHECK (back.norm[kDrive] == 0.85 && back.has[kDrive] && back.norm[kDryWet] == defaultNormalized (kDryWet) && !back.has[kDryWet] &&
                   back.norm[kRatio] == defaultNormalized (kRatio),
               "partial: Drive set, the rest at the defaults");
    }
    // a newer build's state: its extra parameters are skipped, the known ones read
    {
        MemoryStream s;
        {
            IBStreamer w (&s, kLittleEndian);
            w.writeInt32 (0x52425542);
            w.writeInt32 (7);
            w.writeInt32 (2);
            w.writeInt32u (kNumParams + 5);
            w.writeDouble (0.5);
            w.writeInt32u (kDryWet);
            w.writeDouble (0.9);
        }
        s.seek (0, IBStream::kIBSeekSet, nullptr);
        State back;
        CHECK (readState (&s, back), "a newer version reads");
        CHECK (back.norm[kDryWet] == 0.9 && back.has[kDryWet], "its known values kept");
    }
    // not Bubr's
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
        CHECK (std::fabs (t.info (kBoost).def - 0.55) < 1e-12 && t.info (kFreq).def == 40.0 && t.info (kIntensity).def == 0.5 &&
                   t.info (kQFactor).def == 0.2 && t.info (kDrive).def == 0.2 && t.info (kDriveX2).def == 0.0 && t.info (kGate).def == -30.0 &&
                   t.info (kAttack).def == 0.0 && t.info (kRelease).def == 100.0 && t.info (kRatio).def == 4.0 && t.info (kLowCut).def == 20.0 &&
                   t.info (kDryWet).def == 0.0 && t.info (kOversample).def == (double)kOsOff && t.info (kLookahead).def == 1.0 &&
                   t.info (kOutput).def == 0.0 && t.info (kSoftClip).def == 0.0 && t.info (kHardClip).def == 0.0,
               "Boost 55 %%, 40 Hz, Intensity 50 %%, Q-Factor 20 %%, Drive 20 %% (x2 off), the gate at -30 dB (0 ms, 100 ms, 4), Low "
               "Cut 20 Hz, Dry/Wet 0 %%, Oversample off, Lookahead on, Output 0 dB, the clips off");
        // the Freq knob as the reference's: 50 Hz at its middle, 40 Hz at 0.378
        CHECK (std::fabs (toPlain (kFreq, 0.5) - 50.0) < 1e-9 && std::fabs (toNormalized (kFreq, 40.0) - 0.37815) < 1e-4,
               "Freq's curve (%.3f Hz at the middle)", toPlain (kFreq, 0.5));
        CHECK (t.toText (kOversample, kOs4x) == "4x" && t.toText (kDriveX2, 1.0) == "On" && t.toText (kFreq, 40.0) == "40.0 Hz",
               "the texts (%s, %s, %s)", t.toText (kOversample, kOs4x).c_str (), t.toText (kDriveX2, 1.0).c_str (), t.toText (kFreq, 40.0).c_str ());
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
