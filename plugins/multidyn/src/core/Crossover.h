// Multidyn's crossovers, built from trapezoidal SVFs. Every slope splits a signal into two parts that
// add up to an all-pass of it, and a lower band goes through the all-passes of the splits above it,
// so the bands of the tree sum to an all-pass of the input: flat in level (no comb filtering when
// they are summed unprocessed), shifted in phase (the steeper, the more).
//   6 dB/oct: a first-order complementary split (low = 1 / (1 + s), high = s / (1 + s)): the two parts
//     add up to the input itself, so the "all-pass" for the lower bands is nothing at all.
//   12 .. 96 dB/oct: Linkwitz-Riley 2 .. 16, as in Levlr (levlr/src/core/Crossover.h, the same design):
//     a split of order 2N is a Butterworth of order N squared on each side. A Butterworth pair of
//     poles (damping 2 sin ((2i-1) pi / 2N)) is squared, so each appears twice; with N odd the squared
//     first-order pole is one section of Q 0.5. The high side is turned over when N is odd (only
//     then do the two sides sum flat). The all-pass: one section per pole pair (s^2 - k s + 1) /
//     (s^2 + k s + 1), and with N odd the first-order (1 - s) / (1 + s) (the low-pass minus the
//     high-pass of a Q 0.5 section).
//   Brickwall: Linkwitz-Riley 32 (a Butterworth of order 16 squared, 192 dB/oct): the same kind of
//     split, so it still sums flat and adds no latency. The SVF sections stay well behaved in float
//     down to the lowest crossover at high sample rates (the tests check it).
// The first section is shared by both sides (an SVF gives its low- and high-pass from the same state).
// The 24 dB/oct split is the Lr4Split below (the one Multidyn always had), section for section.
#pragma once

#include <cmath>
#include <complex>

namespace multidyn {

struct Svf2
{
    float g = 0.1f, k = 1.41421356f, a1 = 0, a2 = 0, a3 = 0;
    float ic1[2] {}, ic2[2] {};

    void setup (float fc, float sr)
    {
        fc = std::fmin (std::fmax (fc, 10.0f), 0.45f * sr);
        g = std::tan ((float)M_PI * fc / sr);
        a1 = 1.0f / (1.0f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }
    void reset ()
    {
        ic1[0] = ic1[1] = ic2[0] = ic2[1] = 0.0f;
    }
    // One sample; returns the low-pass, band-pass and high-pass outputs.
    inline void tick (float v0, int c, float& lp, float& bp, float& hp)
    {
        const float v3 = v0 - ic2[c];
        const float v1 = a1 * ic1[c] + a2 * v3;
        const float v2 = ic2[c] + a2 * ic1[c] + a3 * v3;
        ic1[c] = 2.0f * v1 - ic1[c];
        ic2[c] = 2.0f * v2 - ic2[c];
        lp = v2;
        bp = v1;
        hp = v0 - k * v1 - v2;
    }
};

// Fourth-order Linkwitz-Riley split: two cascaded Butterworth sections per output.
struct Lr4Split
{
    Svf2 lp1, lp2, hp1, hp2;

    void setup (float fc, float sr)
    {
        lp1.setup (fc, sr);
        lp2.setup (fc, sr);
        hp1.setup (fc, sr);
        hp2.setup (fc, sr);
    }
    void reset ()
    {
        lp1.reset ();
        lp2.reset ();
        hp1.reset ();
        hp2.reset ();
    }
    inline void tick (float x, int c, float& low, float& high)
    {
        float l, b, h;
        lp1.tick (x, c, l, b, h);
        lp2.tick (l, c, low, b, h);
        hp1.tick (x, c, l, b, h);
        hp2.tick (h, c, l, b, high);
    }
};

// Second-order allpass with the same corner as an LR4 split (their sum is this allpass).
struct Allpass2
{
    Svf2 svf;
    void setup (float fc, float sr) { svf.setup (fc, sr); }
    void reset () { svf.reset (); }
    inline float tick (float x, int c)
    {
        float l, b, h;
        svf.tick (x, c, l, b, h);
        return x - 2.0f * svf.k * b;
    }
};

// ---- every slope (kXoverSlope) ------------------------------------------------------------------------

enum XoverSlope
{
    kXover6 = 0,
    kXover12,
    kXover24,
    kXover36,
    kXover48,
    kXover60,
    kXover72,
    kXover84,
    kXover96,
    kXoverBrickwall, // Linkwitz-Riley 32
    kNumXoverSlopes
};
constexpr int kXoverMaxSections = 16; // per side, at the brickwall

struct XoverDef
{
    bool firstOrder = false;       // 6 dB: a one-pole complementary split, no all-pass
    int sections = 0;              // SVF sections per side (the first one shared)
    float k[kXoverMaxSections] {}; // each section's damping (1/Q)
    float hiSign = 1.0f;           // the high side's polarity (turned over for odd N, so the sum is flat)
    int apSections = 0;            // the all-pass's SVF sections
    float apK[kXoverMaxSections / 2] {};
    bool firstOrderAp = false; // odd N: apK[0] is the first-order all-pass (a Q 0.5 section's low minus high)
};

// n: the Butterworth order of each side's square root (a Linkwitz-Riley 2n split, 12 n dB/oct)
inline XoverDef makeXoverDef (int n)
{
    XoverDef d;
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

inline XoverDef makeFirstOrderDef ()
{
    XoverDef d;
    d.firstOrder = true;
    return d;
}

inline const XoverDef& xoverDef (int slope)
{
    static const XoverDef defs[kNumXoverSlopes] = {makeFirstOrderDef (), makeXoverDef (1), makeXoverDef (2), makeXoverDef (3), makeXoverDef (4),
                                                   makeXoverDef (5),     makeXoverDef (6), makeXoverDef (7), makeXoverDef (8), makeXoverDef (16)};
    return defs[slope < 0 ? 0 : (slope >= kNumXoverSlopes ? kNumXoverSlopes - 1 : slope)];
}

// the prewarped corner of a crossover at fc (clamped as Svf2::setup clamps it)
inline float xoverG (double fc, double sr)
{
    const double f = std::fmin (std::fmax (fc, 10.0), 0.45 * sr);
    return (float)std::tan (M_PI * f / sr);
}

// retunes a section without recomputing the tangent (all sections of a crossover share one corner)
inline void tuneSvf (Svf2& s, float g, float k)
{
    s.k = k;
    s.g = g;
    s.a1 = 1.0f / (1.0f + g * (g + k));
    s.a2 = g * s.a1;
    s.a3 = g * s.a2;
}

struct XoverSplit
{
    Svf2 first, lo[kXoverMaxSections - 1], hi[kXoverMaxSections - 1];
    float g1 = 0.0f;       // 6 dB: the one-pole's g / (1 + g)
    float z1[2] {};        // and its state
    int slope = kXover24;

    void setup (float g, int sl)
    {
        slope = sl;
        const XoverDef& d = xoverDef (sl);
        g1 = g / (1.0f + g);
        if (d.firstOrder)
            return;
        tuneSvf (first, g, d.k[0]);
        for (int i = 1; i < d.sections; ++i)
        {
            tuneSvf (lo[i - 1], g, d.k[i]);
            tuneSvf (hi[i - 1], g, d.k[i]);
        }
    }
    void reset ()
    {
        first.reset ();
        for (int i = 0; i < kXoverMaxSections - 1; ++i)
        {
            lo[i].reset ();
            hi[i].reset ();
        }
        z1[0] = z1[1] = 0.0f;
    }
    inline void tick (float x, int c, float& low, float& high)
    {
        const XoverDef& d = xoverDef (slope);
        if (d.firstOrder)
        {
            // trapezoidal one-pole: the low-pass, and the rest of the input as the high side
            const float v = (x - z1[c]) * g1;
            const float lp = v + z1[c];
            z1[c] = lp + v;
            low = lp;
            high = x - lp;
            return;
        }
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

// The all-pass a split's two outputs add up to (the identity at 6 dB).
struct XoverAllpass
{
    Svf2 s[kXoverMaxSections / 2];
    int slope = kXover24;

    void setup (float g, int sl)
    {
        slope = sl;
        const XoverDef& d = xoverDef (sl);
        for (int i = 0; i < d.apSections; ++i)
            tuneSvf (s[i], g, d.apK[i]);
    }
    void reset ()
    {
        for (auto& x : s)
            x.reset ();
    }
    inline float tick (float x, int c)
    {
        const XoverDef& d = xoverDef (slope);
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

// The same filters' responses at frequency f (for the tests): the analog prototypes read at the
// bilinear transform's warped frequency, s = j tan (pi f / sr) / tan (pi fc / sr).
struct XoverResponse
{
    std::complex<double> low, high, allpass;
};

inline XoverResponse xoverResponse (double fc, int slope, double f, double sr)
{
    const XoverDef& d = xoverDef (slope);
    const double g = xoverG (fc, sr);
    const std::complex<double> s (0.0, std::tan (M_PI * std::fmin (f, 0.4999 * sr) / sr) / g);
    if (d.firstOrder)
        return {1.0 / (1.0 + s), s / (1.0 + s), 1.0};
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

} // namespace multidyn
