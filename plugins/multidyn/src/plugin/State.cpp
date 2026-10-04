#include "State.h"

#include "smacheratr/src/core/Params.h"

#include "smacheratr/src/core/TailExt.h"

#include "base/source/fstreamer.h"

#include <algorithm>

namespace multidyn {

using namespace Steinberg;

namespace {
constexpr int32 kMagic = 0x4d44594e; // 'MDYN'
constexpr int32 kClarityFullRange = 2; // 2: the end saturator's Clarity Frequency 20 Hz - 20 kHz
constexpr int32 kClarityOneButton = 3; // 3: one Clarity button in the end saturator
constexpr int32 kOttDefaults = 4;      // 4: Live's OTT preset's gain staging baked in (Params.h)
constexpr int32 kStyleAdded = 5;       // 5: Style (OTT for new instances; older states keep Character)
constexpr int32 kSubHighRange = 6;     // 6: no Sub and High buttons in the saturator (a band works while its Range is above 0)
constexpr int32 kClassicSlope = 7;     // 7: the saturator's Gentlr Slope (Classic for states from before it)
static_assert (kStateVersion == kClassicSlope);
} // namespace

bool writeState (IBStream* stream, const State& st, int32 version)
{
    const int32 kVersion = version;
    IBStreamer s (stream, kLittleEndian);
    int32 present = 0;
    for (uint32 id = 0; id < kNumParams; ++id)
        present += st.has[id] ? 1 : 0;
    bool ok = s.writeInt32 (kMagic) && s.writeInt32 (kVersion) && s.writeInt32 (present);
    for (uint32 id = 0; ok && id < kNumParams; ++id)
        if (st.has[id])
            ok = s.writeInt32u (id) && s.writeDouble (st.norm[id]);
    return ok;
}

bool readState (IBStream* stream, State& st)
{
    IBStreamer s (stream, kLittleEndian);
    int32 magic = 0, version = 0, count = 0;
    if (!s.readInt32 (magic) || magic != kMagic || !s.readInt32 (version) || version < 1 || !s.readInt32 (count) ||
        count < 0 || count > 100000)
        return false;
    for (uint32 id = 0; id < kNumParams; ++id)
    {
        st.norm[id] = defaultNormalized (id);
        st.has[id] = false;
    }
    for (int32 i = 0; i < count; ++i)
    {
        uint32 id = 0;
        double v = 0.0;
        if (!s.readInt32u (id) || !s.readDouble (v))
            return false;
        if (id < kNumParams)
        {
            st.norm[id] = std::clamp (v, 0.0, 1.0);
            st.has[id] = true;
        }
    }
    // before Style: Multidyn's own sound, Character
    if (version < kStyleAdded)
    {
        st.norm[kStyle] = toNormalized (kStyle, kStyleCharacter);
        st.has[kStyle] = true;
    }
    // the baked gains were the old ones: the difference moves into the gain controls (a missing one was
    // at its default, 0 dB, which is also where it is now)
    if (version < kOttDefaults)
    {
        migrateOldBaked (st.norm.data ());
        for (uint32_t id = 0; id < kNumParams; ++id)
            if (oldBakedShiftDb (id) != 0.0)
                st.has[id] = true;
    }
    // one Clarity button: a state from before, made to mean the same
    if (version < kClarityOneButton)
    {
        double on1 = st.has[(uint32_t)(kSatExtBase + pk::kTailExtClarity)] ? st.norm[(uint32_t)(kSatExtBase + pk::kTailExtClarity)] : 0.0, on2 = st.has[(uint32_t)(kSatExtBase + pk::kTailExtClarity2)] ? st.norm[(uint32_t)(kSatExtBase + pk::kTailExtClarity2)] : 0.0;
        double r1 = st.has[(uint32_t)(kSatExtBase + pk::kTailExtClarityRange)] ? st.norm[(uint32_t)(kSatExtBase + pk::kTailExtClarityRange)] : smacheratr::defaultNormalized (smacheratr::kClarityRange);
        double r2 = st.norm[(uint32_t)(kSatExtBase + pk::kTailExtClarity2Range)];
        smacheratr::clarityToOneButton (on1, r1, on2, r2);
        st.norm[(uint32_t)(kSatExtBase + pk::kTailExtClarity)] = on1;
        st.norm[(uint32_t)(kSatExtBase + pk::kTailExtClarityRange)] = r1;
        st.norm[(uint32_t)(kSatExtBase + pk::kTailExtClarity2Range)] = r2;
        st.has[(uint32_t)(kSatExtBase + pk::kTailExtClarity)] = st.has[(uint32_t)(kSatExtBase + pk::kTailExtClarityRange)] = st.has[(uint32_t)(kSatExtBase + pk::kTailExtClarity2Range)] = true;
    }
    // Clarity Frequency's range grew (20 - 500 Hz before): a value saved before, in the new range
    if (version < kClarityFullRange)
        for (uint32_t id : {(uint32_t)(kSatExtBase + pk::kTailExtClarityFreq)})
            if (st.has[id])
                st.norm[id] = smacheratr::clarityFreqFromNarrowRange (st.norm[id]);
    // the end saturator's Sub and High bands had a button each (off by default) and Ranges of 8 and 6 dB
    // by default: a band that was off gets Range 0, one that was on keeps its Range (the same sound)
    if (version < kSubHighRange)
        smacheratr::tailSubHighToRange (st.norm, st.has, kSatExt2Base, kSatExt3Base);
    // the saturator's Gentlr bands had one shape before their Slope: Classic, the same sound (a new
    // instance gets 12 / 12)
    if (version < kClassicSlope)
        smacheratr::tailSlopeToClassic (st.norm, st.has, kSatExt3Base);
    return true;
}

} // namespace multidyn
