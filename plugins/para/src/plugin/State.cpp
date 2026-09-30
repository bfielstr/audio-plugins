#include "State.h"

#include "smacheratr/src/core/Params.h"

#include "base/source/fstreamer.h"

#include <algorithm>

namespace para {

using namespace Steinberg;

namespace {
constexpr int32 kMagic = 0x50455252; // 'PERR'
constexpr int32 kVersion = 4;         // 2: the end saturator's Clarity Frequency 20 Hz - 20 kHz
constexpr int32 kClarityFullRange = 2;
constexpr int32 kClarityOneButton = 3; // 3: one Clarity button in the end saturator
constexpr int32 kPerBandDrive = 4;     // 4: a drive per filter, slopes 6 .. 96 dB and Brickwall
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
        double on1 = st.has[(uint32_t)(kTailExtBase + pk::kTailExtClarity)] ? st.norm[(uint32_t)(kTailExtBase + pk::kTailExtClarity)] : 0.0, on2 = st.has[(uint32_t)(kTailExtBase + pk::kTailExtClarity2)] ? st.norm[(uint32_t)(kTailExtBase + pk::kTailExtClarity2)] : 0.0;
        double r1 = st.has[(uint32_t)(kTailExtBase + pk::kTailExtClarityRange)] ? st.norm[(uint32_t)(kTailExtBase + pk::kTailExtClarityRange)] : smacheratr::defaultNormalized (smacheratr::kClarityRange);
        double r2 = st.norm[(uint32_t)(kTailExtBase + pk::kTailExtClarity2Range)];
        smacheratr::clarityToOneButton (on1, r1, on2, r2);
        st.norm[(uint32_t)(kTailExtBase + pk::kTailExtClarity)] = on1;
        st.norm[(uint32_t)(kTailExtBase + pk::kTailExtClarityRange)] = r1;
        st.norm[(uint32_t)(kTailExtBase + pk::kTailExtClarity2Range)] = r2;
        st.has[(uint32_t)(kTailExtBase + pk::kTailExtClarity)] = st.has[(uint32_t)(kTailExtBase + pk::kTailExtClarityRange)] = st.has[(uint32_t)(kTailExtBase + pk::kTailExtClarity2Range)] = true;
    }
    // Clarity Frequency's range grew (20 - 500 Hz before): a value saved before, in the new range
    if (version < kClarityFullRange)
        for (uint32_t id : {(uint32_t)(kTailExtBase + pk::kTailExtClarityFreq)})
            if (st.has[id])
                st.norm[id] = smacheratr::clarityFreqFromNarrowRange (st.norm[id]);
    // one drive for both filters and three slopes (12 / 18 / 24 dB) before: the low-pass gets the drive
    // too, the slope its place on the longer list
    if (version < kPerBandDrive)
        upgradeToPerBandDrive (
            [&] (uint32_t id, double& v) {
                if (!st.has[id])
                    return false;
                v = st.norm[id];
                return true;
            },
            [&] (uint32_t id, double v) {
                st.norm[id] = v;
                st.has[id] = true;
            });
    // Liquid used to be a toggle on top of Vocal; it is what Vocal does now
    if (st.has[kLiquid] && st.norm[kLiquid] >= 0.5)
    {
        st.norm[kMovement] = toNormalized (kMovement, kVocal);
        st.has[kMovement] = true;
    }
    return true;
}

} // namespace para
