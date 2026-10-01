#include "Spike.h"

#include <cmath>

namespace detonatr {

void Spike::prepare (double sampleRate, int)
{
    sr = sampleRate;
    fastAtt = dsp::coef (0.1, sr);
    fastRel = dsp::coef (15.0, sr);
    gainAtt = dsp::coef (0.2, sr);
    setSharpness (sharpness);
    design ();
    updateDecay ();
    reset ();
}

void Spike::reset ()
{
    for (auto& band : filters)
        for (auto& f : band)
            f.reset ();
    for (int b = 0; b < kBands; ++b)
        for (int c = 0; c < 2; ++c)
            fast[(size_t)b][(size_t)c] = slow[(size_t)b][(size_t)c] = gainDb[(size_t)b][(size_t)c] = 0.0f;
}

void Spike::setDecay (double d)
{
    decay = std::clamp (d, 0.0, 10.0);
    updateDecay ();
}

void Spike::setDecayTilt (double t)
{
    tilt = std::clamp (t, -10.0, 10.0);
    updateDecay ();
}

void Spike::setSharpness (double s)
{
    sharpness = std::clamp (s, 0.0, 10.0);
    slowAtt = dsp::coef (40.0 / (1.0 + sharpness), sr);
}

void Spike::setRange (double lo, double hi)
{
    lo = std::max (10.0, lo);
    hi = std::max (lo * 2.0, hi);
    if (lo != lowHz || hi != highHz)
    {
        lowHz = lo;
        highHz = hi;
        design ();
    }
}

void Spike::updateDecay ()
{
    const double ms = 2.0 * std::pow (2.0, 0.7 * decay);
    for (int b = 0; b < kBands; ++b)
    {
        const double pos = 2.0 * b / (kBands - 1) - 1.0; // -1 lowest .. +1 highest
        gainRel[(size_t)b] = dsp::coef (ms * std::pow (2.0, 0.2 * tilt * pos), sr);
    }
}

void Spike::design ()
{
    const double hi = std::min (highHz, 0.45 * sr), lo = std::min (lowHz, hi / 2.0);
    const double bw = std::log2 (hi / lo) / (kBands - 1);
    const double w = std::pow (2.0, bw);
    const double q = std::sqrt (w) / (w - 1.0) * std::sqrt (std::sqrt (2.0) - 1.0); // two sections
    for (int b = 0; b < kBands; ++b)
    {
        centres[(size_t)b] = lo * std::pow (2.0, bw * b);
        for (auto& f : filters[(size_t)b])
            f.setup (centres[(size_t)b], q, sr);
    }
}

void Spike::process (float* l, float* r, int n)
{
    const float fa = fastAtt, fr = fastRel, sa = slowAtt, ga = gainAtt, thr = threshDb, depth = maxDb, lk = link;
    const float sign = boost ? 1.0f : -1.0f, m = mix, tr = trim;
    for (int i = 0; i < n; ++i)
    {
        const float xl = l[i], xr = r[i];
        float dl = 0.0f, dr = 0.0f;
        for (int b = 0; b < kBands; ++b)
        {
            auto& f = filters[(size_t)b];
            const float bl = f[1].tick (f[0].tick (xl, 0), 0);
            const float br = f[1].tick (f[0].tick (xr, 1), 1);
            auto& fe = fast[(size_t)b];
            auto& se = slow[(size_t)b];
            const float al = std::fabs (bl), ar = std::fabs (br);
            dsp::follow (fe[0], al, fa, fr);
            dsp::follow (fe[1], ar, fa, fr);
            dsp::follow (se[0], al, sa, fr);
            dsp::follow (se[1], ar, sa, fr);
            const float tL = dsp::ampToDb ((fe[0] + 1e-9f) / (se[0] + 1e-9f));
            const float tR = dsp::ampToDb ((fe[1] + 1e-9f) / (se[1] + 1e-9f));
            const float tMax = std::max (tL, tR);
            const float rel = gainRel[(size_t)b];
            auto& g = gainDb[(size_t)b];
            const float t[2] = {tL + (tMax - tL) * lk, tR + (tMax - tR) * lk};
            for (int c = 0; c < 2; ++c)
            {
                const float target = depth * std::clamp ((t[c] - thr) * (1.0f / 6.0f), 0.0f, 1.0f);
                g[(size_t)c] += (target - g[(size_t)c]) * (target > g[(size_t)c] ? ga : rel);
            }
            dl += (dsp::dbToAmp (sign * g[0]) - 1.0f) * bl;
            dr += (dsp::dbToAmp (sign * g[1]) - 1.0f) * br;
        }
        l[i] = (xl + m * dl) * tr;
        r[i] = (xr + m * dr) * tr;
    }
}

} // namespace detonatr
