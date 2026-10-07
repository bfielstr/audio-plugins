// Smeezr's DSP pieces: the crossover's filters (topology-preserving state-variable filters, in double
// precision so the lowest crossovers stay exact at high sample rates).
//
// A crossover is a Linkwitz-Riley 4 split: a Butterworth 2 squared on each side. The first section is
// shared by both sides (a state-variable filter gives its low- and high-pass from the same state), then one
// more section on each side. Low + high of one split is the second-order all-pass with the same corner
// (Allpass), so a band that did not go through a split gets that all-pass instead and every band ends up
// with the same phase: the bands sum flat (Engine.h).
#pragma once

#include <algorithm>
#include <cmath>

namespace smeezr::dsp {

constexpr double kPi = 3.14159265358979323846;
constexpr double kButterworthK = 1.41421356237309504880; // 1 / Q of a Butterworth 2 section

struct SvfCoefs
{
    double k = kButterworthK, a1 = 1.0, a2 = 0.0, a3 = 0.0;
    void set (double fc, double sr, double kk = kButterworthK)
    {
        const double f = std::clamp (fc, 5.0, 0.45 * sr);
        const double g = std::tan (kPi * f / sr);
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
    bool finite () const { return std::isfinite (ic1) && std::isfinite (ic2); }
};

// One channel of a Linkwitz-Riley 4 split (all three sections share the corner's coefficients).
struct Lr4
{
    Svf first, low, high;
    void reset ()
    {
        first.reset ();
        low.reset ();
        high.reset ();
    }
    inline void tick (double x, const SvfCoefs& c, double& lo, double& hi)
    {
        const Svf::Out a = first.tick (x, c);
        lo = low.tick (a.lp, c).lp;
        hi = high.tick (a.hp, c).hp;
    }
    bool finite () const { return first.finite () && low.finite () && high.finite (); }
};

// The second-order all-pass a Linkwitz-Riley 4 split at the same corner sums to: x - 2 k bp.
struct Allpass
{
    Svf s;
    void reset () { s.reset (); }
    inline double tick (double x, const SvfCoefs& c) { return x - 2.0 * c.k * s.tick (x, c).bp; }
    bool finite () const { return s.finite (); }
};

} // namespace smeezr::dsp
