// State-variable filter (Cytomic / Simper form, trapezoidal): the sections Para's slopes are built of
// (Slopes.h), and the first-order section. Also the analog responses of the sections.
#pragma once

#include <cmath>
#include <complex>

namespace para {

// First-order (6 dB) low- and high-pass, trapezoidal.
struct OnePole
{
    double s = 0.0;
    void reset () { s = 0.0; }
    // g = tan (pi * fc / sr)
    inline void tick (double g, double x, double& lp, double& hp)
    {
        const double v = (x - s) * g / (1.0 + g);
        lp = v + s;
        s = lp + v;
        hp = x - lp;
    }
};
inline double onePoleG (double hz, double sr) { return std::tan (M_PI * std::fmin (std::fmax (hz, 5.0), 0.49 * sr) / sr); }

inline std::complex<double> lowPass1Response (double f, double fc) { return 1.0 / (std::complex<double> (0.0, f / fc) + 1.0); }
inline std::complex<double> highPass1Response (double f, double fc)
{
    const std::complex<double> s (0.0, f / fc);
    return s / (s + 1.0);
}

struct SvfCoeffs
{
    double g = 0.0, k = 2.0, a1 = 0.0, a2 = 0.0, a3 = 0.0;
    void set (double hz, double q, double sr) { setG (std::tan (M_PI * std::fmin (std::fmax (hz, 5.0), 0.49 * sr) / sr), q); }
    // g = tan (pi * fc / sr)
    void setG (double gain, double q)
    {
        g = gain;
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
    // also the band-pass (the all-pass is x - 2 k bp, a bell x + m bp)
    inline void tick (const SvfCoeffs& c, double x, double& lp, double& bp, double& hp)
    {
        const double v3 = x - ic2;
        const double v1 = c.a1 * ic1 + c.a2 * v3;
        const double v2 = ic2 + c.a2 * ic1 + c.a3 * v3;
        ic1 = 2.0 * v1 - ic1;
        ic2 = 2.0 * v2 - ic2;
        lp = v2;
        bp = v1;
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

} // namespace para
