#include "State.h"

#include "smacheratr/src/core/TailExt.h"

#include "base/source/fstreamer.h"

#include <algorithm>

namespace smoothr {

using namespace Steinberg;

namespace {
constexpr int32 kMagic = 0x534D5448; // 'SMTH'
constexpr int32 kVersion = 6;
constexpr int32 kSubHighRange = 2; // 2: the saturator's Sub and High bands work while their Range is above 0 dB
constexpr int32 kClassicSlope = 3; // 3: the saturator's Gentlr Slope (Classic for states from before it)
constexpr int32 kOversamplingChoice = 4; // 4: the saturator's Oversampling Off / 2x / 4x (its Hi-Quality switch before)
// 5: the saturator and its Gentlr on by default, Gentlr's Slope Signature (older states keep the
// old defaults where they lack them)
constexpr int32 kNewDefaults = 5;
// 6: Gentlr's Slope has a fourth choice, Alt Signature: a Slope saved before (three choices) is read as
// the same choice
constexpr int32 kAltSignature = 6;
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
    // the end saturator's Gentlr Slope had three choices before Alt Signature: a value saved then is read
    // as the same choice (first: the conversions below set the Slope as it is now)
    if (version < kAltSignature)
        smacheratr::tailSlopeFromThreeChoices (st.norm, st.has, kTailExt3Base);
    // the end saturator's Sub and High bands had a button each (off by default) and Ranges of 8 and 6 dB
    // by default: a band that was off gets Range 0, one that was on keeps its Range (the same sound)
    if (version < kSubHighRange)
        smacheratr::tailSubHighToRange (st.norm, st.has, kTailExt2Base, kTailExt3Base);
    // the saturator's Gentlr bands had one shape before their Slope: Classic, the same sound (a new
    // instance gets Signature)
    if (version < kClassicSlope)
        smacheratr::tailSlopeToClassic (st.norm, st.has, kTailExt3Base);
    // the saturator's Oversampling was its Hi-Quality switch: on is 4x, off is Off
    if (version < kOversamplingChoice)
        smacheratr::tailOversamplingFromHiQuality (st.norm, st.has, kTailExtBase);
    // the defaults were the saturator on, its Gentlr off and Gentlr's Slope 12 / 12: a state saved
    // then keeps them where it lacks them (after the Slope's Classic above)
    if (version < kNewDefaults)
        smacheratr::tailOldDefaults (st.norm, st.has, kTailBase, kTailExtBase, kTailExt3Base, true);
    return true;
}

} // namespace smoothr
