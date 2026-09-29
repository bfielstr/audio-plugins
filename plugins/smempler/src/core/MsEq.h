// Mid/side EQ after the built-in effects: a high-pass on the side signal, so the low end is mono below
// its cutoff, and the levels of mid and side. Also reports the mid and side peaks for the editor. Zero
// latency. The mid is never filtered, so folding to mono (L + R = 2 x mid) is untouched whatever the
// slope.
//
// The slope: 6 dB per octave (first order), 12 .. 96 dB (Butterworth, order 2 .. 16: flat above the
// cutoff, -3 dB at it), or Brickwall: a 16th-order Chebyshev (0.05 dB ripple above the cutoff, 0 dB at
// it) that falls about 40 dB within a tenth of the cutoff below it, 70 dB at 0.8 x and over 150 dB an
// octave down.
#pragma once

#include <algorithm>
#include <cmath>

namespace smempler {

class MsEq
{
public:
    enum Slope { k6 = 0, k12, k24, k36, k48, k60, k72, k84, k96, kBrickwall, kNumSlopes };
    static constexpr int kMaxSections = 8; // biquads (16th order)
    static constexpr int kBrickwallOrder = 16;
    static constexpr double kBrickwallRippleDb = 0.05;

    // The filter's order for a slope (Brickwall: kBrickwallOrder).
    static int order (int slope)
    {
        static constexpr int orders[kNumSlopes] = {1, 2, 4, 6, 8, 10, 12, 14, 16, kBrickwallOrder};
        return orders[std::clamp (slope, 0, kNumSlopes - 1)];
    }

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        reset ();
    }
    void reset ()
    {
        for (auto& s : st)
            s = {};
        hz = 0.0;
        curSlope = -1;
        sections = 0;
        midG = sideG = -1.0f;
    }

    // In place.
    void process (float* L, float* R, int n, double hpHz, int slope, double sideDb, double midDb)
    {
        update (hpHz, std::clamp (slope, 0, kNumSlopes - 1));
        const float midT = dbToGain (midDb), sideT = dbToGain (sideDb);
        if (midG < 0.0f)
        {
            midG = midT;
            sideG = sideT;
        }
        const float k = (float)(1.0 - std::exp (-1.0 / (0.01 * sr)));
        float pm = 0.0f, ps = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            midG += (midT - midG) * k;
            sideG += (sideT - sideG) * k;
            double m = 0.5 * ((double)L[i] + R[i]), s = 0.5 * ((double)L[i] - R[i]);
            for (int j = 0; j < sections; ++j)
                s = st[j].tick (c[j], s);
            m *= midG;
            s *= sideG;
            L[i] = (float)(m + s);
            R[i] = (float)(m - s);
            pm = std::max (pm, (float)std::fabs (m));
            ps = std::max (ps, (float)std::fabs (s));
        }
        midPeak = pm;
        sidePeak = ps;
    }

    // Off: only the levels, for the display.
    void measure (const float* L, const float* R, int n)
    {
        float pm = 0.0f, ps = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            pm = std::max (pm, 0.5f * std::fabs (L[i] + R[i]));
            ps = std::max (ps, 0.5f * std::fabs (L[i] - R[i]));
        }
        midPeak = pm;
        sidePeak = ps;
    }

    float midPeak = 0.0f, sidePeak = 0.0f;

    // The side high-pass's magnitude (dB) at f, for the display (the analog response the filter follows).
    static double responseDb (double f, double hpHz, int slope)
    {
        const double x = std::max (1e-9, f / std::max (1.0, hpHz));
        if (slope == kBrickwall)
        {
            // Chebyshev: |H|^2 = (1 + e^2) / (1 + e^2 T_N (1 / x)^2), unity far above the cutoff
            const double e2 = std::pow (10.0, kBrickwallRippleDb / 10.0) - 1.0, y = 1.0 / x;
            const double t = y <= 1.0 ? std::cos (kBrickwallOrder * std::acos (y)) : std::cosh (kBrickwallOrder * std::acosh (y));
            return 10.0 * std::log10 (std::max (1e-30, (1.0 + e2) / (1.0 + e2 * t * t)));
        }
        const int n = order (slope);
        // Butterworth: x^n / sqrt (1 + x^2n), in logs so a steep slope far below the cutoff stays finite
        const double lx = std::log10 (x);
        return 20.0 * n * lx - 10.0 * std::log10 (1.0 + std::pow (10.0, std::min (300.0, 2.0 * n * lx)));
    }

private:
    struct Coeffs
    {
        double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
    };
    struct State
    {
        double z1 = 0.0, z2 = 0.0;
        double tick (const Coeffs& c, double x) // transposed direct form II
        {
            const double y = c.b0 * x + z1;
            z1 = c.b1 * x - c.a1 * y + z2;
            z2 = c.b2 * x - c.a2 * y;
            return y;
        }
    };
    static float dbToGain (double db) { return (float)std::pow (10.0, db / 20.0); }

    // A second-order high-pass at f (Hz) with quality q (unity far above f).
    Coeffs highPass (double f, double q) const
    {
        const double w = 2.0 * M_PI * std::clamp (f, 1.0, 0.49 * sr) / sr, cw = std::cos (w), alpha = std::sin (w) / (2.0 * q), a0 = 1.0 + alpha;
        Coeffs r;
        r.b0 = (1.0 + cw) / 2.0 / a0;
        r.b1 = -(1.0 + cw) / a0;
        r.b2 = r.b0;
        r.a1 = -2.0 * cw / a0;
        r.a2 = (1.0 - alpha) / a0;
        return r;
    }

    void update (double target, int slope)
    {
        target = std::clamp (target, 10.0, 0.45 * sr);
        const double next = hz <= 0.0 ? target : hz * std::pow (target / hz, 0.3); // glide per block
        if (slope == curSlope && std::fabs (next - hz) < 1e-4 * hz)
            return;
        hz = next;
        const int before = sections;
        curSlope = slope;
        const int n = order (slope);
        if (n == 1)
        {
            // first order (bilinear): unity at Nyquist, zero at DC
            const double t = std::tan (M_PI * hz / sr), b0 = 1.0 / (1.0 + t);
            c[0] = {b0, -b0, 0.0, (t - 1.0) / (t + 1.0), 0.0};
            sections = 1;
        }
        else if (slope == kBrickwall)
        {
            // Chebyshev type I, low-pass prototype poles p = -sinh (v) sin (th) + j cosh (v) cos (th);
            // turned into a high-pass (s -> wc / s), each pole pair is a second-order high-pass at
            // hz / |p| with the pair's quality |p| / (2 |Re p|), unity far above the cutoff
            const double e = std::sqrt (std::pow (10.0, kBrickwallRippleDb / 10.0) - 1.0);
            const double v = std::asinh (1.0 / e) / n;
            sections = n / 2;
            for (int k = 0; k < sections; ++k)
            {
                const double th = M_PI * (2.0 * k + 1.0) / (2.0 * n);
                const double re = std::sinh (v) * std::sin (th), im = std::cosh (v) * std::cos (th), mag = std::hypot (re, im);
                c[k] = highPass (hz / mag, mag / (2.0 * re));
            }
        }
        else
        {
            // Butterworth: n / 2 second-order sections at the cutoff, qualities 1 / (2 sin ((2k + 1) pi / 2n))
            sections = n / 2;
            for (int k = 0; k < sections; ++k)
                c[k] = highPass (hz, 1.0 / (2.0 * std::sin (M_PI * (2.0 * k + 1.0) / (2.0 * n))));
        }
        // sections that were not running start from silence (not from what they held before)
        for (int k = before; k < sections; ++k)
            st[k] = {};
    }

    double sr = 48000.0, hz = 0.0;
    int curSlope = -1, sections = 0;
    Coeffs c[kMaxSections];
    State st[kMaxSections];
    float midG = -1.0f, sideG = -1.0f;
};

} // namespace smempler
