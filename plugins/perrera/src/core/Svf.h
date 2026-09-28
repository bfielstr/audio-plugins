// State-variable filter (Cytomic / Simper form): one section gives 12 dB/oct low- and high-pass;
// two in series give 24 dB/oct. Resonance 0 is the Q where a low-pass and a high-pass at the
// same cutoff sum to a flat response (the Linkwitz-Riley condition), so Perrera's two filters can
// meet without a bump. Also the analytic response the editor draws.
#pragma once

#include <cmath>
#include <complex>

namespace perrera {

// Resonance 0 is the Q at which a low-pass and a high-pass meeting at one cutoff sum flat: 0.5 for one
// section (Linkwitz-Riley 2), 0.707 for two in series (Linkwitz-Riley 4). Up to 20x that.
inline double resonanceToQ (double res, bool slope24 = false)
{
    return (slope24 ? M_SQRT1_2 : 0.5) * std::pow (20.0, std::fmin (std::fmax (res, 0.0), 1.0));
}

struct SvfCoeffs
{
    double g = 0.0, k = 2.0, a1 = 0.0, a2 = 0.0, a3 = 0.0;
    void set (double hz, double q, double sr)
    {
        g = std::tan (M_PI * std::fmin (std::fmax (hz, 5.0), 0.49 * sr) / sr);
        k = 1.0 / std::fmax (q, 0.1);
        a1 = 1.0 / (1.0 + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }
};

struct Svf
{
    double ic1 = 0.0, ic2 = 0.0;
    void reset () { ic1 = ic2 = 0.0; }
    // returns low-pass and high-pass for one input sample
    inline void tick (const SvfCoeffs& c, double x, double& lp, double& hp)
    {
        const double v3 = x - ic2;
        const double v1 = c.a1 * ic1 + c.a2 * v3;
        const double v2 = ic2 + c.a2 * ic1 + c.a3 * v3;
        ic1 = 2.0 * v1 - ic1;
        ic2 = 2.0 * v2 - ic2;
        lp = v2;
        hp = x - c.k * v1 - v2;
    }
};

// Analytic responses of the analog prototypes at f for cutoff fc and quality q.
inline std::complex<double> lowPassResponse (double f, double fc, double q)
{
    const std::complex<double> s (0.0, f / fc);
    return 1.0 / (s * s + s / q + 1.0);
}
inline std::complex<double> highPassResponse (double f, double fc, double q)
{
    const std::complex<double> s (0.0, f / fc);
    return s * s / (s * s + s / q + 1.0);
}

} // namespace perrera
