// sweepr_analyze's measurements, header-only so the core tests run them too: WAV reading, the delay between
// two recordings, the magnitude response of one against the other in 1/12-octave bands and how deep they
// null. Used to calibrate the bells (src/core/BellShape.h) against renders of a reference EQ.
#pragma once

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace sweepr::analyze {

constexpr double kPi = 3.14159265358979323846;

struct Audio
{
    double sampleRate = 0.0;
    std::vector<std::vector<float>> channels;
    size_t frames () const { return channels.empty () ? 0 : channels[0].size (); }
    // the channels' mean (mid), the signal the measurements use
    std::vector<float> mono () const
    {
        std::vector<float> m (frames (), 0.0f);
        for (const auto& c : channels)
            for (size_t i = 0; i < m.size (); ++i)
                m[i] += c[i] / (float)channels.size ();
        return m;
    }
};

// A WAV file: PCM 16, 24 or 32 bit or float 32 / 64 bit (also WAVE_FORMAT_EXTENSIBLE). error says why not.
inline bool readWav (const std::string& path, Audio& out, std::string& error)
{
    FILE* f = std::fopen (path.c_str (), "rb");
    if (!f)
    {
        error = "cannot open " + path;
        return false;
    }
    std::vector<unsigned char> d;
    unsigned char buf[65536];
    size_t n;
    while ((n = std::fread (buf, 1, sizeof (buf), f)) > 0)
        d.insert (d.end (), buf, buf + n);
    std::fclose (f);
    auto u16 = [&] (size_t p) { return (uint32_t)d[p] | (uint32_t)d[p + 1] << 8; };
    auto u32 = [&] (size_t p) { return u16 (p) | u16 (p + 2) << 16; };
    if (d.size () < 12 || std::memcmp (d.data (), "RIFF", 4) != 0 || std::memcmp (d.data () + 8, "WAVE", 4) != 0)
    {
        error = path + ": not a WAV file";
        return false;
    }
    uint32_t format = 0, chans = 0, rate = 0, bits = 0;
    size_t data = 0, dataLen = 0;
    for (size_t p = 12; p + 8 <= d.size ();)
    {
        const uint32_t len = u32 (p + 4);
        if (std::memcmp (d.data () + p, "fmt ", 4) == 0 && p + 24 <= d.size ())
        {
            format = u16 (p + 8);
            chans = u16 (p + 10);
            rate = u32 (p + 12);
            bits = u16 (p + 22);
            if (format == 0xFFFE && len >= 26 && p + 34 <= d.size ())
                format = u16 (p + 32); // (the sub-format's first two bytes)
        }
        else if (std::memcmp (d.data () + p, "data", 4) == 0)
        {
            data = p + 8;
            dataLen = std::min<size_t> (len, d.size () - data);
        }
        p += 8 + len + (len & 1);
    }
    const bool pcm = format == 1 && (bits == 16 || bits == 24 || bits == 32), flt = format == 3 && (bits == 32 || bits == 64);
    if (!data || chans == 0 || rate == 0 || (!pcm && !flt))
    {
        error = path + ": unsupported WAV (PCM 16 / 24 / 32 or float 32 / 64 only)";
        return false;
    }
    const size_t bytes = bits / 8, frames = dataLen / (bytes * chans);
    out.sampleRate = rate;
    out.channels.assign (chans, std::vector<float> (frames));
    for (size_t i = 0; i < frames; ++i)
        for (uint32_t c = 0; c < chans; ++c)
        {
            const size_t p = data + (i * chans + c) * bytes;
            double v = 0.0;
            if (flt && bits == 32)
            {
                float x;
                std::memcpy (&x, d.data () + p, 4);
                v = x;
            }
            else if (flt)
                std::memcpy (&v, d.data () + p, 8);
            else if (bits == 16)
                v = (int16_t)u16 (p) / 32768.0;
            else if (bits == 24)
                v = (double)((int32_t)(u16 (p) << 8 | (uint32_t)d[p + 2] << 24) >> 8) / 8388608.0;
            else
                v = (int32_t)u32 (p) / 2147483648.0;
            out.channels[c][i] = (float)v;
        }
    return true;
}

// in-place radix-2 FFT (size a power of two)
inline void fft (std::vector<std::complex<double>>& a, bool inverse = false)
{
    const size_t n = a.size ();
    for (size_t i = 1, j = 0; i < n; ++i)
    {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j)
            std::swap (a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1)
    {
        const double ang = (inverse ? 2.0 : -2.0) * kPi / (double)len;
        const std::complex<double> wl (std::cos (ang), std::sin (ang));
        for (size_t i = 0; i < n; i += len)
        {
            std::complex<double> w (1.0, 0.0);
            for (size_t k = 0; k < len / 2; ++k)
            {
                const auto u = a[i + k], v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
                w *= wl;
            }
        }
    }
}

// How many samples b lags a (negative: b is ahead), from the cross-correlation's peak within +-maxLag.
inline long delayOf (const std::vector<float>& a, const std::vector<float>& b, long maxLag = 48000)
{
    const size_t len = std::min<size_t> (std::min (a.size (), b.size ()), (size_t)1 << 20);
    size_t n = 1;
    while (n < 2 * len)
        n <<= 1;
    std::vector<std::complex<double>> fa (n), fb (n);
    for (size_t i = 0; i < len; ++i)
    {
        fa[i] = a[i];
        fb[i] = b[i];
    }
    fft (fa);
    fft (fb);
    for (size_t i = 0; i < n; ++i)
        fa[i] = std::conj (fa[i]) * fb[i];
    fft (fa, true);
    long best = 0;
    double peak = -1.0;
    for (long lag = -maxLag; lag <= maxLag; ++lag)
    {
        const double v = fa[(size_t)((lag + (long)n) % (long)n)].real ();
        if (v > peak)
        {
            peak = v;
            best = lag;
        }
    }
    return best;
}

struct Band
{
    double centreHz = 0.0;
    double db = 0.0;        // b's level against a's in the band
    double coherence = 0.0; // 0 .. 1: how much of b is a linear response to a there (low: noise, distortion, movement)
};

// b's magnitude response against a (b delayed by `delay` against a) in 1/12-octave bands from 20 Hz to
// 20 kHz: Welch averages (Hann, 65536 points, half overlapping) of the cross and auto spectra, each band's
// gain |sum S_ab| / sum S_aa (the bins between its edges, the nearest bin where a band is narrower than one).
inline std::vector<Band> response (const std::vector<float>& a, const std::vector<float>& b, long delay, double sampleRate)
{
    constexpr size_t kN = 65536, kHop = kN / 2;
    const size_t a0 = delay < 0 ? (size_t)-delay : 0, b0 = delay > 0 ? (size_t)delay : 0;
    const size_t len = std::min (a.size () - std::min (a.size (), a0), b.size () - std::min (b.size (), b0));
    std::vector<double> saa (kN / 2 + 1, 0.0), sbb (kN / 2 + 1, 0.0);
    std::vector<std::complex<double>> sab (kN / 2 + 1, 0.0);
    std::vector<double> win (kN);
    for (size_t i = 0; i < kN; ++i)
        win[i] = 0.5 - 0.5 * std::cos (2.0 * kPi * (double)i / (double)kN);
    std::vector<std::complex<double>> fa (kN), fb (kN);
    for (size_t s = 0; s + kN <= len || (s == 0 && len > 0); s += kHop)
    {
        for (size_t i = 0; i < kN; ++i)
        {
            const bool in = s + i < len;
            fa[i] = in ? win[i] * a[a0 + s + i] : 0.0;
            fb[i] = in ? win[i] * b[b0 + s + i] : 0.0;
        }
        fft (fa);
        fft (fb);
        for (size_t k = 0; k <= kN / 2; ++k)
        {
            saa[k] += std::norm (fa[k]);
            sbb[k] += std::norm (fb[k]);
            sab[k] += std::conj (fa[k]) * fb[k];
        }
        if (s + kN > len)
            break;
    }
    std::vector<Band> out;
    const double binHz = sampleRate / (double)kN;
    for (int k = -120; k <= 120; ++k)
    {
        const double fc = 1000.0 * std::pow (2.0, k / 12.0);
        if (fc < 20.0 || fc > std::min (20000.0, 0.48 * sampleRate))
            continue;
        const double lo = fc * std::pow (2.0, -1.0 / 24.0), hi = fc * std::pow (2.0, 1.0 / 24.0);
        size_t k0 = (size_t)std::ceil (lo / binHz), k1 = (size_t)std::floor (hi / binHz);
        if (k1 < k0)
            k0 = k1 = (size_t)std::lround (fc / binHz);
        double pa = 0.0, pb = 0.0;
        std::complex<double> c = 0.0;
        for (size_t i = k0; i <= std::min (k1, kN / 2); ++i)
        {
            pa += saa[i];
            pb += sbb[i];
            c += sab[i];
        }
        Band band;
        band.centreHz = fc;
        band.db = 20.0 * std::log10 (std::max (1e-30, std::abs (c)) / std::max (1e-30, pa));
        band.coherence = pa > 0.0 && pb > 0.0 ? std::norm (c) / (pa * pb) : 0.0;
        out.push_back (band);
    }
    return out;
}

struct Null
{
    double raw = 0.0;     // dB: the residual b - a (aligned) against b
    double matched = 0.0; // dB: the same with a scaled by the gain that nulls best
    double gain = 1.0;    // that gain
};

inline Null nullDepth (const std::vector<float>& a, const std::vector<float>& b, long delay)
{
    const size_t a0 = delay < 0 ? (size_t)-delay : 0, b0 = delay > 0 ? (size_t)delay : 0;
    const size_t len = std::min (a.size () - std::min (a.size (), a0), b.size () - std::min (b.size (), b0));
    double ab = 0.0, aa = 0.0, bb = 0.0;
    for (size_t i = 0; i < len; ++i)
    {
        const double x = a[a0 + i], y = b[b0 + i];
        ab += x * y;
        aa += x * x;
        bb += y * y;
    }
    Null n;
    n.gain = aa > 0.0 ? ab / aa : 1.0;
    auto db = [] (double v) { return 10.0 * std::log10 (std::max (v, 1e-30)); };
    n.raw = db ((bb - 2.0 * ab + aa) / std::max (bb, 1e-30));
    n.matched = db ((bb - 2.0 * n.gain * ab + n.gain * n.gain * aa) / std::max (bb, 1e-30));
    return n;
}

} // namespace sweepr::analyze
