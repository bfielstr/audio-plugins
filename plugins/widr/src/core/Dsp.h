// Building blocks of Widr's DSP: biquads, delay lines, all-passes, a micro pitch shifter and a
// guard against denormals. Allocation only in prepare().
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
#include <xmmintrin.h>
#define WIDR_SSE 1
#endif

namespace widr {

// Flushes denormals to zero while alive (the reverb and the filters decay towards them after a
// loud burst, and they are slow on x86).
class NoDenormals
{
public:
    NoDenormals ()
    {
#if defined(WIDR_SSE)
        old = _mm_getcsr ();
        _mm_setcsr (old | 0x8040); // FTZ | DAZ
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
#if defined(WIDR_SSE)
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

inline float dbToGain (double db) { return (float)std::pow (10.0, db / 20.0); }

// --- biquads (RBJ cookbook), transposed direct form II in double ------------------------------
struct BiquadCoeffs
{
    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;

    static double clampHz (double f, double sr) { return std::clamp (f, 5.0, 0.45 * sr); }
    static BiquadCoeffs lowPass (double f, double q, double sr)
    {
        const double w = 2.0 * M_PI * clampHz (f, sr) / sr, c = std::cos (w), al = std::sin (w) / (2.0 * q);
        return norm ((1.0 - c) * 0.5, 1.0 - c, (1.0 - c) * 0.5, 1.0 + al, -2.0 * c, 1.0 - al);
    }
    static BiquadCoeffs highPass (double f, double q, double sr)
    {
        const double w = 2.0 * M_PI * clampHz (f, sr) / sr, c = std::cos (w), al = std::sin (w) / (2.0 * q);
        return norm ((1.0 + c) * 0.5, -(1.0 + c), (1.0 + c) * 0.5, 1.0 + al, -2.0 * c, 1.0 - al);
    }
    static BiquadCoeffs peaking (double f, double q, double gainDb, double sr)
    {
        const double A = std::pow (10.0, gainDb / 40.0);
        const double w = 2.0 * M_PI * clampHz (f, sr) / sr, c = std::cos (w), al = std::sin (w) / (2.0 * q);
        return norm (1.0 + al * A, -2.0 * c, 1.0 - al * A, 1.0 + al / A, -2.0 * c, 1.0 - al / A);
    }
    static BiquadCoeffs highShelf (double f, double gainDb, double sr)
    {
        const double A = std::pow (10.0, gainDb / 40.0);
        const double w = 2.0 * M_PI * clampHz (f, sr) / sr, c = std::cos (w), s = std::sin (w);
        const double al = s / 2.0 * std::sqrt (2.0); // shelf slope 1
        const double k = 2.0 * std::sqrt (A) * al;
        return norm (A * ((A + 1.0) + (A - 1.0) * c + k), -2.0 * A * ((A - 1.0) + (A + 1.0) * c),
                     A * ((A + 1.0) + (A - 1.0) * c - k), (A + 1.0) - (A - 1.0) * c + k, 2.0 * ((A - 1.0) - (A + 1.0) * c),
                     (A + 1.0) - (A - 1.0) * c - k);
    }
    // band-pass with 0 dB at the centre
    static BiquadCoeffs bandPass (double f, double q, double sr)
    {
        const double w = 2.0 * M_PI * clampHz (f, sr) / sr, c = std::cos (w), al = std::sin (w) / (2.0 * q);
        return norm (al, 0.0, -al, 1.0 + al, -2.0 * c, 1.0 - al);
    }

private:
    static BiquadCoeffs norm (double b0, double b1, double b2, double a0, double a1, double a2)
    {
        return {b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0};
    }
};

struct Biquad
{
    double z1 = 0.0, z2 = 0.0;
    void reset () { z1 = z2 = 0.0; }
    inline double tick (const BiquadCoeffs& c, double x)
    {
        const double y = c.b0 * x + z1;
        z1 = c.b1 * x - c.a1 * y + z2;
        z2 = c.b2 * x - c.a2 * y;
        return y;
    }
};

// One-pole low-pass (6 dB/oct).
struct OnePole
{
    float s = 0.0f;
    void reset () { s = 0.0f; }
    static float coeff (double hz, double sr) { return (float)(1.0 - std::exp (-2.0 * M_PI * std::min (hz, 0.49 * sr) / sr)); }
    inline float lp (float a, float x) { return s += a * (x - s); }
};

// --- delay line (power-of-two ring) -------------------------------------------------------
class DelayLine
{
public:
    void prepare (int maxSamples)
    {
        int n = 1;
        while (n < maxSamples + 4)
            n <<= 1;
        buf.assign ((size_t)n, 0.0f);
        mask = n - 1;
        w = 0;
    }
    void reset () { std::fill (buf.begin (), buf.end (), 0.0f); }
    inline void push (float x)
    {
        buf[(size_t)w] = x;
        w = (w + 1) & mask;
    }
    // d samples ago (d >= 1 after a push: 1 = the sample just pushed)
    inline float tap (int d) const { return buf[(size_t)((w - d) & mask)]; }
    // fractional delay, linear interpolation (d >= 1)
    inline float at (double d) const
    {
        const int i = (int)d;
        const float f = (float)(d - i);
        const float a = tap (i), b = tap (i + 1);
        return a + (b - a) * f;
    }
    int capacity () const { return mask - 2; }

private:
    std::vector<float> buf;
    int mask = 0, w = 0;
};

// Schroeder all-pass: flat magnitude, smeared phase.
class Allpass
{
public:
    void prepare (int delaySamples, float gain)
    {
        d = std::max (1, delaySamples);
        g = gain;
        line.prepare (d);
    }
    void reset () { line.reset (); }
    inline float tick (float x)
    {
        const float z = line.tap (d);
        const float v = x + g * z;
        line.push (v);
        return z - g * v;
    }

private:
    DelayLine line;
    int d = 1;
    float g = 0.5f;
};

// A shift of a few cents: two taps on a delay line sweep through a window in opposite phase and
// crossfade (sin^2 / cos^2, so the power stays constant).
class MicroShift
{
public:
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        window = 0.035 * sr;
        line.prepare ((int)window + 8);
        reset ();
    }
    void reset ()
    {
        line.reset ();
        phase = 0.0;
    }
    // cents > 0 raises the pitch (set per block)
    void setCents (double cents)
    {
        const double ratio = std::pow (2.0, cents / 1200.0);
        inc = std::fabs (1.0 - ratio) / window;
        up = ratio > 1.0;
    }
    inline float tick (float x)
    {
        line.push (x);
        phase += inc;
        if (phase >= 1.0)
            phase -= 1.0;
        auto delayAt = [&] (double ph) { return 2.0 + (up ? (1.0 - ph) : ph) * window; };
        const double p2 = phase + 0.5 >= 1.0 ? phase - 0.5 : phase + 0.5;
        const float wa = (float)std::sin (M_PI * phase), wb = (float)std::sin (M_PI * p2);
        return line.at (delayAt (phase)) * wa * wa + line.at (delayAt (p2)) * wb * wb;
    }

private:
    DelayLine line;
    double sr = 48000.0, window = 1680.0, phase = 0.0, inc = 0.0;
    bool up = false;
};

} // namespace widr
