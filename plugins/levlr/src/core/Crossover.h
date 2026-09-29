// Levlr's crossovers: Linkwitz-Riley splits at 12, 24 or 48 dB/oct, built from Multidyn's trapezoidal
// SVF sections (multidyn/src/core/Crossover.h; the 24 dB/oct split here is its Lr4Split). Each split's
// two outputs add up to an all-pass of the input, and so does a matching all-pass (Allpass) at the
// same corner, so a tree of splits whose lower bands go through the all-passes of the splits above
// them sums to an all-pass: flat in level, shifted in phase. That phase shift is the sound: Levlr has
// no linear-phase mode.
//   12 dB/oct (LR2): one section with Q 0.5; low = its low-pass, high = its high-pass turned over (the
//                    two only sum flat with one of them inverted); the sum is a first-order all-pass.
//   24 dB/oct (LR4): two Butterworth sections (Q 0.707) per side; the sum is a second-order all-pass.
//   48 dB/oct (LR8): two 4th-order Butterworths per side (sections of Q 0.54 and 1.31); the sum is the
//                    fourth-order all-pass B4(-s)/B4(s).
// The first section is shared by both sides (an SVF gives its low- and high-pass from the same state).
#pragma once

#include "multidyn/src/core/Crossover.h"

#include <cmath>
#include <complex>

namespace levlr {

enum Slope { kSlope12 = 0, kSlope24, kSlope48, kNumSlopes };

struct SlopeDef
{
    int sections;       // SVF sections per side (the first one shared)
    float k[4];         // each section's damping (1/Q)
    float hiSign;       // the high side's polarity (LR2 is inverted so the sum is flat)
    int apSections;     // the all-pass's SVF sections
    float apK[2];       // and their damping
    bool firstOrderAp;  // LR2: the all-pass is (1-s)/(1+s), the low-pass minus the high-pass of a Q 0.5 section
};

inline const SlopeDef& slopeDef (int slope)
{
    static constexpr float kA = 1.847759065f, kB = 0.765366865f; // 2 cos (pi/8), 2 cos (3 pi/8): Butterworth 4
    static const SlopeDef defs[kNumSlopes] = {
        {1, {2.0f, 0, 0, 0}, -1.0f, 1, {2.0f, 0}, true},
        {2, {1.41421356f, 1.41421356f, 0, 0}, 1.0f, 1, {1.41421356f, 0}, false},
        {4, {kA, kB, kA, kB}, 1.0f, 2, {kA, kB}, false},
    };
    return defs[slope < 0 ? 0 : (slope >= kNumSlopes ? kNumSlopes - 1 : slope)];
}

// the prewarped corner of a crossover at fc (as multidyn::Svf2::setup clamps it)
inline float cornerG (double fc, double sr)
{
    const double f = std::fmin (std::fmax (fc, 10.0), 0.45 * sr);
    return (float)std::tan (M_PI * f / sr);
}

// retunes a section without recomputing the tangent (all sections of a crossover share one corner)
inline void tune (multidyn::Svf2& s, float g, float k)
{
    s.k = k;
    s.g = g;
    s.a1 = 1.0f / (1.0f + g * (g + k));
    s.a2 = g * s.a1;
    s.a3 = g * s.a2;
}

struct LrSplit
{
    multidyn::Svf2 first, lo[3], hi[3];
    int slope = kSlope24;

    void setup (float g, int sl)
    {
        slope = sl;
        const SlopeDef& d = slopeDef (sl);
        tune (first, g, d.k[0]);
        for (int i = 1; i < d.sections; ++i)
        {
            tune (lo[i - 1], g, d.k[i]);
            tune (hi[i - 1], g, d.k[i]);
        }
    }
    void reset ()
    {
        first.reset ();
        for (int i = 0; i < 3; ++i)
        {
            lo[i].reset ();
            hi[i].reset ();
        }
    }
    inline void tick (float x, int c, float& low, float& high)
    {
        const SlopeDef& d = slopeDef (slope);
        float l, b, h, dummy;
        first.tick (x, c, l, b, h);
        for (int i = 1; i < d.sections; ++i)
        {
            lo[i - 1].tick (l, c, l, b, dummy);
            hi[i - 1].tick (h, c, dummy, b, h);
        }
        low = l;
        high = d.hiSign * h;
    }
};

// The all-pass a split's two outputs add up to.
struct LrAllpass
{
    multidyn::Svf2 s[2];
    int slope = kSlope24;

    void setup (float g, int sl)
    {
        slope = sl;
        const SlopeDef& d = slopeDef (sl);
        for (int i = 0; i < d.apSections; ++i)
            tune (s[i], g, d.apK[i]);
    }
    void reset ()
    {
        s[0].reset ();
        s[1].reset ();
    }
    inline float tick (float x, int c)
    {
        const SlopeDef& d = slopeDef (slope);
        float l, b, h;
        if (d.firstOrderAp)
        {
            s[0].tick (x, c, l, b, h);
            return l - h;
        }
        for (int i = 0; i < d.apSections; ++i)
        {
            s[i].tick (x, c, l, b, h);
            x = x - 2.0f * s[i].k * b;
        }
        return x;
    }
};

// The same filters' responses at frequency f (for the display and the tests): the analog prototypes
// read at the bilinear transform's warped frequency, s = j tan (pi f / sr) / tan (pi fc / sr).
struct SplitResponse
{
    std::complex<double> low, high, allpass;
};

inline SplitResponse splitResponse (double fc, int slope, double f, double sr)
{
    const SlopeDef& d = slopeDef (slope);
    const double g = cornerG (fc, sr);
    const std::complex<double> s (0.0, std::tan (M_PI * std::fmin (f, 0.4999 * sr) / sr) / g);
    std::complex<double> lp (1.0, 0.0), hp (1.0, 0.0);
    for (int i = 0; i < d.sections; ++i)
    {
        const std::complex<double> den = s * s + (double)d.k[i] * s + 1.0;
        lp /= den;
        hp *= s * s / den;
    }
    std::complex<double> ap (1.0, 0.0);
    if (d.firstOrderAp)
        ap = (1.0 - s) / (1.0 + s);
    else
        for (int i = 0; i < d.apSections; ++i)
            ap *= (s * s - (double)d.apK[i] * s + 1.0) / (s * s + (double)d.apK[i] * s + 1.0);
    return {lp, (double)d.hiSign * hp, ap};
}

} // namespace levlr
