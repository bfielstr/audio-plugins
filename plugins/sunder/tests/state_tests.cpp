// Sunder's saved state (plugin/State.cpp) on its own: a round trip of every parameter, a partial state, a
// state from a newer build (parameters this one does not know), a stream that is not sunder's, pinchr's states
// (version 1: Drive, Feedback and Ratio to the nearest steps, Dirty ignored, the bass enhancer off), Input saved
// on -21 .. +21 dB before version 3 (the same dB now; Bass Position Pre), and the
// parameter table's defaults and texts. Run: ./sunder_state_tests
#include "Params.h"
#include "plugin/State.h"

#include "base/source/fstreamer.h"
#include "public.sdk/source/common/memorystream.h"

#include <cmath>
#include <cstdio>
#include <string>

using namespace Steinberg;
using namespace sunder;

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
    // not sunder's (nor pinchr's)
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
                   t.info (kRelease).def == 3.0 && t.info (kScFilter).def == 0.0 && t.info (kMix).def == 1.0 && t.info (kDrive).def == 4.0 &&
                   t.info (kFeedback).def == 3.0 && t.info (kOutput).def == 0.0 && t.info (kLink).def == 1.0 && t.info (kMode).def == (double)kStereo &&
                   t.info (kExtSc).def == 0.0 && t.info (kScGain).def == 0.0 && t.info (kSaturation).def == 0.5 && t.info (kSoftClip).def == 0.0 &&
                   t.info (kHardClip).def == 0.0 && t.info (kBass).def == 0.0,
               "Input 0 dB, Ratio 4, Germ Soft, Attack and Release 3, SC Filter off, Mix 100 %%, Drive 4, Feedback 3, Output 0 dB, linked, "
               "stereo, Ext SC off at 0 dB, Saturation 50 %%, the clips off, Bass 0");
        CHECK (t.info (kBassBoost).def == 0.55 && t.info (kBassFreq).def == 40.0 && t.info (kBassIntensity).def == 0.5 && t.info (kBassQ).def == 0.2 &&
                   t.info (kBassDrive).def == 0.2 && t.info (kBassDriveX2).def == 0.0 && t.info (kBassGate).def == -30.0 &&
                   t.info (kBassAttack).def == 0.0 && t.info (kBassRelease).def == 100.0 && t.info (kBassRatio).def == 4.0 &&
                   t.info (kBassLowCut).def == 20.0 && t.info (kBassOversample).def == (double)kBassOsOff,
               "the enhancer's settings: bubr's defaults, Oversample off");
        // the old end saturator's blocks and Dirty: still there (saved IDs stay put), retired; nothing else is
        int wrong = 0;
        for (uint32_t id = 0; id < kNumParams; ++id)
            wrong += t.info (id).retired != ((id >= kTailBase && id < kTailExt4Base + pk::kTailExt4Fields) || id == kDirty);
        CHECK (wrong == 0, "the retired parameters are the old end saturator's and Dirty (%d wrong)", wrong);
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
    // pinchr's states (version 1): Feedback (real 0 .. 10), Drive (0 .. 10) and Ratio (real 1 .. 11) to the nearest
    // step (below 1: step 1; Ratio 11: 10), Dirty ignored, the bass enhancer at its defaults (off)
    {
        struct Case
        {
            double feedback, drive, ratio;
            int feedbackStep, driveStep, ratioStep;
        };
        const Case cases[] = {{5.0, 5.0, 4.0, 4, 4, 4}, {0.0, 0.0, 1.0, 1, 1, 1}, {0.4, 1.0, 1.4, 1, 1, 1}, {10.0, 10.0, 11.0, 7, 7, 10},
                              {7.5, 8.0, 6.6, 5, 6, 7}, {2.6, 3.0, 10.4, 2, 2, 10}};
        for (const Case& c : cases)
        {
            MemoryStream s;
            {
                IBStreamer w (&s, kLittleEndian);
                w.writeInt32 (0x48434E50);
                w.writeInt32 (1);
                w.writeInt32 (5);
                w.writeInt32u (kFeedback);
                w.writeDouble (c.feedback / 10.0);
                w.writeInt32u (kDrive);
                w.writeDouble (c.drive / 10.0);
                w.writeInt32u (kRatio);
                w.writeDouble ((c.ratio - 1.0) / 10.0);
                w.writeInt32u (kDirty);
                w.writeDouble (1.0);
                w.writeInt32u (kMix);
                w.writeDouble (0.45);
            }
            s.seek (0, IBStream::kIBSeekSet, nullptr);
            State back;
            CHECK (readState (&s, back), "pinchr's state reads");
            const int fb = (int)std::lround (toPlain (kFeedback, back.norm[kFeedback])), dr = (int)std::lround (toPlain (kDrive, back.norm[kDrive])),
                      ra = (int)std::lround (toPlain (kRatio, back.norm[kRatio]));
            CHECK (fb == c.feedbackStep && dr == c.driveStep && ra == c.ratioStep && back.norm[kMix] == 0.45 && back.norm[kBass] == 0.0 && !back.has[kBass],
                   "pinchr's Feedback %.1f, Drive %.0f, Ratio %.1f: steps %d %d %d (want %d %d %d)", c.feedback, c.drive, c.ratio, fb, dr, ra,
                   c.feedbackStep, c.driveStep, c.ratioStep);
        }
        // a pinchr state without them: the steps' defaults
        MemoryStream s;
        {
            IBStreamer w (&s, kLittleEndian);
            w.writeInt32 (0x48434E50);
            w.writeInt32 (1);
            w.writeInt32 (0);
        }
        s.seek (0, IBStream::kIBSeekSet, nullptr);
        State back;
        CHECK (readState (&s, back) && back.norm[kDrive] == defaultNormalized (kDrive) && back.norm[kFeedback] == defaultNormalized (kFeedback),
               "an empty pinchr state: the defaults");
    }
    // sunder's own (version 2): the steps as saved
    {
        State st;
        for (uint32_t id = 0; id < kNumParams; ++id)
        {
            st.norm[id] = defaultNormalized (id);
            st.has[id] = true;
        }
        st.norm[kFeedback] = toNormalized (kFeedback, 9.0);
        st.norm[kRatio] = toNormalized (kRatio, 10.0);
        st.norm[kBass] = 0.7;
        MemoryStream s;
        CHECK (writeState (&s, st), "write");
        s.seek (0, IBStream::kIBSeekSet, nullptr);
        State back;
        CHECK (readState (&s, back) && std::lround (toPlain (kFeedback, back.norm[kFeedback])) == 9 &&
                   std::lround (toPlain (kRatio, back.norm[kRatio])) == 10 && back.norm[kBass] == 0.7,
               "version 2: as saved");
    }
    // Input saved before version 3 (sunder up to 0.33 and pinchr: -21 .. +21 dB, linear): the same dB on today's
    // -48 .. +48 dB; Bass Position, not in them, Pre
    {
        const double dbs[] = {-21.0, -12.5, -3.0, 0.0, 0.4, 3.0, 9.0, 17.25, 21.0};
        for (int version : {1, 2})
            for (double dbOld : dbs)
            {
                MemoryStream s;
                {
                    IBStreamer w (&s, kLittleEndian);
                    w.writeInt32 (0x48434E50);
                    w.writeInt32 (version);
                    w.writeInt32 (version == 1 ? 3 : 2);
                    w.writeInt32u (kInput);
                    w.writeDouble ((dbOld + 21.0) / 42.0);
                    w.writeInt32u (kMix);
                    w.writeDouble (0.45);
                    if (version == 1)
                    {
                        w.writeInt32u (kFeedback);
                        w.writeDouble (0.5); // pinchr's Feedback 5: step 4
                    }
                }
                s.seek (0, IBStream::kIBSeekSet, nullptr);
                State back;
                CHECK (readState (&s, back), "version %d reads", version);
                const double dbNow = toPlain (kInput, back.norm[kInput]);
                CHECK (std::fabs (dbNow - dbOld) < 1e-9 && back.norm[kMix] == 0.45 && back.norm[kBassPosition] == defaultNormalized (kBassPosition) &&
                           !back.has[kBassPosition] && std::lround (toPlain (kBassPosition, back.norm[kBassPosition])) == kBassPre &&
                           (version == 2 || std::lround (toPlain (kFeedback, back.norm[kFeedback])) == 4),
                       "version %d: Input %.2f dB loads as %.6f dB, Bass Position Pre", version, dbOld, dbNow);
            }
        // without Input: 0 dB (the default then and now)
        MemoryStream s;
        {
            IBStreamer w (&s, kLittleEndian);
            w.writeInt32 (0x48434E50);
            w.writeInt32 (2);
            w.writeInt32 (0);
        }
        s.seek (0, IBStream::kIBSeekSet, nullptr);
        State back;
        CHECK (readState (&s, back) && toPlain (kInput, back.norm[kInput]) == 0.0 && !back.has[kInput], "version 2 without Input: 0 dB");
    }
    // version 3: Input and Bass Position as saved (no conversion)
    {
        State st;
        for (uint32_t id = 0; id < kNumParams; ++id)
        {
            st.norm[id] = defaultNormalized (id);
            st.has[id] = true;
        }
        st.norm[kInput] = toNormalized (kInput, 36.5);
        st.norm[kBassPosition] = toNormalized (kBassPosition, kBassPost);
        MemoryStream s;
        CHECK (writeState (&s, st), "write");
        s.seek (0, IBStream::kIBSeekSet, nullptr);
        State back;
        CHECK (readState (&s, back) && toPlain (kInput, back.norm[kInput]) == 36.5 &&
                   std::lround (toPlain (kBassPosition, back.norm[kBassPosition])) == kBassPost,
               "version 3: Input +36.5 dB and Post as saved");
        const auto& t = paramTable ();
        CHECK (t.info (kInput).min == -48.0 && t.info (kInput).max == 48.0 && t.info (kInput).def == 0.0 && t.info (kBassPosition).def == (double)kBassPre &&
                   std::string (t.info (kBassPosition).name) == "Bass Position" && t.toText (kBassPosition, kBassPost) == "Post",
               "Input -48 .. +48 dB (0 by default), Bass Position Pre / Post (Pre by default)");
    }
    std::printf ("%d checks, %d failures\n", gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}
