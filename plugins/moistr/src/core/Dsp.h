// Moistr's DSP pieces: the crossover split (Linkwitz-Riley 4th order, from TPT state-variable filters), the
// anti-aliased soft clipper used by Drive and Grit, and the Glue compressor.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace moistr::dsp {

constexpr double kPi = 3.14159265358979323846;

// (0.18's band filters) Resonance (0 .. 1) to Q: 0.5 .. 12 on a log scale (0.15: about 0.8, 0.35: about 1.5).
inline double qOf (double res) { return 0.5 * std::pow (24.0, std::clamp (res, 0.0, 1.0)); }

// A filter's coefficients from g = tan (pi f / sr) and k = 1 / Q (the topology-preserving transform
// state-variable filter: its frequency can move every sample without clicks or instability).
struct SvfCoefs
{
    double k = 1.0, a1 = 1.0, a2 = 0.0, a3 = 0.0;
    void set (double g, double kk)
    {
        k = kk;
        a1 = 1.0 / (1.0 + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }
};

// One channel's state. tick () gives the low-, band- and high-pass outputs at once.
struct Svf
{
    double ic1 = 0.0, ic2 = 0.0;
    void reset () { ic1 = ic2 = 0.0; }
    struct Out
    {
        double lp, bp, hp;
    };
    inline Out tick (double x, const SvfCoefs& c)
    {
        const double v3 = x - ic2;
        const double v1 = c.a1 * ic1 + c.a2 * v3;
        const double v2 = ic2 + c.a2 * ic1 + c.a3 * v3;
        ic1 = 2.0 * v1 - ic1;
        ic2 = 2.0 * v2 - ic2;
        return {v2, v1, x - c.k * v1 - v2};
    }
};

// ---- the crossover split ------------------------------------------------------------------------------
// A Linkwitz-Riley 4th-order split (24 dB/oct) at one corner: each side is a 2nd-order Butterworth squared,
// low = 1 / (s^2 + sqrt2 s + 1)^2, high = s^4 / (...)^2. Both are -6 dB at the corner, and low + high is the
// 2nd-order all-pass (s^2 - sqrt2 s + 1) / (s^2 + sqrt2 s + 1): flat in level. The first SVF section is
// shared (one state gives both its low- and high-pass); every section of a corner, and its all-pass, use
// the same coefficients (k = sqrt2), so a corner needs one SvfCoefs.
constexpr double kSqrt2 = 1.41421356237309504880;

struct Lr4Split
{
    Svf first, lo, hi;
    void reset ()
    {
        first.reset ();
        lo.reset ();
        hi.reset ();
    }
    inline void tick (double x, const SvfCoefs& c, double& low, double& high)
    {
        const Svf::Out o = first.tick (x, c);
        low = lo.tick (o.lp, c).lp;
        high = hi.tick (o.hp, c).hp;
    }
};

// The all-pass an Lr4Split's two sides add up to: x - 2 k (band-pass).
struct Lr4Allpass
{
    Svf s;
    void reset () { s.reset (); }
    inline double tick (double x, const SvfCoefs& c) { return x - 2.0 * c.k * s.tick (x, c).bp; }
};

// The four-band split tree (one channel), corners c[0] < c[1] < c[2]:
//   x -> split 0 -> Low  -> all-pass 1 -> all-pass 2
//                -> rest -> split 1 -> Mid  -> all-pass 2
//                                   -> rest -> split 2 -> High, Air
// Every band goes through every corner once (as a split or as its all-pass), so the four add up to
// AP0 AP1 AP2 x: an all-pass of the input, flat in level. Three bands are the same tree with High and
// Air at the same gain (High + Air = AP2 of the rest).
struct Split4
{
    Lr4Split split[3];
    Lr4Allpass lowAp1, lowAp2, midAp2;
    void reset ()
    {
        for (auto& s : split)
            s.reset ();
        lowAp1.reset ();
        lowAp2.reset ();
        midAp2.reset ();
    }
    inline void tick (double x, const SvfCoefs* c, double* band)
    {
        double low, rest, mid, rest2;
        split[0].tick (x, c[0], low, rest);
        split[1].tick (rest, c[1], mid, rest2);
        split[2].tick (rest2, c[2], band[2], band[3]);
        band[0] = lowAp2.tick (lowAp1.tick (low, c[1]), c[2]);
        band[1] = midAp2.tick (mid, c[2]);
    }
};

// ---- the frequency shifter's Hilbert transformer ---------------------------------------------------------
// Two chains of four second-order allpasses whose outputs stay a quarter cycle apart over most of the band
// (Olli Niemitalo's coefficients; the same transformer as Ciphr's FreqShifter, ciphr/src/core/Dsp.h). With
// a complex oscillator, i cos (phase) + q sin (phase) is the single sideband: every frequency moved up by
// the oscillator's frequency (down for a negative one). `i` alone is the unshifted signal with the same
// phase response as the shifted one (so the two blend without comb filtering).
struct Hilbert
{
    // y[n] = c (x[n] + y[n-2]) - x[n-2]
    struct Stage
    {
        double x1 = 0, x2 = 0, y1 = 0, y2 = 0;
        inline double tick (double x, double c)
        {
            const double y = c * (x + y2) - x2;
            x2 = x1;
            x1 = x;
            y2 = y1;
            y1 = y;
            return y;
        }
    };
    Stage a[4], b[4];
    double aDelay = 0.0;
    void reset ()
    {
        for (auto& s : a)
            s = {};
        for (auto& s : b)
            s = {};
        aDelay = 0.0;
    }
    inline void tick (double x, double& i, double& q)
    {
        static constexpr double ca[4] = {0.6923878 * 0.6923878, 0.9360654322959 * 0.9360654322959,
                                         0.9882295226860 * 0.9882295226860, 0.9987488452737 * 0.9987488452737};
        static constexpr double cb[4] = {0.4021921162426 * 0.4021921162426, 0.8561710882420 * 0.8561710882420,
                                         0.9722909545651 * 0.9722909545651, 0.9952884791278 * 0.9952884791278};
        double u = x, v = x;
        for (int s = 0; s < 4; ++s)
            u = a[s].tick (u, ca[s]);
        for (int s = 0; s < 4; ++s)
            v = b[s].tick (v, cb[s]);
        i = aDelay; // (the first chain one sample later)
        aDelay = u;
        q = v;
    }
};

// tanh with first-order antiderivative anti-aliasing (the clipper's output is the mean of tanh over
// the step between two samples): far less aliasing than tanh itself without oversampling, and no
// latency (half a sample of delay, which only matters against the dry signal at very high frequencies).
struct AdaaTanh
{
    double x1 = 0.0, f1 = 0.0;
    void reset ()
    {
        x1 = 0.0;
        f1 = 0.0;
    }
    // log (cosh (x)), without overflow
    static double logCosh (double x)
    {
        const double a = std::fabs (x);
        return a + std::log1p (std::exp (-2.0 * a)) - 0.69314718055994530942;
    }
    inline double tick (double x)
    {
        const double f = logCosh (x);
        const double dx = x - x1;
        const double y = std::fabs (dx) > 1e-5 ? (f - f1) / dx : std::tanh (0.5 * (x + x1));
        x1 = x;
        f1 = f;
        return y;
    }
};

// A soft clipper for two channels: tanh (g x) / g, so quiet signals pass at their level and loud ones
// are rounded off. g follows the amount (0: passes untouched, the clipper not run at all).
struct Saturator
{
    AdaaTanh ch[2];
    double g = 1.0, gTarget = 1.0;
    bool active = false;
    double maxDb = 18.0; // the drive at amount 1
    void reset ()
    {
        ch[0].reset ();
        ch[1].reset ();
        g = gTarget;
        active = gTarget > 1.0;
    }
    void setAmount (double a) { gTarget = std::pow (10.0, std::clamp (a, 0.0, 1.0) * maxDb / 20.0); }
    // In place, n samples; the drive glides to its target over the block.
    void process (double* l, double* r, int n)
    {
        if (!active && gTarget <= 1.0)
        {
            g = 1.0;
            return;
        }
        if (!active)
        {
            ch[0].reset ();
            ch[1].reset ();
            active = true;
        }
        const double g0 = g, step = (gTarget - g0) / n;
        for (int i = 0; i < n; ++i)
        {
            const double gi = g0 + step * (i + 1);
            l[i] = ch[0].tick (gi * l[i]) / gi;
            r[i] = ch[1].tick (gi * r[i]) / gi;
        }
        g = gTarget;
        if (gTarget <= 1.0)
            active = false; // (at 1 it is the identity for quiet signals; switched off once there)
    }
};

// The Glue compressor: an RMS detector (10 ms) on both channels together, a soft knee (6 dB), a fixed
// attack (10 ms) and release (150 ms), and automatic makeup gain. Glue sets the threshold (-10 to
// -30 dB) and the ratio (1:1 to 4:1) together; at 0 it is off (not run).
class Glue
{
public:
    void prepare (double sr)
    {
        rate = sr;
        rms = 1.0 - std::exp (-1.0 / (0.010 * sr));
        reset ();
    }
    void reset ()
    {
        env = 0.0;
        grDb = 0.0;
        gain = makeupGain ();
    }
    void setAmount (double a)
    {
        amount = std::clamp (a, 0.0, 1.0);
        threshold = -10.0 - 20.0 * amount;
        ratio = 1.0 + 3.0 * amount;
        makeup = 0.5 * -threshold * (1.0 - 1.0 / ratio);
    }
    double amountNow () const { return amount; }
    double gainReductionDb () const { return grDb; }
    // In place, n samples (the gain is worked out at the block's start from the detector and glides to it).
    void process (double* l, double* r, int n)
    {
        if (amount <= 0.0)
        {
            grDb = 0.0;
            gain = 1.0;
            return;
        }
        // the gain for this block from the detector so far
        const double levelDb = 10.0 * std::log10 (env + 1e-30);
        const double over = levelDb - threshold, slope = 1.0 - 1.0 / ratio;
        double target = 0.0;
        if (over >= 0.5 * kKnee)
            target = over * slope;
        else if (over > -0.5 * kKnee)
            target = slope * (over + 0.5 * kKnee) * (over + 0.5 * kKnee) / (2.0 * kKnee);
        const double tau = target > grDb ? 0.010 : 0.150;
        grDb += (target - grDb) * (1.0 - std::exp (-(double)n / (tau * rate)));
        const double g1 = std::pow (10.0, (makeup - grDb) / 20.0), g0 = gain, step = (g1 - g0) / n;
        for (int i = 0; i < n; ++i)
        {
            env += (0.5 * (l[i] * l[i] + r[i] * r[i]) - env) * rms;
            const double gi = g0 + step * (i + 1);
            l[i] *= gi;
            r[i] *= gi;
        }
        gain = g1;
        if (!(env < 1e30))
            env = 0.0; // (never: a NaN or an overflow resets the detector)
    }

private:
    static constexpr double kKnee = 6.0;
    double makeupGain () const { return amount > 0.0 ? std::pow (10.0, makeup / 20.0) : 1.0; }
    double rate = 48000.0, rms = 0.002;
    double amount = 0.0, threshold = -10.0, ratio = 1.0, makeup = 0.0;
    double env = 0.0, grDb = 0.0, gain = 1.0;
};

} // namespace moistr::dsp
