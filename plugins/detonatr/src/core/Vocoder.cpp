#include "Vocoder.h"

#include <cmath>

namespace detonatr {

void Vocoder::prepare (double sampleRate, int)
{
    sr = sampleRate;
    att = dsp::coef (attackMs, sr);
    rel = dsp::coef (releaseMs, sr);
    norm = dsp::coef (60.0, sr);
    design ();
    reset ();
}

void Vocoder::reset ()
{
    for (auto& band : filters)
        for (auto& f : band)
            f.reset ();
    env.fill (0.0f);
    inPow = wetPow = 0.0;
}

void Vocoder::setBands (int n)
{
    n = std::clamp (n, 8, kMaxBands);
    if (n != numBands)
    {
        numBands = n;
        design ();
    }
}

void Vocoder::setRange (double lo, double hi)
{
    lo = std::max (10.0, lo);
    hi = std::max (lo * 1.5, hi);
    if (lo != lowHz || hi != highHz)
    {
        lowHz = lo;
        highHz = hi;
        design ();
    }
}

void Vocoder::setOrder (int sections)
{
    sections = std::clamp (sections, 1, kMaxOrder);
    if (sections != order)
    {
        order = sections;
        design ();
    }
}

void Vocoder::setAttack (double ms)
{
    attackMs = ms;
    att = dsp::coef (ms, sr);
}

void Vocoder::setRelease (double ms)
{
    releaseMs = ms;
    rel = dsp::coef (ms, sr);
}

void Vocoder::design ()
{
    const double hi = std::min (highHz, 0.45 * sr), lo = std::min (lowHz, hi / 1.5);
    const double span = std::log2 (hi / lo);
    const double bw = span / (double)(numBands - 1); // octaves between two bands
    const double w = std::pow (2.0, bw);
    const double q = std::sqrt (w) / (w - 1.0);
    // each section wider, so the cascade is 3 dB down where the next band's centre is half-way
    const double qs = q * std::sqrt (std::pow (2.0, 1.0 / order) - 1.0);
    for (int b = 0; b < numBands; ++b)
    {
        centres[(size_t)b] = lo * std::pow (2.0, bw * b);
        for (int s = 0; s < order; ++s)
            filters[(size_t)b][(size_t)s].setup (centres[(size_t)b], qs, sr);
    }
}

void Vocoder::process (float* l, float* r, int n)
{
    const int nb = numBands, ord = order;
    const float a = att, re = rel, nm = norm, mix = ratio, cap = kMaxBandGain;
    float k = (float)std::sqrt ((inPow + 1e-12) / (wetPow + 1e-24));
    for (int i = 0; i < n; ++i)
    {
        const float xl = l[i], xr = r[i];
        float wl = 0.0f, wr = 0.0f, rawL = 0.0f, rawR = 0.0f;
        for (int b = 0; b < nb; ++b)
        {
            auto& f = filters[(size_t)b];
            float bl = xl, br = xr;
            for (int s = 0; s < ord; ++s)
            {
                bl = f[(size_t)s].tick (bl, 0);
                br = f[(size_t)s].tick (br, 1);
            }
            float& e = env[(size_t)b];
            dsp::follow (e, std::max (std::fabs (bl), std::fabs (br)), a, re);
            const float g = std::min (k * e, cap);
            wl += g * bl;
            wr += g * br;
            rawL += e * bl;
            rawR += e * br;
        }
        inPow += ((double)xl * xl + (double)xr * xr - inPow) * nm;
        wetPow += ((double)rawL * rawL + (double)rawR * rawR - wetPow) * nm;
        k = (float)std::sqrt ((inPow + 1e-12) / (wetPow + 1e-24));
        l[i] = xl + (wl - xl) * mix;
        r[i] = xr + (wr - xr) * mix;
    }
    // denormals: the follower states settle on tiny values in silence
    for (int b = 0; b < nb; ++b)
        if (env[(size_t)b] < 1e-15f)
            env[(size_t)b] = 0.0f;
    if (inPow < 1e-30)
        inPow = 0.0;
    if (wetPow < 1e-30)
        wetPow = 0.0;
}

} // namespace detonatr
