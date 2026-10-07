#include "State.h"

#include "base/source/fstreamer.h"

#include <algorithm>

namespace moistr {

using namespace Steinberg;

namespace {
constexpr int32 kMagic = 0x5453494D; // 'MIST'
// 1: the first (0.18: three moving filters).
// 2: 0.19, the multiband split and the frequency shifter (IDs 69 .. 79 appended). A version-1 state keeps every value it stored
//    (the filters' parameters are kept, though the split no longer uses them) and reads the new
//    parameters at their defaults: nothing to convert.
// A later version that changes what a saved value means converts older states in readState (as the other
// plug-ins do), so projects keep sounding the same.
constexpr int32 kVersion = 2;
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
        if (id < kNumParams) // (a newer build's parameters are skipped)
        {
            st.norm[id] = std::clamp (v, 0.0, 1.0);
            st.has[id] = true;
        }
    }
    return true;
}

} // namespace moistr
