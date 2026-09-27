#include "Clip.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace stretchr {

TimeMap::TimeMap (const std::vector<StretchMarker>& markers, double scale) : m (markers), k (scale > 0.0 ? scale : 1.0)
{
    if (m.size () < 2)
        m = {{0.0, 0.0}, {1.0, 1.0}};
}

double TimeMap::srcAt (double out) const
{
    const double d = out / k;
    auto it = std::upper_bound (m.begin (), m.end (), d, [] (double v, const StretchMarker& s) { return v < s.dst; });
    size_t i = it == m.begin () ? 0 : (size_t)(it - m.begin ()) - 1;
    i = std::min (i, m.size () - 2);
    const auto& a = m[i];
    const auto& b = m[i + 1];
    return a.src + (d - a.dst) * (b.src - a.src) / (b.dst - a.dst);
}

double TimeMap::outAt (double src) const
{
    auto it = std::upper_bound (m.begin (), m.end (), src, [] (double v, const StretchMarker& s) { return v < s.src; });
    size_t i = it == m.begin () ? 0 : (size_t)(it - m.begin ()) - 1;
    i = std::min (i, m.size () - 2);
    const auto& a = m[i];
    const auto& b = m[i + 1];
    return k * (a.dst + (src - a.src) * (b.dst - a.dst) / (b.src - a.src));
}

double TimeMap::stretchAt (double out) const
{
    const double d = out / k;
    auto it = std::upper_bound (m.begin (), m.end (), d, [] (double v, const StretchMarker& s) { return v < s.dst; });
    size_t i = it == m.begin () ? 0 : (size_t)(it - m.begin ()) - 1;
    i = std::min (i, m.size () - 2);
    return k * (m[i + 1].dst - m[i].dst) / (m[i + 1].src - m[i].src);
}

std::vector<StretchMarker> identityMarkers (double len)
{
    len = std::max (len, 1e-3);
    return {{0.0, 0.0}, {len, len}};
}

void sanitizeMarkers (std::vector<StretchMarker>& v, double len)
{
    len = std::max (len, 1e-3);
    const double minSrc = std::min (0.002, len / 4.0);
    std::vector<StretchMarker> in;
    for (const auto& s : v)
        if (std::isfinite (s.src) && std::isfinite (s.dst) && s.src > minSrc && s.src < len - minSrc)
            in.push_back (s);
    std::sort (in.begin (), in.end (), [] (const StretchMarker& a, const StretchMarker& b) { return a.src < b.src; });
    double endDst = len;
    for (const auto& s : v)
        if (std::fabs (s.src - len) < 1e-9 && std::isfinite (s.dst) && s.dst > 0.0)
            endDst = s.dst;

    std::vector<StretchMarker> out {{0.0, 0.0}};
    for (const auto& s : in)
    {
        const auto& p = out.back ();
        if (s.src - p.src < minSrc)
            continue;
        StretchMarker c = s;
        const double gap = c.src - p.src;
        c.dst = std::clamp (c.dst, p.dst + gap * kMinSegmentStretch, p.dst + gap * kMaxSegmentStretch);
        out.push_back (c);
    }
    const auto& p = out.back ();
    const double gap = len - p.src;
    out.push_back ({len, std::clamp (endDst, p.dst + gap * kMinSegmentStretch, p.dst + gap * kMaxSegmentStretch)});
    v = std::move (out);
}

double pitchAt (const std::vector<PitchPoint>& p, double src)
{
    if (p.empty ())
        return 0.0;
    if (src <= p.front ().src)
        return p.front ().semis;
    if (src >= p.back ().src)
        return p.back ().semis;
    auto it = std::upper_bound (p.begin (), p.end (), src, [] (double v, const PitchPoint& q) { return v < q.src; });
    const auto& b = *it;
    const auto& a = *(it - 1);
    const double span = b.src - a.src;
    return span <= 0.0 ? b.semis : a.semis + (src - a.src) * (b.semis - a.semis) / span;
}

void sanitizePitch (std::vector<PitchPoint>& p, double len)
{
    std::vector<PitchPoint> out;
    for (auto q : p)
        if (std::isfinite (q.src) && std::isfinite (q.semis))
        {
            q.src = std::clamp (q.src, 0.0, std::max (0.0, len));
            q.semis = std::clamp (q.semis, -kMaxPitchEnvelope, kMaxPitchEnvelope);
            out.push_back (q);
        }
    std::stable_sort (out.begin (), out.end (), [] (const PitchPoint& a, const PitchPoint& b) { return a.src < b.src; });
    p = std::move (out);
}

//==============================================================================
// Serialisation

namespace {
constexpr uint32_t kClipMagic = 0x50494c43; // 'CLIP'
constexpr uint32_t kClipVersion = 1;

struct Writer
{
    std::vector<uint8_t>& o;
    void u32 (uint32_t v)
    {
        for (int i = 0; i < 4; ++i)
            o.push_back ((uint8_t)(v >> (8 * i)));
    }
    void f64 (double d)
    {
        uint64_t v;
        std::memcpy (&v, &d, 8);
        for (int i = 0; i < 8; ++i)
            o.push_back ((uint8_t)(v >> (8 * i)));
    }
    void f32 (float f)
    {
        uint32_t v;
        std::memcpy (&v, &f, 4);
        u32 (v);
    }
};

struct Reader
{
    const uint8_t* p;
    size_t n, i = 0;
    bool ok = true;
    bool need (size_t k)
    {
        if (!ok || n - i < k || i > n)
            ok = false;
        return ok;
    }
    uint32_t u32 ()
    {
        if (!need (4))
            return 0;
        uint32_t v = 0;
        for (int b = 0; b < 4; ++b)
            v |= (uint32_t)p[i++] << (8 * b);
        return v;
    }
    double f64 ()
    {
        if (!need (8))
            return 0.0;
        uint64_t v = 0;
        for (int b = 0; b < 8; ++b)
            v |= (uint64_t)p[i++] << (8 * b);
        double d;
        std::memcpy (&d, &v, 8);
        return d;
    }
    float f32 ()
    {
        const uint32_t v = u32 ();
        float f;
        std::memcpy (&f, &v, 4);
        return f;
    }
};
} // namespace

void writeClip (const Clip& c, std::vector<uint8_t>& out)
{
    Writer w {out};
    w.u32 (kClipMagic);
    w.u32 (kClipVersion);
    w.f64 (c.start);
    w.u32 ((uint32_t)c.name.size ());
    out.insert (out.end (), c.name.begin (), c.name.end ());
    w.u32 ((uint32_t)c.markers.size ());
    for (const auto& m : c.markers)
    {
        w.f64 (m.src);
        w.f64 (m.dst);
    }
    w.u32 ((uint32_t)c.pitch.size ());
    for (const auto& p : c.pitch)
    {
        w.f64 (p.src);
        w.f64 (p.semis);
    }
    if (c.empty ())
    {
        w.u32 (0);
        return;
    }
    const auto& a = *c.audio;
    const int nch = std::clamp (a.numChannels, 1, 2);
    // 24-bit PCM, scaled down first if the capture went over full scale.
    const float scale = std::max (1.0f, a.peakAbs);
    w.u32 (1);
    w.f64 (a.sampleRate);
    w.u32 ((uint32_t)nch);
    w.u32 ((uint32_t)a.length);
    w.f32 (scale);
    const size_t base = out.size ();
    out.resize (base + (size_t)a.length * (size_t)nch * 3);
    uint8_t* d = out.data () + base;
    const float q = 8388607.0f / scale;
    for (int i = 0; i < a.length; ++i)
        for (int ch = 0; ch < nch; ++ch)
        {
            const float x = a.ch[ch][(size_t)i];
            const int32_t v = (int32_t)std::lrint (std::clamp (x * q, -8388608.0f, 8388607.0f));
            *d++ = (uint8_t)(v & 0xff);
            *d++ = (uint8_t)((v >> 8) & 0xff);
            *d++ = (uint8_t)((v >> 16) & 0xff);
        }
}

bool readClip (const uint8_t* data, size_t size, Clip& c)
{
    Reader r {data, size};
    if (r.u32 () != kClipMagic)
        return false;
    const uint32_t version = r.u32 ();
    if (!r.ok || version < 1)
        return false;
    Clip out;
    out.start = r.f64 ();
    const uint32_t nameLen = r.u32 ();
    if (nameLen > 4096 || !r.need (nameLen))
        return false;
    out.name.assign ((const char*)data + r.i, nameLen);
    r.i += nameLen;
    const uint32_t nm = r.u32 ();
    if (nm > 100000)
        return false;
    for (uint32_t i = 0; i < nm && r.ok; ++i)
    {
        StretchMarker m;
        m.src = r.f64 ();
        m.dst = r.f64 ();
        out.markers.push_back (m);
    }
    const uint32_t np = r.u32 ();
    if (np > 100000)
        return false;
    for (uint32_t i = 0; i < np && r.ok; ++i)
    {
        PitchPoint p;
        p.src = r.f64 ();
        p.semis = r.f64 ();
        out.pitch.push_back (p);
    }
    const uint32_t hasAudio = r.u32 ();
    if (!r.ok)
        return false;
    if (hasAudio)
    {
        const double sr = r.f64 ();
        const uint32_t nch = r.u32 ();
        const uint32_t frames = r.u32 ();
        const float scale = r.f32 ();
        if (!r.ok || nch < 1 || nch > 2 || !(sr >= 1000.0 && sr <= 768000.0) || frames > 0x7fffffff ||
            !(scale >= 1.0f) || !r.need ((size_t)frames * nch * 3))
            return false;
        std::vector<float> ch[2];
        for (uint32_t k = 0; k < nch; ++k)
            ch[k].resize (frames);
        const uint8_t* d = data + r.i;
        const float q = scale / 8388608.0f;
        for (uint32_t i = 0; i < frames; ++i)
            for (uint32_t k = 0; k < nch; ++k)
            {
                int32_t v = (int32_t)((uint32_t)d[0] | ((uint32_t)d[1] << 8) | ((uint32_t)d[2] << 16));
                if (v & 0x800000)
                    v -= 0x1000000;
                ch[k][i] = (float)v * q;
                d += 3;
            }
        r.i += (size_t)frames * nch * 3;
        out.audio = SampleData::fromBuffers (std::move (ch[0]), std::move (ch[1]), sr, out.name);
        sanitizeMarkers (out.markers, out.srcLength ());
        sanitizePitch (out.pitch, out.srcLength ());
    }
    else
    {
        out.markers.clear ();
        out.pitch.clear ();
    }
    c = std::move (out);
    return true;
}

} // namespace stretchr
