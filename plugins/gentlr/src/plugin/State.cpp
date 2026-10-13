#include "State.h"

#include "smacheratr/src/core/TailExt.h"

#include "base/source/fstreamer.h"

#include <algorithm>

namespace gentlr {

using namespace Steinberg;

namespace {
constexpr int32 kMagic = 0x474E544C; // 'GNTL'
constexpr int32 kVersion = 7;
constexpr int32 kSubHighRange = 2; // 2: the Sub and High bands (Gentlr's and the end saturator's) work while their Range is above 0 dB
constexpr int32 kClassicSlope = 3; // 3: the bands' Slope, Gentlr's and the end saturator's (Classic for states from before it)
constexpr int32 kOversamplingChoice = 4; // 4: the end saturator's Oversampling Off / 2x / 4x (its Hi-Quality switch before)
// 5: the end saturator and its Gentlr on by default, Gentlr's Slope Signature (older states keep the
// old defaults where they lack them)
constexpr int32 kNewDefaults = 5;
// 6: Gentlr's Slope has a fourth choice, Alt Signature: a Slope saved before (three choices) is read as
// the same choice
constexpr int32 kAltSignature = 6;
// 7: every band's Threshold goes down to -90 dB (-60 before: a Threshold saved then keeps its dB), band 1
// at 100 Hz, Range 20 dB, Threshold -82 dB for a new instance (250 Hz, 8 dB, -18 dB before: older states
// keep those where they lack them), and Band Shelves (off for a new instance: on for older states)
constexpr int32 kWideThresholds = 7;

void keepOld (State& st, uint32 id, double plain)
{
    if (st.has[id])
        return;
    st.norm[id] = toNormalized (id, plain);
    st.has[id] = true;
}
} // namespace

bool writeState (IBStream* stream, const State& st)
{
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
    // IDs this version doesn't know (from a newer one) are skipped; missing ones keep their defaults
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
    // the Thresholds were -60 to 0 dB: each one saved is read as the dB it had (its normalized value on
    // the old range, put on the new one; the end saturator's are Smacheratr's and kept that range); band 1's
    // defaults were 250 Hz, Range 8 dB and Threshold -18 dB, and the bands turned into shelves at the ends
    if (version < kWideThresholds)
    {
        for (int k = 0; k < kAllBands; ++k)
        {
            const uint32 id = thresholdParam (k);
            if (st.has[id])
                st.norm[id] = toNormalized (id, kThresholdMinBeforeDb + st.norm[id] * (kThresholdMaxDb - kThresholdMinBeforeDb));
        }
        keepOld (st, bandParam (0, kFreq), kBand1BeforeHz);
        keepOld (st, bandParam (0, kRange), kBand1BeforeRangeDb);
        keepOld (st, bandParam (0, kThreshold), smacheratr::kClarityThresholdDb);
        keepOld (st, kBandShelves, 1.0);
    }
    // Gentlr's Slope (and the end saturator's) had three choices before Alt Signature: a value saved then is read
    // as the same choice (first: the conversions below set the Slope as it is now)
    if (version < kAltSignature)
    {
        smacheratr::tailSlopeFromThreeChoices (st.norm, st.has, kTailExt3Base);
        smacheratr::slopeFromThreeChoices (st.norm, st.has, kSlope);
    }
    // the Sub and High bands had an On (off by default) and their Ranges 8 and 6 dB by default: a band
    // that was off gets Range 0, one that was on keeps its Range (the same sound); the end saturator's too
    if (version < kSubHighRange)
    {
        smacheratr::subHighStateToRange (st.norm, st.has, kSubOn, kSubRange, kHighOn, kHighRange);
        st.has[kSubRange] = st.has[kHighRange] = true;
        smacheratr::tailSubHighToRange (st.norm, st.has, kTailExt2Base, kTailExt3Base);
    }
    // bands 1 and 2 had one shape before their Slope (Gently, as Gentlr was called then, included):
    // Classic, the same sound (a new instance gets Signature); the end saturator's Gentlr too
    if (version < kClassicSlope)
    {
        st.norm[kSlope] = smacheratr::classicSlopeNorm ();
        st.has[kSlope] = true;
        smacheratr::tailSlopeToClassic (st.norm, st.has, kTailExt3Base);
    }
    // the end saturator's Oversampling was its Hi-Quality switch: on is 4x, off is Off
    if (version < kOversamplingChoice)
        smacheratr::tailOversamplingFromHiQuality (st.norm, st.has, kTailExtBase);
    // the defaults were the end saturator off, its Gentlr off and Gentlr's Slope 12 / 12: a state saved
    // then keeps them where it lacks them (after the Slope's Classic above), and Gentlr's own Slope too
    if (version < kNewDefaults)
    {
        smacheratr::tailOldDefaults (st.norm, st.has, kTailBase, kTailExtBase, kTailExt3Base);
        if (!st.has[kSlope])
        {
            st.norm[kSlope] = smacheratr::oldDefaultSlopeNorm ();
            st.has[kSlope] = true;
        }
    }
    return true;
}

} // namespace gentlr
