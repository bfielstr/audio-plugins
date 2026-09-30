#include "State.h"

#include "base/source/fstreamer.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace detonatr {

using namespace Steinberg;

namespace {
constexpr int32 kMagic = 0x44544E52;    // 'DTNR'
constexpr int32 kMbOttDefaults = 2; // 2: the Multiband stage has Live's OTT gain staging baked in
static_assert (kStateVersion == kMbOttDefaults);
constexpr int32 kRecMagic = 0x52454353; // 'RECS'
constexpr int32 kMaxFrames = 192000 * 20;

// A channel as 16 bits scaled to its own peak (the peak first, as a float): recordings are textures
// for the vocoder, and this keeps a project's size down.
bool writeChannel (IBStreamer& s, const std::vector<float>& x, int32 frames)
{
    float peak = 0.0f;
    for (int32 i = 0; i < frames; ++i)
        peak = std::max (peak, std::fabs (x[(size_t)i]));
    if (!s.writeFloat (peak))
        return false;
    std::vector<int16> q ((size_t)frames);
    const float k = peak > 0.0f ? 32767.0f / peak : 0.0f;
    for (int32 i = 0; i < frames; ++i)
        q[(size_t)i] = (int16)std::lround (std::clamp (x[(size_t)i] * k, -32767.0f, 32767.0f));
    const TSize bytes = (TSize)frames * 2;
    return s.writeRaw (q.data (), bytes) == bytes;
}

bool readChannel (IBStreamer& s, std::vector<float>& x, int32 frames)
{
    float peak = 0.0f;
    if (!s.readFloat (peak) || !std::isfinite (peak))
        return false;
    std::vector<int16> q ((size_t)frames);
    const TSize bytes = (TSize)frames * 2;
    if (s.readRaw (q.data (), bytes) != bytes)
        return false;
    x.resize ((size_t)frames);
    const float k = peak / 32767.0f;
    for (int32 i = 0; i < frames; ++i)
        x[(size_t)i] = q[(size_t)i] * k;
    return true;
}
} // namespace

bool writeState (IBStream* stream, const State& st, int32 version)
{
    const int32 kVersion = version;
    IBStreamer s (stream, kLittleEndian);
    int32 present = 0;
    for (uint32 id = 0; id < kNumParams; ++id)
        present += st.has[id] ? 1 : 0;
    bool ok = s.writeInt32 (kMagic) && s.writeInt32 (kVersion) && s.writeInt32 (present);
    for (uint32 id = 0; ok && id < kNumParams; ++id)
        if (st.has[id])
            ok = s.writeInt32u (id) && s.writeDouble (st.norm[id]);
    ok = ok && s.writeInt32 (kRecMagic) && s.writeInt32 (kCarrierSlots);
    for (int slot = 0; ok && slot < kCarrierSlots; ++slot)
    {
        const auto& r = st.recordings[(size_t)slot];
        const Carrier* c = r.audio.get ();
        const bool has = c && c->frames > 0 && (int32)c->ch[0].size () >= c->frames;
        ok = s.writeInt32 (has ? 1 : 0);
        if (!ok || !has)
            continue;
        const int32 frames = std::min (c->frames, kMaxFrames);
        const bool stereo = (int32)c->ch[1].size () >= frames && c->ch[1] != c->ch[0];
        ok = s.writeStr8 (r.name.c_str ()) && s.writeInt32 (frames) && s.writeDouble (c->sampleRate) && s.writeInt32 (stereo ? 2 : 1) &&
             writeChannel (s, c->ch[0], frames) && (!stereo || writeChannel (s, c->ch[1], frames));
    }
    return ok;
}

bool readState (IBStream* stream, State& st, bool withRecordings)
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
    // the Multiband stage's baked gains were the old ones: the difference moves into its gain controls
    // (multidyn::migrateOldBakedNorm; a missing one was at 0 dB, its default then and now)
    if (version < kMbOttDefaults)
        for (uint32 id = 0; id < kNumParams; ++id)
        {
            const int64_t md = mdIdOf (id);
            if (md >= 0 && multidyn::oldBakedShiftDb ((uint32_t)md) != 0.0)
            {
                st.norm[id] = multidyn::migrateOldBakedNorm ((uint32_t)md, st.norm[id]);
                st.has[id] = true;
            }
        }
    st.hasRecordings = false;
    for (auto& r : st.recordings)
        r = {};
    if (!withRecordings)
        return true;

    // the recordings (a stream without them, or with a damaged section, keeps the parameters)
    int32 recMagic = 0, slots = 0;
    if (!s.readInt32 (recMagic) || recMagic != kRecMagic || !s.readInt32 (slots) || slots < 0 || slots > 64)
        return true;
    State::Recording got[kCarrierSlots];
    for (int32 slot = 0; slot < slots; ++slot)
    {
        int32 has = 0;
        if (!s.readInt32 (has))
            return true;
        if (!has)
            continue;
        char8* name = s.readStr8 ();
        const std::string nm = name ? name : "";
        delete[] name;
        int32 frames = 0, channels = 0;
        double rate = 0.0;
        if (!s.readInt32 (frames) || frames <= 0 || frames > kMaxFrames || !s.readDouble (rate) || !(rate > 1000.0 && rate < 1e6) ||
            !s.readInt32 (channels) || channels < 1 || channels > 2)
            return true;
        auto c = std::make_shared<Carrier> ();
        if (!readChannel (s, c->ch[0], frames))
            return true;
        if (channels == 2)
        {
            if (!readChannel (s, c->ch[1], frames))
                return true;
        }
        else
            c->ch[1] = c->ch[0];
        c->frames = frames;
        c->sampleRate = rate;
        if (slot < kCarrierSlots)
            got[slot] = {c, nm};
    }
    for (int i = 0; i < kCarrierSlots; ++i)
        st.recordings[(size_t)i] = got[i];
    st.hasRecordings = true;
    return true;
}

} // namespace detonatr
