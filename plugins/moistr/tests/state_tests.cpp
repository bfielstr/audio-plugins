// Moistr's saved state (plugin/State.cpp) on its own: a round trip of every parameter, a partial state, a
// state from a newer build (parameters this one does not know), a 0.18 state (version 1), a stream that is
// not Moistr's, and the parameter table's fixed points. Run: ./moistr_state_tests
#include "Params.h"
#include "plugin/State.h"

#include "base/source/fstreamer.h"
#include "public.sdk/source/common/memorystream.h"

#include <cmath>
#include <cstdio>
#include <string>

using namespace Steinberg;
using namespace moistr;

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
        st.norm[kSeed] = toNormalized (kSeed, 42.0);
        st.has[kSeed] = true;
        MemoryStream s;
        CHECK (writeState (&s, st), "write partial");
        s.seek (0, IBStream::kIBSeekSet, nullptr);
        State back;
        CHECK (readState (&s, back), "read partial");
        CHECK (std::lround (toPlain (kSeed, back.norm[kSeed])) == 42 && back.norm[kGlue] == defaultNormalized (kGlue) &&
                   !back.has[kGlue],
               "partial: Seed 42, the rest at the defaults");
    }
    // a newer build's state: its extra parameters are skipped, the known ones read
    {
        MemoryStream s;
        {
            IBStreamer w (&s, kLittleEndian);
            w.writeInt32 (0x5453494D);
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
    // a 0.18 state (version 1, IDs 0 .. 68): every stored value kept, the split's and the shifter's
    // parameters (69 ..) at their defaults from before 0.24 (legacyDefaultNormalized: Sweep off)
    {
        MemoryStream s;
        const uint32_t old = kBandCount; // (0.18's parameters: 0 .. 68)
        {
            IBStreamer w (&s, kLittleEndian);
            w.writeInt32 (0x5453494D);
            w.writeInt32 (1);
            w.writeInt32 ((int32)old);
            for (uint32_t id = 0; id < old; ++id)
            {
                w.writeInt32u (id);
                w.writeDouble (std::fmod (0.311 * (id + 1), 1.0));
            }
        }
        s.seek (0, IBStream::kIBSeekSet, nullptr);
        State back;
        CHECK (readState (&s, back), "a 0.18 state reads");
        int kept = 0, defaults = 0;
        for (uint32_t id = 0; id < kNumParams; ++id)
            if (id < old)
                kept += back.has[id] && back.norm[id] == std::fmod (0.311 * (id + 1), 1.0);
            else
                defaults += !back.has[id] && back.norm[id] == legacyDefaultNormalized (id);
        CHECK (back.norm[kSweep] == 0.0, "a 0.18 state: Sweep off");
        CHECK (kept == (int)old, "every stored value kept (%d of %u)", kept, old);
        CHECK (defaults == (int)(kNumParams - old), "the new parameters at their defaults (%d of %u)", defaults, kNumParams - old);
        // and saved again it is the current version, with everything
        for (uint32_t id = 0; id < kNumParams; ++id)
            back.has[id] = true;
        MemoryStream t;
        CHECK (writeState (&t, back), "write");
        t.seek (0, IBStream::kIBSeekSet, nullptr);
        IBStreamer r (&t, kLittleEndian);
        int32 magic = 0, version = 0, count = 0;
        r.readInt32 (magic);
        r.readInt32 (version);
        r.readInt32 (count);
        CHECK (version == 4 && count == (int32)kNumParams, "saved as version 4 (%d) with %d values", version, count);
    }
    // a 0.23 state (version 2, IDs 0 .. 91, every one): every value kept, the SWEEP stage off and its settings at
    // their defaults; a state from 0.24 (version 3) without them reads the new defaults (Sweep on)
    for (int32 version : {2, 3})
    {
        MemoryStream s;
        const uint32_t old = kSweep; // (0.23's parameters: 0 .. 91)
        {
            IBStreamer w (&s, kLittleEndian);
            w.writeInt32 (0x5453494D);
            w.writeInt32 (version);
            w.writeInt32 ((int32)old);
            for (uint32_t id = 0; id < old; ++id)
            {
                w.writeInt32u (id);
                w.writeDouble (std::fmod (0.173 * (id + 1), 1.0));
            }
        }
        s.seek (0, IBStream::kIBSeekSet, nullptr);
        State back;
        CHECK (readState (&s, back), "a version %d state reads", version);
        int kept = 0, rest = 0;
        for (uint32_t id = 0; id < kNumParams; ++id)
            if (id < old)
                kept += back.has[id] && back.norm[id] == std::fmod (0.173 * (id + 1), 1.0);
            else
                rest += !back.has[id] && back.norm[id] == (version < 3 ? legacyDefaultNormalized (id) : defaultNormalized (id));
        CHECK (kept == (int)old && rest == (int)(kNumParams - old), "version %d: %d values kept, %d at their defaults", version, kept,
               rest);
        CHECK ((back.norm[kSweep] >= 0.5) == (version >= 3), "version %d: Sweep %s", version, version >= 3 ? "on" : "off");
    }
    // not Moistr's
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
        CHECK (std::lround (t.info (kSeed).def) == 1 && t.info (kLowFreq).def == 180.0 && t.info (kMidFreq).def == 450.0 &&
                   t.info (kHighFreq).def == 3000.0 && t.info (kGap).def == 0.0 && std::lround (t.info (kPasses).def) == kPasses1 &&
                   t.info (kMix).def == 1.0,
               "Seed 1, Low 180 Hz, Mid 450 Hz, High 3 kHz, no Gap, 1 pass, Mix 100 %%");
        CHECK (t.info (kMidMove).def < t.info (kHighMove).def, "High moves more than Mid by default");
        CHECK (std::lround (t.info (kBandCount).def) == kBands3 && t.info (kDepth).def == 24.0 && t.info (kRise).def == 1.0 &&
                   t.info (kFall).def == 1.0 && t.info (kShiftOn).def == 0.0 && t.info (kShift).def == 0.0 &&
                   t.info (kShiftMix).def == 1.0,
               "3 bands, Depth 24 dB, Rise and Fall x1, the shifter off (0 Hz, Mix 100 %%)");
        CHECK (t.info (kSweep).def == 1.0 && t.info (kShelf).def == 1.0 && t.info (kSweepDrive).def == 18.0 && t.info (kDrive).def == 0.0 &&
                   t.info (kMovement).def == 0.0 && t.info (kGlue).def == 0.0 && t.info (kGrit).def == 0.0,
               "0.24: Sweep and High Shelf on, Drive 18 dB; the bands neutral (Drive, Movement, Glue, Grit 0)");
        CHECK (legacyDefaultNormalized (kSweep) == 0.0 && toPlain (kMovement, legacyDefaultNormalized (kMovement)) == 0.5 &&
                   toPlain (kGlue, legacyDefaultNormalized (kGlue)) == 0.4,
               "before 0.24: Sweep off, Movement 50 %%, Glue 40 %%");
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
