// Sweepr's saved state (plugin/State.cpp) on its own: a round trip of every parameter, a partial state, a
// state from a newer build (parameters this one does not know), a stream that is not Sweepr's, and the
// parameter table's defaults and texts. Run: ./sweepr_state_tests
#include "Params.h"
#include "plugin/State.h"

#include "base/source/fstreamer.h"
#include "public.sdk/source/common/memorystream.h"

#include <cmath>
#include <cstdio>
#include <string>

using namespace Steinberg;
using namespace sweepr;

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
                   back.norm[bandParam (0, kFreq)] == defaultNormalized (bandParam (0, kFreq)),
               "partial: Drive 85 %%, the rest at the defaults");
    }
    // a newer build's state: its extra parameters are skipped, the known ones read
    {
        MemoryStream s;
        {
            IBStreamer w (&s, kLittleEndian);
            w.writeInt32 (0x52505753);
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
    // not Sweepr's
    {
        MemoryStream s;
        {
            IBStreamer w (&s, kLittleEndian);
            w.writeInt32 (0x5453494D); // another plug-in's
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
        CHECK (t.info (bandParam (0, kFreq)).def == 110.0 && t.info (bandParam (0, kGain)).def == 13.0 && t.info (bandParam (1, kFreq)).def == 320.0 &&
                   t.info (bandParam (1, kGain)).def == -15.0 && t.info (bandParam (0, kLfoDepth)).def == 1.0 &&
                   t.info (bandParam (1, kLfoDepth)).def == 1.0 && t.info (kLink).def == 0.0 && t.info (kTapeOn).def == 1.0 &&
                   t.info (kMix).def == 1.0 && t.info (kSoftClip).def == 0.0 && t.info (kHardClip).def == 0.0,
               "A 110 Hz +13 dB, B 320 Hz -15 dB, both swept +1 octave, Link off, Tape on, Mix 100 %%, the clips off");
        // the old end saturator's blocks: still there (saved IDs stay put), retired; nothing else is
        int wrong = 0;
        for (uint32_t id = 0; id < kNumParams; ++id)
            wrong += t.info (id).retired != (id >= kTailBase && id < kTailExt4Base + pk::kTailExt4Fields);
        CHECK (wrong == 0, "the retired parameters are the old end saturator's (%d wrong)", wrong);
        // the values as the presets and the info box show them
        CHECK (t.toText (bandParam (0, kLfoDepth), 1.0) == "+1.00 oct" && t.toText (bandParam (0, kLfoDepth), -2.5) == "-2.50 oct" &&
                   t.toText (bandParam (0, kLfoDepth), 0.0) == "0.00 oct",
               "Depth in octaves: %s", t.toText (bandParam (0, kLfoDepth), 1.0).c_str ());
        double v = 0.0;
        CHECK (t.fromText (bandParam (0, kLfoDepth), "-1.5 oct", v) && v == -1.5, "Depth typed: %.2f", v);
        CHECK (t.toText (bandParam (0, kLfoDiv), kDiv4) == "1/4" && t.toText (bandParam (0, kLfoShape), kSmoothRandom) == "Smooth Random",
               "the division and shape names");
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
