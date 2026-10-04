#include "State.h"

#include "TailExt.h"

#include "base/source/fstreamer.h"

#include <algorithm>

namespace smacheratr {

using namespace Steinberg;

namespace {
constexpr int32 kMagic = 0x534D5452; // 'SMTR'
constexpr int32 kVersion = 6; // 2: the Analog-only parameter layout (version 1 states are ignored)
constexpr int32 kClarityFullRange = 3; // 3: Clarity Frequency 20 Hz - 20 kHz
constexpr int32 kClarityOneButton = 4; // 4: one Clarity button (a band works while its Range is above 0)
constexpr int32 kSubHighRange = 5;     // 5: no Sub and High buttons (those bands work while their Range is above 0)
constexpr int32 kClassicSlope = 6;     // 6: Gentlr's band Slope (Classic for states from before it)
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
    if (!s.readInt32 (magic) || magic != kMagic || !s.readInt32 (version) || version < 2 || !s.readInt32 (count) ||
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
    // one Clarity button: a state from before, made to mean the same
    if (version < kClarityOneButton)
    {
        double on1 = st.has[kClarity] ? st.norm[kClarity] : 0.0, on2 = st.has[kClarity2] ? st.norm[kClarity2] : 0.0;
        double r1 = st.has[kClarityRange] ? st.norm[kClarityRange] : smacheratr::defaultNormalized (smacheratr::kClarityRange);
        double r2 = st.norm[kClarity2Range];
        smacheratr::clarityToOneButton (on1, r1, on2, r2);
        st.norm[kClarity] = on1;
        st.norm[kClarityRange] = r1;
        st.norm[kClarity2Range] = r2;
        st.has[kClarity] = st.has[kClarityRange] = st.has[kClarity2Range] = true;
    }
    // the Sub and High bands had a button each (off by default) and Ranges of 8 and 6 dB by default: a
    // band that was off gets Range 0, one that was on keeps its Range (or the old default). Same sound.
    if (version < kSubHighRange)
    {
        smacheratr::subHighStateToRange (st.norm, st.has, kClaritySub, kClaritySubRange, kClarityHigh, kClarityHighRange);
        st.has[kClaritySubRange] = st.has[kClarityHighRange] = true;
    }
    // Clarity Frequency's range grew (20 - 500 Hz before): a value saved before, in the new range
    if (version < kClarityFullRange)
        for (uint32_t id : {kClarityFreq})
            if (st.has[id])
                st.norm[id] = smacheratr::clarityFreqFromNarrowRange (st.norm[id]);
    // Gentlr's bands had one shape before their Slope: Classic, the same sound (a new instance gets 12 / 12)
    if (version < kClassicSlope)
    {
        st.norm[kClaritySlope] = classicSlopeNorm ();
        st.has[kClaritySlope] = true;
    }
    return true;
}

} // namespace smacheratr
