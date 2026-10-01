// Small DSP pieces Detonatr's stages share: fast dB conversions, envelope coefficients, a band-pass
// biquad, a fixed delay and a 4-point Hermite read.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
#include <xmmintrin.h>
#define DETONATR_SSE 1
#endif

namespace detonatr::dsp {

constexpr double kPi = 3.14159265358979323846;

// Flushes denormals to zero while alive (as Gently and Levlr do): the filters decay towards them
// after the audio stops, and they are slow on x86.
class NoDenormals
{
public:
    NoDenormals ()
    {
#if defined(DETONATR_SSE)
        old = _mm_getcsr ();
        _mm_setcsr ((unsigned int)(old | 0x8040)); // FTZ | DAZ
#elif defined(__aarch64__) && !defined(_MSC_VER)
        uint64_t fpcr;
        asm volatile ("mrs %0, fpcr" : "=r"(fpcr));
        old = fpcr;
        fpcr |= (uint64_t)1 << 24; // FZ
        asm volatile ("msr fpcr, %0" : : "r"(fpcr));
#endif
    }
    ~NoDenormals ()
    {
#if defined(DETONATR_SSE)
        _mm_setcsr ((unsigned int)old);
#elif defined(__aarch64__) && !defined(_MSC_VER)
        asm volatile ("msr fpcr, %0" : : "r"(old));
#endif
    }
    NoDenormals (const NoDenormals&) = delete;
    NoDenormals& operator= (const NoDenormals&) = delete;

private:
    uint64_t old = 0;
};

// log2 to about 1e-4 (a few thousandths of a dB), for envelopes in dB
inline float fastLog2 (float x)
{
    x = std::max (x, 1e-30f);
    uint32_t bits;
    std::memcpy (&bits, &x, 4);
    const float e = (float)((int)((bits >> 23) & 255) - 127);
    bits = (bits & 0x007FFFFFu) | 0x3F800000u; // the mantissa, 1 .. 2
    float m;
    std::memcpy (&m, &bits, 4);
    // log2 (m) = 2 atanh (t) / ln 2 with t = (m - 1) / (m + 1) in 0 .. 1/3: the series to t^7
    const float t = (m - 1.0f) / (m + 1.0f), t2 = t * t;
    return e + 2.8853901f * t * (1.0f + t2 * (0.33333333f + t2 * (0.2f + t2 * 0.14285714f)));
}

// 2^x to about 1e-4 relative, for x in about -120 .. 120
inline float fastExp2 (float x)
{
    x = std::clamp (x, -126.0f, 126.0f);
    const float fl = std::floor (x);
    const float f = x - fl;
    const float p = 1.0f + f * (0.69314718f + f * (0.24022650f + f * (0.055504109f + f * (0.0096181291f + f * 0.0013333558f))));
    const int32_t e = (int32_t)fl + 127;
    const uint32_t bits = (uint32_t)e << 23;
    float s;
    std::memcpy (&s, &bits, 4);
    return s * p;
}

constexpr float kLog2ToDb = 6.0205999f; // 20 log10 (2)
inline float ampToDb (float a) { return kLog2ToDb * fastLog2 (a); }
inline float powToDb (float p) { return 0.5f * kLog2ToDb * fastLog2 (p); }
inline float dbToAmp (float db) { return fastExp2 (db / kLog2ToDb); }

// one-pole coefficient for a time constant in ms (the part of the way covered per sample)
inline float coef (double ms, double sr)
{
    const double n = std::max (1e-3, ms) * 0.001 * sr;
    return (float)(1.0 - std::exp (-1.0 / n));
}

// an attack / release follower: rises with `att`, falls with `rel`
inline void follow (float& env, float x, float att, float rel) { env += (x - env) * (x > env ? att : rel); }

// RBJ band-pass (0 dB at the centre), transposed direct form II, two channels
struct BandPass
{
    float b0 = 0, b2 = 0, a1 = 0, a2 = 0;
    float s1[2] {}, s2[2] {};
    void setup (double fc, double q, double sr)
    {
        fc = std::clamp (fc, 5.0, 0.45 * sr);
        const double w = 2.0 * kPi * fc / sr, alpha = std::sin (w) / (2.0 * std::max (0.05, q)), a0 = 1.0 + alpha;
        b0 = (float)(alpha / a0);
        b2 = (float)(-alpha / a0);
        a1 = (float)(-2.0 * std::cos (w) / a0);
        a2 = (float)((1.0 - alpha) / a0);
    }
    void reset () { s1[0] = s1[1] = s2[0] = s2[1] = 0.0f; }
    inline float tick (float x, int c)
    {
        const float y = b0 * x + s1[c];
        s1[c] = -a1 * y + s2[c];
        s2[c] = b2 * x - a2 * y;
        return y;
    }
};

// a fixed delay of a few channels' samples (0 samples: passes through)
struct Delay
{
    std::vector<float> buf[2];
    int len = 0, pos = 0;
    void prepare (int samples)
    {
        len = std::max (0, samples);
        for (auto& b : buf)
            b.assign ((size_t)std::max (1, len), 0.0f);
        pos = 0;
    }
    void reset ()
    {
        for (auto& b : buf)
            std::fill (b.begin (), b.end (), 0.0f);
        pos = 0;
    }
    // one sample of both channels, in place
    inline void tick (float& a, float& b)
    {
        if (len == 0)
            return;
        const float x = buf[0][(size_t)pos], y = buf[1][(size_t)pos];
        buf[0][(size_t)pos] = a;
        buf[1][(size_t)pos] = b;
        a = x;
        b = y;
        if (++pos >= len)
            pos = 0;
    }
    // in place on both channels
    void process (float* l, float* r, int n)
    {
        if (len == 0)
            return;
        for (int i = 0; i < n; ++i)
        {
            const float a = buf[0][(size_t)pos], b = buf[1][(size_t)pos];
            buf[0][(size_t)pos] = l[i];
            buf[1][(size_t)pos] = r[i];
            l[i] = a;
            r[i] = b;
            if (++pos >= len)
                pos = 0;
        }
    }
};

// 4-point Hermite (Catmull-Rom) interpolation between y0 and y1 at t (0 .. 1); ym1, y2 the neighbours
inline float hermite (float ym1, float y0, float y1, float y2, float t)
{
    const float c1 = 0.5f * (y1 - ym1);
    const float c2 = ym1 - 2.5f * y0 + 2.0f * y1 - 0.5f * y2;
    const float c3 = 0.5f * (y2 - ym1) + 1.5f * (y0 - y1);
    return ((c3 * t + c2) * t + c1) * t + y0;
}

} // namespace detonatr::dsp
