#include "State.h"

#include "smacheratr/src/core/TailExt.h"

#include "base/source/fstreamer.h"

#include <algorithm>

namespace gentlr {

using namespace Steinberg;

namespace {
constexpr int32 kMagic = 0x474E544C; // 'GNTL'
constexpr int32 kVersion = 2;
constexpr int32 kSubHighRange = 2; // 2: the Sub and High bands (Gentlr's and the end saturator's) work while their Range is above 0 dB
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
    // the Sub and High bands had an On (off by default) and their Ranges 8 and 6 dB by default: a band
    // that was off gets Range 0, one that was on keeps its Range (the same sound); the end saturator's too
    if (version < kSubHighRange)
    {
        smacheratr::subHighStateToRange (st.norm, st.has, kSubOn, kSubRange, kHighOn, kHighRange);
        st.has[kSubRange] = st.has[kHighRange] = true;
        smacheratr::tailSubHighToRange (st.norm, st.has, kTailExt2Base, kTailExt3Base);
    }
    return true;
}

} // namespace gentlr
