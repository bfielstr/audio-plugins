// Sweepr's DSP pieces, without any framework: the bell (a topology-preserving state-variable filter,
// stable however fast its coefficients move), one-pole shelves for the tape's pre- and de-emphasis, the
// LFO shapes and the tape's hysteresis-like saturator.
#pragma once

#include "BellShape.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>

namespace sweepr::dsp {

constexpr double kPi = 3.14159265358979323846;
constexpr double kLn2 = 0.69314718055994530942;
constexpr double kLn10 = 2.30258509299404568402;

// ---- the state-variable filter (Andrew Simper's trapezoidal SVF), its coefficients for a bell, a low-pass
// and the analog response the bell is drawn from

struct SvfCoefs
{
    double a1 = 1.0, a2 = 0.0, a3 = 0.0;
    double m1 = 0.0;                 // bell: the band-pass mix (x + m1 * bp)
    double k = 1.0;                  // the damping (1 / Q of the poles)
    // A bell at rate sr: the band's prototype (BellShape.h, where its shape is defined) mapped with the
    // bilinear transform prewarped at its centre. The SVF's band-pass is s / (s^2 + k s + 1), so
    // x + m1 bp with k the poles' damping and m1 = zeroDamp - poleDamp is the prototype.
    void bell (const bell::Prototype& p, double sr)
    {
        k = p.poleDamp;
        set (std::tan (kPi * std::clamp (p.centreHz, 1.0, 0.49 * sr) / sr));
        m1 = p.zeroDamp - p.poleDamp;
    }
    void bell (double fc, double sr, double q, double gainDb) { bell (bell::prototype (fc, q, gainDb), sr); }
    void lowpass (double fc, double sr, double q)
    {
        k = 1.0 / std::max (q, 0.01);
        set (std::tan (kPi * std::clamp (fc, 1.0, 0.49 * sr) / sr));
        m1 = 0.0;
    }
    void set (double g)
    {
        a1 = 1.0 / (1.0 + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }
};

struct Svf
{
    double ic1 = 0.0, ic2 = 0.0;
    void reset () { ic1 = ic2 = 0.0; }
    // returns the band-pass; lp: the low-pass
    inline double tick (double x, const SvfCoefs& c, double& lp)
    {
        const double v3 = x - ic2;
        const double v1 = c.a1 * ic1 + c.a2 * v3;
        const double v2 = ic2 + c.a2 * ic1 + c.a3 * v3;
        ic1 = 2.0 * v1 - ic1;
        ic2 = 2.0 * v2 - ic2;
        lp = v2;
        return v1;
    }
    inline double bell (double x, const SvfCoefs& c)
    {
        double lp;
        return x + c.m1 * tick (x, c, lp);
    }
    inline double lowpass (double x, const SvfCoefs& c)
    {
        double lp;
        tick (x, c, lp);
        return lp;
    }
    bool finite () const { return std::isfinite (ic1) && std::isfinite (ic2); }
};

// The analog bell at f (Hz) for a band at fc, Q, gain dB (BellShape.h; the display, the tests).
inline std::complex<double> bellResponse (double f, double fc, double q, double gainDb)
{
    return bell::response (bell::prototype (fc, q, gainDb), f);
}

// ---- a first-order TPT section: low-pass state, and the high shelf built on it
struct OnePole
{
    double s = 0.0;
    void reset () { s = 0.0; }
    // G = g / (1 + g), g = tan (pi fc / sr)
    inline double lp (double x, double G)
    {
        const double v = (x - s) * G;
        const double y = v + s;
        s = y + v;
        return y;
    }
};
inline double onePoleG (double fc, double sr)
{
    const double g = std::tan (kPi * std::clamp (fc, 0.1, 0.49 * sr) / sr);
    return g / (1.0 + g);
}

// ---- the LFO shapes: pos in cycles (any real; the whole part is the cycle, for the random shapes), the
// value -1 .. 1. Phase 0 is where a sine starts rising through 0.
inline double hashUnit (int64_t n, uint32_t seed)
{
    uint64_t x = (uint64_t)n * 0x9E3779B97F4A7C15ull ^ ((uint64_t)seed << 32 | seed);
    x ^= x >> 30;
    x *= 0xBF58476D1CE4E5B9ull;
    x ^= x >> 27;
    x *= 0x94D049BB133111EBull;
    x ^= x >> 31;
    return (double)(x >> 11) * (2.0 / 9007199254740992.0) - 1.0; // -1 .. 1
}

inline double lfoShape (int shape, double pos, uint32_t seed)
{
    const double whole = std::floor (pos);
    const double f = pos - whole;
    switch (shape)
    {
        case 0: return std::sin (2.0 * kPi * f);                                          // Sine
        case 1: return f < 0.25 ? 4.0 * f : (f < 0.75 ? 2.0 - 4.0 * f : 4.0 * f - 4.0); // Triangle
        case 2: return 2.0 * f - 1.0;                                                     // Saw Up
        case 3: return 1.0 - 2.0 * f;                                                     // Saw Down
        case 4: return f < 0.5 ? 1.0 : -1.0;                                              // Square
        case 5: return hashUnit ((int64_t)whole, seed);                                   // Random
        default:                                                                          // Smooth Random
        {
            const double a = hashUnit ((int64_t)whole, seed), b = hashUnit ((int64_t)whole + 1, seed);
            return a + (b - a) * 0.5 * (1.0 - std::cos (kPi * f));
        }
    }
}

// ---- the tape's curve: an asymmetric soft saturator (tanh with a bias: even harmonics, 0 at 0, slope ~1)
inline double tapeCurve (double h, double bias, double tanhBias) { return std::tanh (h + bias) - tanhBias; }
// its antiderivative (log cosh, written so it never overflows)
inline double tapeCurveIntegral (double h, double bias, double tanhBias)
{
    const double a = std::fabs (h + bias);
    return a + std::log1p (std::exp (-2.0 * a)) - kLn2 - h * tanhBias;
}
// The curve with first-order antiderivative anti-aliasing: the mean of the curve between the last input and
// this one (what the curve does to a straight line between them), so its harmonics beyond the oversampled
// Nyquist fold back far weaker. Half a sample of delay (at 4x: an eighth of an input sample).
inline double tapeCurveAa (double h, double last, double bias, double tanhBias)
{
    const double d = h - last;
    if (std::fabs (d) < 1e-6)
        return tapeCurve (0.5 * (h + last), bias, tanhBias);
    return (tapeCurveIntegral (h, bias, tanhBias) - tapeCurveIntegral (last, bias, tanhBias)) / d;
}

} // namespace sweepr::dsp
