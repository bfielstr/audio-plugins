#include "State.h"

#include <cmath>

#include "base/source/fstreamer.h"

#include <algorithm>
#include <vector>

namespace stretchr {

using namespace Steinberg;

namespace {
constexpr int32 kMagic = 0x43525453; // 'STRC'
constexpr int32 kVersion = 2; // 2: the Algorithm choice has 8 entries (Alien)
constexpr int64 kMaxBlob = (int64)1 << 33;
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
    if (version < 2 && st.has[kAlgorithm])
    {
        // the choice was stored over 7 entries: keep the same algorithm on the longer list
        const double index = std::round (st.norm[kAlgorithm] * (kAlgorithmsBefore06 - 1));
        st.norm[kAlgorithm] = toNormalized (kAlgorithm, index);
    }
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
