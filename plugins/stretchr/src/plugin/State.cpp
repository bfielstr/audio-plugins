#include "State.h"

#include "smacheratr/src/core/Params.h"

#include <cmath>

#include "smacheratr/src/core/TailExt.h"

#include "base/source/fstreamer.h"

#include <algorithm>
#include <vector>

namespace stretchr {

using namespace Steinberg;

namespace {
constexpr int32 kMagic = 0x43525453; // 'STRC'
constexpr int32 kVersion = 9; // 2: the Algorithm choice has 8 entries (Alien)
                               // 3: the end saturator's Clarity Frequency 20 Hz - 20 kHz
                               // 4: one Clarity button in the end saturator
                               // 5: no Sub and High buttons in the end saturator (a band works while its Range is above 0)
                               // 6: the end saturator's Gentlr Slope (Classic for states from before it)
constexpr int32 kSubHighRange = 5;
constexpr int32 kClassicSlope = 6;
constexpr int32 kOversamplingChoice = 7; // 7: the end saturator's Oversampling Off / 2x / 4x (its Hi-Quality switch before)
// 8: the end saturator and its Gentlr on by default, Gentlr's Slope Signature (older states keep the
// old defaults where they lack them)
constexpr int32 kNewDefaults = 8;
constexpr int64 kMaxBlob = (int64)1 << 33;
// 9: Gentlr's Slope has a fourth choice, Alt Signature: a Slope saved before (three choices) is read as
// the same choice
constexpr int32 kAltSignature = 9;
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
    if (!ok)
        return false;
    std::vector<uint8_t> blob;
    if (st.hasClip)
        writeClip (st.clip, blob);
    if (!s.writeInt64 ((int64)blob.size ()))
        return false;
    size_t done = 0;
    while (done < blob.size ())
    {
        const int32 n = (int32)std::min<size_t> (blob.size () - done, 1 << 20);
        int32 written = 0;
        if (stream->write (blob.data () + done, n, &written) != kResultOk || written <= 0)
            return false;
        done += (size_t)written;
    }
    return true;
}

bool readState (IBStream* stream, State& st, bool withClip)
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
    // the end saturator's Gentlr Slope had three choices before Alt Signature: a value saved then is read
    // as the same choice (first: the conversions below set the Slope as it is now)
    if (version < kAltSignature)
        smacheratr::tailSlopeFromThreeChoices (st.norm, st.has, kTailExt3Base);
    // one Clarity button: a state from before, made to mean the same
    if (version < 4)
    {
        const uint32_t i1 = kTailExtBase + pk::kTailExtClarity, r1i = kTailExtBase + pk::kTailExtClarityRange,
                       i2 = kTailExtBase + pk::kTailExtClarity2, r2i = kTailExtBase + pk::kTailExtClarity2Range;
        double on1 = st.has[i1] ? st.norm[i1] : 0.0, on2 = st.has[i2] ? st.norm[i2] : 0.0;
        double r1 = st.has[r1i] ? st.norm[r1i] : smacheratr::defaultNormalized (smacheratr::kClarityRange), r2 = st.norm[r2i];
        smacheratr::clarityToOneButton (on1, r1, on2, r2);
        st.norm[i1] = on1;
        st.norm[r1i] = r1;
        st.norm[r2i] = r2;
        st.has[i1] = st.has[r1i] = st.has[r2i] = true;
    }
    // Clarity Frequency's range grew (20 - 500 Hz before): a value saved before, in the new range
    if (version < 3 && st.has[kTailExtBase + pk::kTailExtClarityFreq])
        st.norm[kTailExtBase + pk::kTailExtClarityFreq] = smacheratr::clarityFreqFromNarrowRange (st.norm[kTailExtBase + pk::kTailExtClarityFreq]);
    if (version < 2 && st.has[kAlgorithm])
    {
        // the choice was stored over 7 entries: keep the same algorithm on the longer list
        const double index = std::round (st.norm[kAlgorithm] * (kAlgorithmsBefore06 - 1));
        st.norm[kAlgorithm] = toNormalized (kAlgorithm, index);
    }
    // the end saturator's Sub and High bands had a button each (off by default) and Ranges of 8 and 6 dB
    // by default: a band that was off gets Range 0, one that was on keeps its Range (the same sound)
    if (version < kSubHighRange)
        smacheratr::tailSubHighToRange (st.norm, st.has, kTailExt2Base, kTailExt3Base);
    // the end saturator's Gentlr bands had one shape before their Slope: Classic, the same sound (a new
    // instance gets Signature)
    if (version < kClassicSlope)
        smacheratr::tailSlopeToClassic (st.norm, st.has, kTailExt3Base);
    // the end saturator's Oversampling was its Hi-Quality switch: on is 4x, off is Off
    if (version < kOversamplingChoice)
        smacheratr::tailOversamplingFromHiQuality (st.norm, st.has, kTailExtBase);
    // the defaults were the end saturator off, its Gentlr off and Gentlr's Slope 12 / 12: a state saved
    // then keeps them where it lacks them (after the Slope's Classic above)
    if (version < kNewDefaults)
        smacheratr::tailOldDefaults (st.norm, st.has, kTailBase, kTailExtBase, kTailExt3Base);
    st.hasClip = false;
    st.clip = {};
    if (!withClip)
        return true;
    int64 size = 0;
    if (!s.readInt64 (size))
        return true; // parameters only
    if (size < 0 || size > kMaxBlob)
        return false;
    if (size == 0)
        return true;
    std::vector<uint8_t> blob ((size_t)size);
    size_t done = 0;
    while (done < blob.size ())
    {
        const int32 n = (int32)std::min<size_t> (blob.size () - done, 1 << 20);
        int32 got = 0;
        if (stream->read (blob.data () + done, n, &got) != kResultOk || got <= 0)
            return false;
        done += (size_t)got;
    }
    if (!readClip (blob.data (), blob.size (), st.clip))
        return false;
    st.hasClip = true;
    return true;
}

} // namespace stretchr
