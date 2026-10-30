#include "State.h"

#include "base/source/fstreamer.h"

#include <algorithm>
#include <cmath>

namespace sunder {

using namespace Steinberg;

namespace {
constexpr int32 kMagic = 0x48434E50; // 'PNCH' (pinchr's: its projects load here)
// 1: pinchr. 2: sunder: Drive, Feedback and Ratio in steps 1 .. 10 (convertPinchr), Dirty always on, the bass
// enhancer (a state without it: off). 3: Input -48 .. +48 dB (Curve::Centre; convertInput), Bass Position (a
// state without it: Pre).
constexpr int32 kVersion = 3;
} // namespace

void convertInput (State& st)
{
    // -21 .. +21 dB, linear: the same dB on the new range
    if (st.has[kInput])
        st.norm[kInput] = toNormalized (kInput, -kOldInputMaxDb + 2.0 * kOldInputMaxDb * st.norm[kInput]);
}

void convertPinchr (State& st)
{
    // pinchr's ranges: Feedback real 0 .. 10, Drive steps 0 .. 10, Ratio real 1 .. 11; to the nearest step now
    // (a position below 1 to step 1, Ratio 11 to 10). Dirty is ignored (always on).
    auto steps = [&] (uint32_t id, int step) { st.norm[id] = toNormalized (id, (double)std::clamp (step, 1, kSteps)); };
    if (st.has[kFeedback])
        steps (kFeedback, stepNearest (10.0 * st.norm[kFeedback]));
    if (st.has[kDrive])
        steps (kDrive, stepNearest (10.0 * st.norm[kDrive]));
    if (st.has[kRatio])
        steps (kRatio, (int)std::lround (1.0 + 10.0 * st.norm[kRatio]));
}

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
        if (id < kNumParams) // (a newer build's parameters are skipped)
        {
            st.norm[id] = std::clamp (v, 0.0, 1.0);
            st.has[id] = true;
        }
    }
    if (version < 2)
        convertPinchr (st);
    if (version < 3)
        convertInput (st);
    return true;
}

} // namespace sunder
