#include "Oversampler.h"

#include "pluginkit/Simd.h"

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
    hEven.clear ();
    hOdd.clear ();
    for (int m = 0; m < n; ++m)
        (m % 2 ? hOdd : hEven).push_back (h[(size_t)m]);
    hist = (n - 1) / 2;
    for (auto* b : {&upBuf, &evenBuf, &oddBuf})
        b->assign ((size_t)(hist + kChunk), 0.0f);
}

void Halfband2x::reset ()
{
    for (auto* b : {&upBuf, &evenBuf, &oddBuf})
        std::fill (b->begin (), b->end (), 0.0f);
}

void Halfband2x::up (const float* in, float* out, int n)
{
    for (int pos = 0; pos < n; pos += kChunk)
        upChunk (in + pos, out + 2 * pos, std::min (kChunk, n - pos));
}

void Halfband2x::down (const float* in, float* out, int n)
{
    for (int pos = 0; pos < n; pos += kChunk)
        downChunk (in + 2 * pos, out + pos, std::min (kChunk, n - pos));
}

void Halfband2x::upChunk (const float* in, float* out, int n)
{
    // u: the input (times 2: the zeros halve the level), hist samples back first. The FIR's output
    // after the input sample i is sum_m h[m] x[2i - m] over the zero-stuffed x: the even taps against
    // u[i], u[i - 1] ..; after the zero that follows it, the odd taps against the same.
    float* u = upBuf.data () + hist;
    for (int i = 0; i < n; ++i)
        u[i] = 2.0f * in[i];
    const int ne = (int)hEven.size (), no = (int)hOdd.size ();
    const float* he = hEven.data ();
    const float* ho = hOdd.data ();
    int i = 0;
    for (; i + 4 <= n; i += 4)
    {
        pk::F4 e = pk::F4::set1 (0.0f), o = pk::F4::set1 (0.0f);
        for (int m = 0; m < ne; ++m)
            e = e + pk::F4::set1 (he[m]) * pk::F4::load (u + i - m);
        for (int m = 0; m < no; ++m)
            o = o + pk::F4::set1 (ho[m]) * pk::F4::load (u + i - m);
        alignas (16) float ev[4], od[4];
        e.store (ev);
        o.store (od);
        for (int l = 0; l < 4; ++l)
        {
            out[2 * (i + l)] = ev[l];
            out[2 * (i + l) + 1] = od[l];
        }
    }
    for (; i < n; ++i)
    {
        float e = 0.0f, o = 0.0f;
        for (int m = 0; m < ne; ++m)
            e = e + he[m] * u[i - m];
        for (int m = 0; m < no; ++m)
            o = o + ho[m] * u[i - m];
        out[2 * i] = e;
        out[2 * i + 1] = o;
    }
    std::copy (u + n - hist, u + n, upBuf.data ()); // the history for the next chunk
}

void Halfband2x::downChunk (const float* in, float* out, int n)
{
    // out[i] = sum_m h[m] x[2i - m], m from 0 up: the even taps against the even samples xe[i - m/2],
    // the odd ones against the odd samples xo[i - (m+1)/2] (the one before this pair and further back)
    float* xe = evenBuf.data () + hist;
    float* xo = oddBuf.data () + hist;
    for (int i = 0; i < n; ++i)
    {
        xe[i] = in[2 * i];
        xo[i] = in[2 * i + 1];
    }
    const int taps = (int)h.size ();
    const float* hh = h.data ();
    int i = 0;
    for (; i + 4 <= n; i += 4)
    {
        pk::F4 acc = pk::F4::set1 (0.0f);
        for (int m = 0; m < taps; ++m)
        {
            const float* x = m % 2 ? xo + i - (m + 1) / 2 : xe + i - m / 2;
            acc = acc + pk::F4::set1 (hh[m]) * pk::F4::load (x);
        }
        acc.store (out + i);
    }
    for (; i < n; ++i)
    {
        float acc = 0.0f;
        for (int m = 0; m < taps; ++m)
            acc = acc + hh[m] * (m % 2 ? xo[i - (m + 1) / 2] : xe[i - m / 2]);
        out[i] = acc;
    }
    std::copy (xe + n - hist, xe + n, evenBuf.data ());
    std::copy (xo + n - hist, xo + n, oddBuf.data ());
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
