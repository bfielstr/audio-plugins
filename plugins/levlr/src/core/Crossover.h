// Levlr's crossovers: Linkwitz-Riley splits from 12 to 96 dB/oct (LR2 .. LR16, in 12 dB steps), built
// from Multidyn's trapezoidal SVF sections (multidyn/src/core/Crossover.h; the 24 dB/oct split here is its
// Lr4Split). A split of order 2N is a Butterworth of order N squared on each side: its two outputs add up
// to the all-pass BN(-s)/BN(s) of the input, and so does a matching all-pass (LrAllpass) at the same
// corner, so a tree of splits whose lower bands go through the all-passes of the splits above them sums
// to an all-pass: flat in level, shifted in phase. That phase shift is the sound (the steeper, the more
// it turns): Levlr has no linear-phase mode.
//   Each side: N second-order sections. A Butterworth pair of poles (damping 2 sin ((2i-1) pi / 2N)) is
//   squared, so each appears twice; with N odd, the squared first-order pole is one section of Q 0.5.
//   The high side is turned over when N is odd (LR2, LR6, ...): only then do the two sides sum flat.
//   The all-pass: one section per pole pair (s^2 - k s + 1) / (s^2 + k s + 1), and with N odd the
//   first-order (1 - s) / (1 + s) (the low-pass minus the high-pass of a Q 0.5 section).
// The first section is shared by both sides (an SVF gives its low- and high-pass from the same state).
#pragma once

#include "multidyn/src/core/Crossover.h"

#include <cmath>
#include <complex>

namespace levlr {

enum Slope { kSlope12 = 0, kSlope24, kSlope36, kSlope48, kSlope60, kSlope72, kSlope84, kSlope96, kNumSlopes };
constexpr int kMaxSections = 8; // per side, at 96 dB/oct

struct SlopeDef
{
    int sections = 0;          // SVF sections per side (the first one shared)
    float k[kMaxSections] {};  // each section's damping (1/Q)
    float hiSign = 1.0f;       // the high side's polarity (turned over for odd N, so the sum is flat)
    int apSections = 0;        // the all-pass's SVF sections
    float apK[kMaxSections / 2] {}; // and their damping
    bool firstOrderAp = false; // odd N: apK[0] is the first-order all-pass (a Q 0.5 section's low minus high)
};

inline SlopeDef makeSlope (int n) // n: the Butterworth order (the slope is 12 n dB/oct)
{
    SlopeDef d;
    d.hiSign = (n % 2) ? -1.0f : 1.0f;
    if (n % 2)
    {
        d.k[d.sections++] = 2.0f;
        d.apK[d.apSections++] = 2.0f;
        d.firstOrderAp = true;
    }
    for (int i = 1; i <= n / 2; ++i)
    {
        const float k = (float)(2.0 * std::sin ((2 * i - 1) * M_PI / (2.0 * n)));
        d.k[d.sections++] = k;
        d.k[d.sections++] = k;
        d.apK[d.apSections++] = k;
    }
    return d;
}

inline const SlopeDef& slopeDef (int slope)
{
    static const SlopeDef defs[kNumSlopes] = {makeSlope (1), makeSlope (2), makeSlope (3), makeSlope (4),
                                              makeSlope (5), makeSlope (6), makeSlope (7), makeSlope (8)};
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
    multidyn::Svf2 first, lo[kMaxSections - 1], hi[kMaxSections - 1];
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
        for (int i = 0; i < kMaxSections - 1; ++i)
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
    multidyn::Svf2 s[kMaxSections / 2];
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
        for (auto& x : s)
            x.reset ();
    }
    inline float tick (float x, int c)
    {
        const SlopeDef& d = slopeDef (slope);
        float l, b, h;
        int i = 0;
        if (d.firstOrderAp)
        {
            s[0].tick (x, c, l, b, h);
            x = l - h;
            i = 1;
        }
        for (; i < d.apSections; ++i)
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
    int i0 = 0;
    if (d.firstOrderAp)
    {
        ap = (1.0 - s) / (1.0 + s);
        i0 = 1;
    }
    for (int i = i0; i < d.apSections; ++i)
        ap *= (s * s - (double)d.apK[i] * s + 1.0) / (s * s + (double)d.apK[i] * s + 1.0);
    return {lp, (double)d.hiSign * hp, ap};
}

} // namespace levlr
