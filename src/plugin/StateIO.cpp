#include "StateIO.h"

#include "base/source/fstreamer.h"

#include <algorithm>

namespace simplr {

using namespace Steinberg;

namespace {
constexpr int32 kMagic = 0x534d5052; // 'SMPR'
constexpr int32 kVersion = 1;

bool writeDoubles (IBStreamer& s, const std::vector<double>& v)
{
    if (!s.writeInt32 ((int32)v.size ()))
        return false;
    for (double d : v)
        if (!s.writeDouble (d))
            return false;
    return true;
}

bool readDoubles (IBStreamer& s, std::vector<double>& v)
{
    int32 n = 0;
    if (!s.readInt32 (n) || n < 0 || n > 100000)
        return false;
    v.resize ((size_t)n);
    for (auto& d : v)
        if (!s.readDouble (d))
            return false;
    return true;
}
} // namespace

bool writeState (IBStream* stream, const PluginState& st)
{
    IBStreamer s (stream, kLittleEndian);
    bool ok = s.writeInt32 (kMagic) && s.writeInt32 (kVersion) && s.writeInt32 ((int32)kNumParams);
    for (uint32 id = 0; ok && id < kNumParams; ++id)
        ok = s.writeInt32u (id) && s.writeDouble (st.norm[id]);
    ok = ok && s.writeInt32 ((int32)st.samplePath.size ());
    ok = ok && (st.samplePath.empty () || s.writeRaw (st.samplePath.data (), (int32)st.samplePath.size ()) ==
                                              (int32)st.samplePath.size ());
    ok = ok && s.writeDouble (st.ops.cropStart) && s.writeDouble (st.ops.cropEnd) && s.writeBool (st.ops.reverse) &&
         s.writeBool (st.ops.normalize);
    ok = ok && writeDoubles (s, st.edits.manual) && writeDoubles (s, st.edits.suppressed);
    ok = ok && s.writeBool (st.constantPowerFade);
    return ok;
}

bool readState (IBStream* stream, PluginState& st)
{
    IBStreamer s (stream, kLittleEndian);
    int32 magic = 0, version = 0, count = 0;
    if (!s.readInt32 (magic) || magic != kMagic || !s.readInt32 (version) || version < 1 || !s.readInt32 (count))
        return false;
    if (count < 0 || count > 100000)
        return false;
    st.has.fill (false);
    for (int32 i = 0; i < count; ++i)
    {
        uint32 id = 0;
        double v = 0.0;
        if (!s.readInt32u (id) || !s.readDouble (v))
            return false;
        if (id < kNumParams) // unknown IDs from newer versions are skipped
        {
            st.norm[id] = std::clamp (v, 0.0, 1.0);
            st.has[id] = true;
        }
    }
    int32 len = 0;
    if (!s.readInt32 (len) || len < 0 || len > 65536)
        return false;
    st.samplePath.assign ((size_t)len, '\0');
    if (len > 0 && s.readRaw (st.samplePath.data (), len) != len)
        return false;
    if (!s.readDouble (st.ops.cropStart) || !s.readDouble (st.ops.cropEnd) || !s.readBool (st.ops.reverse) ||
        !s.readBool (st.ops.normalize))
        return false;
    if (!readDoubles (s, st.edits.manual) || !readDoubles (s, st.edits.suppressed))
        return false;
    bool cp = true;
    if (s.readBool (cp))
        st.constantPowerFade = cp;
    return true;
}

} // namespace simplr
