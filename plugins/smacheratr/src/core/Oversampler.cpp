#include "Oversampler.h"

#include <algorithm>
#include <cmath>

namespace smacheratr {

namespace {
double besselI0 (double x)
{
    double sum = 1.0, term = 1.0;
    const double hx = x * 0.5;
    for (int k = 1; k < 64; ++k)
    {
        term *= (hx / k) * (hx / k);
        sum += term;
        if (term < 1e-14 * sum)
            break;
    }
    return sum;
}
} // namespace

void Halfband2x::Fir::resize (int taps)
{
    int n = 1;
    while (n < taps)
        n <<= 1;
    hist.assign ((size_t)n, 0.0f);
    mask = n - 1;
    pos = 0;
}

void Halfband2x::Fir::reset ()
{
    std::fill (hist.begin (), hist.end (), 0.0f);
    pos = 0;
}

float Halfband2x::Fir::run (const std::vector<float>& h) const
{
    float acc = 0.0f;
    int i = (pos - 1) & mask; // most recent sample
    for (float tap : h)
    {
        acc += tap * hist[(size_t)i];
        i = (i - 1) & mask;
    }
    return acc;
}

void Halfband2x::design (double passEdge, double attenDb, int maxTaps)
{
    // at the 2x rate the transition runs from passEdge/2 to (1 - passEdge)/2 around the cutoff 1/4
    const double df = std::max (0.005, 0.5 - passEdge);
    const double beta = attenDb > 50.0   ? 0.1102 * (attenDb - 8.7)
                        : attenDb >= 21.0 ? 0.5842 * std::pow (attenDb - 21.0, 0.4) + 0.07886 * (attenDb - 21.0)
                                          : 0.0;
    int n = (int)std::ceil ((attenDb - 8.0) / (2.285 * 2.0 * M_PI * df)) + 1;
    n = std::clamp (n, 5, maxTaps);
    n = 4 * ((n + 2) / 4) + 1;
    h.assign ((size_t)n, 0.0f);
    const int M = (n - 1) / 2;
    const double i0b = besselI0 (beta);
    double sum = 0.0;
    for (int m = 0; m < n; ++m)
    {
        const double t = m - M;
        const double sinc = t == 0.0 ? 1.0 : std::sin (M_PI * 0.5 * t) / (M_PI * 0.5 * t);
        const double r = 2.0 * m / (n - 1) - 1.0;
        const double w = besselI0 (beta * std::sqrt (std::max (0.0, 1.0 - r * r))) / i0b;
        h[(size_t)m] = (float)(0.5 * sinc * w);
        sum += h[(size_t)m];
    }
    for (auto& v : h)
        v = (float)(v / sum);
    upFir.resize (n);
    downFir.resize (n);
}

void Halfband2x::reset ()
{
    upFir.reset ();
    downFir.reset ();
}

void Halfband2x::up (const float* in, float* out, int n)
{
    for (int i = 0; i < n; ++i)
    {
        upFir.push (2.0f * in[i]);
        out[2 * i] = upFir.run (h);
        upFir.push (0.0f);
        out[2 * i + 1] = upFir.run (h);
    }
}

void Halfband2x::down (const float* in, float* out, int n)
{
    for (int i = 0; i < n; ++i)
    {
        downFir.push (in[2 * i]);
        out[i] = downFir.run (h);
        downFir.push (in[2 * i + 1]);
    }
}

void Oversampler::prepare (double sr, int maxBlock)
{
    const double pass = std::min (20000.0, 0.46 * sr);
    s1.design (pass / sr, 80.0, 257);
    s2.design (pass / (2.0 * sr), 80.0, 257);
    mid.assign ((size_t)std::max (1, maxBlock) * 2, 0.0f);
    reset ();
}

void Oversampler::reset ()
{
    s1.reset ();
    s2.reset ();
}

void Oversampler::up (const float* in, float* out, int n)
{
    s1.up (in, mid.data (), n);
    s2.up (mid.data (), out, 2 * n);
}

void Oversampler::down (const float* in, float* out, int n)
{
    s2.down (in, mid.data (), 2 * n);
    s1.down (mid.data (), out, n);
}

} // namespace smacheratr
