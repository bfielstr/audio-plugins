// Linkwitz-Riley (LR4) three-way crossover built from trapezoidal SVFs. The low band gets an
// allpass at the upper crossover so that low + mid + high is an allpass of the input
// (flat magnitude, no comb filtering when bands are summed unprocessed).
#pragma once

#include <cmath>

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

} // namespace multidyn
