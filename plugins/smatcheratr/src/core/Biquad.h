// RBJ biquads (double precision). A shelf or peak with gain -G is the exact inverse of the same
// filter with gain +G, which is what the colour stage relies on.
#pragma once

#include <algorithm>
#include <cmath>
#include <complex>

namespace smatcheratr {

struct BiquadCoeffs
{
    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
};

inline BiquadCoeffs lowShelf (double sr, double hz, double gainDb, double slope = 1.0)
{
    const double A = std::pow (10.0, gainDb / 40.0);
    const double w = 2.0 * M_PI * hz / sr, c = std::cos (w);
    const double alpha = std::sin (w) / 2.0 * std::sqrt ((A + 1.0 / A) * (1.0 / slope - 1.0) + 2.0);
    const double k = 2.0 * std::sqrt (A) * alpha;
    const double a0 = (A + 1.0) + (A - 1.0) * c + k;
    BiquadCoeffs r;
    r.b0 = A * ((A + 1.0) - (A - 1.0) * c + k) / a0;
    r.b1 = 2.0 * A * ((A - 1.0) - (A + 1.0) * c) / a0;
    r.b2 = A * ((A + 1.0) - (A - 1.0) * c - k) / a0;
    r.a1 = -2.0 * ((A - 1.0) + (A + 1.0) * c) / a0;
    r.a2 = ((A + 1.0) + (A - 1.0) * c - k) / a0;
    return r;
}

inline BiquadCoeffs peak (double sr, double hz, double gainDb, double q)
{
    const double A = std::pow (10.0, gainDb / 40.0);
    const double w = 2.0 * M_PI * hz / sr, c = std::cos (w);
    const double alpha = std::sin (w) / (2.0 * q);
    const double a0 = 1.0 + alpha / A;
    BiquadCoeffs r;
    r.b0 = (1.0 + alpha * A) / a0;
    r.b1 = -2.0 * c / a0;
    r.b2 = (1.0 - alpha * A) / a0;
    r.a1 = -2.0 * c / a0;
    r.a2 = (1.0 - alpha / A) / a0;
    return r;
}

inline BiquadCoeffs highPass (double sr, double hz, double q)
{
    const double w = 2.0 * M_PI * hz / sr, c = std::cos (w);
    const double alpha = std::sin (w) / (2.0 * q);
    const double a0 = 1.0 + alpha;
    BiquadCoeffs r;
    r.b0 = (1.0 + c) / 2.0 / a0;
    r.b1 = -(1.0 + c) / a0;
    r.b2 = r.b0;
    r.a1 = -2.0 * c / a0;
    r.a2 = (1.0 - alpha) / a0;
    return r;
}

inline double magnitudeDb (const BiquadCoeffs& c, double hz, double sr)
{
    const std::complex<double> z1 = std::polar (1.0, -2.0 * M_PI * hz / sr), z2 = z1 * z1;
    const std::complex<double> num = c.b0 + c.b1 * z1 + c.b2 * z2, den = 1.0 + c.a1 * z1 + c.a2 * z2;
    return 20.0 * std::log10 (std::max (1e-9, std::abs (num / den)));
}

struct Biquad
{
    BiquadCoeffs c;
    double z1 = 0.0, z2 = 0.0;
    void reset () { z1 = z2 = 0.0; }
    double process (double x) // transposed direct form II
    {
        const double y = c.b0 * x + z1;
        z1 = c.b1 * x - c.a1 * y + z2;
        z2 = c.b2 * x - c.a2 * y;
        return y;
    }
};

} // namespace smatcheratr
