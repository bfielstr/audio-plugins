// Signals and measurements Detonatr's stage tests share.
#pragma once

#include <algorithm>
#include <cmath>
#include <random>
#include <vector>

namespace sig {

constexpr double kPi = 3.14159265358979323846;

inline std::vector<float> sine (double hz, double amp, double seconds, double sr, double phase = 0.0)
{
    std::vector<float> x ((size_t)(seconds * sr));
    for (size_t i = 0; i < x.size (); ++i)
        x[i] = (float)(amp * std::sin (2.0 * kPi * hz * (double)i / sr + phase));
    return x;
}

inline std::vector<float> noise (double rmsAmp, double seconds, double sr, unsigned seed = 1)
{
    std::mt19937 rng (seed);
    std::normal_distribution<float> d (0.0f, (float)rmsAmp);
    std::vector<float> x ((size_t)(seconds * sr));
    for (auto& v : x)
        v = d (rng);
    return x;
}

inline void add (std::vector<float>& a, const std::vector<float>& b)
{
    for (size_t i = 0; i < a.size () && i < b.size (); ++i)
        a[i] += b[i];
}

inline double rms (const std::vector<float>& x, size_t a, size_t b)
{
    b = std::min (b, x.size ());
    double s = 0.0;
    for (size_t i = a; i < b; ++i)
        s += (double)x[i] * x[i];
    return b > a ? std::sqrt (s / (double)(b - a)) : 0.0;
}

inline double peak (const std::vector<float>& x, size_t a = 0, size_t b = ~(size_t)0)
{
    b = std::min (b, x.size ());
    double p = 0.0;
    for (size_t i = a; i < b; ++i)
        p = std::max (p, (double)std::fabs (x[i]));
    return p;
}

inline double db (double v) { return 20.0 * std::log10 (std::max (v, 1e-20)); }

// the amplitude of a sine at hz in x[a .. b) (a Hann-windowed single-bin DFT)
inline double amplitude (const std::vector<float>& x, double hz, double sr, size_t a, size_t b)
{
    b = std::min (b, x.size ());
    double re = 0.0, im = 0.0, wsum = 0.0;
    const double n = (double)(b - a);
    for (size_t i = a; i < b; ++i)
    {
        const double t = (double)(i - a);
        const double w = 0.5 - 0.5 * std::cos (2.0 * kPi * t / n);
        const double ph = 2.0 * kPi * hz * (double)i / sr;
        re += w * x[i] * std::cos (ph);
        im += w * x[i] * std::sin (ph);
        wsum += w;
    }
    return 2.0 * std::sqrt (re * re + im * im) / wsum;
}

inline bool finite (const std::vector<float>& x)
{
    for (float v : x)
        if (!std::isfinite (v))
            return false;
    return true;
}

// runs a stage (process (l, r, n) in place) over a mono signal on both channels, in blocks
template <typename Stage>
std::vector<float> run (Stage& s, const std::vector<float>& x, int block = 256, std::vector<float>* right = nullptr)
{
    std::vector<float> out (x.size ()), l ((size_t)block), r ((size_t)block);
    if (right)
        right->assign (x.size (), 0.0f);
    for (size_t a = 0; a < x.size (); a += (size_t)block)
    {
        const int n = (int)std::min ((size_t)block, x.size () - a);
        std::copy (x.begin () + (ptrdiff_t)a, x.begin () + (ptrdiff_t)a + n, l.begin ());
        std::copy (x.begin () + (ptrdiff_t)a, x.begin () + (ptrdiff_t)a + n, r.begin ());
        s.process (l.data (), r.data (), n);
        std::copy (l.begin (), l.begin () + n, out.begin () + (ptrdiff_t)a);
        if (right)
            std::copy (r.begin (), r.begin () + n, right->begin () + (ptrdiff_t)a);
    }
    return out;
}

} // namespace sig
