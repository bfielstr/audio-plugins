#include "Disperse.h"

#include "Variant.h" // (Rng)

#include <algorithm>
#include <cmath>
#include <complex>

namespace ciphr {

namespace {
constexpr double kPi = 3.14159265358979323846;
inline double smoothstep (double t) { return t * t * (3.0 - 2.0 * t); }

// the bands' Q: kFlatQ times the Q at which neighbours cross at -3 dB, narrowed by Width
double qFor (int bands, double width)
{
    const double r = std::pow (Disperse::kHighHz / Disperse::kLowHz, 1.0 / (double)(bands - 1));
    const double crossQ = 1.0 / (std::sqrt (r) - 1.0 / std::sqrt (r));
    return crossQ * Disperse::kFlatQ * std::pow (Disperse::kNarrowQ, 1.0 - std::clamp (width, 0.0, 1.0));
}
} // namespace

double Disperse::centreHz (int b, int bands)
{
    return kLowHz * std::pow (kHighHz / kLowHz, (double)b / (double)std::max (1, bands - 1));
}

void Disperse::order (int bands, int seedValue, int* rankOf, int* bandAt)
{
    bands = std::clamp (bands, kMinBands, kMaxBands);
    int at[kMaxBands];
    for (int i = 0; i < bands; ++i)
        at[i] = i;
    Rng rng (0x5EED5EEDull + 0x9E3779B1ull * (uint64_t)(uint32_t)seedValue);
    for (int i = bands - 1; i > 0; --i)
        std::swap (at[i], at[rng.below (i + 1)]);
    for (int j = 0; j < bands; ++j)
    {
        if (rankOf)
            rankOf[at[j]] = j;
        if (bandAt)
            bandAt[j] = at[j];
    }
}

double Disperse::revealStart (int rank, int bands)
{
    const double w = std::min (1.0, 2.0 / (double)bands);
    return bands > 1 ? (double)rank * (1.0 - w) / (double)(bands - 1) : 0.0;
}

double Disperse::revealEnd (int rank, int bands) { return revealStart (rank, bands) + std::min (1.0, 2.0 / (double)bands); }

double Disperse::revealGain (double a, int rank, int bands)
{
    const double s = revealStart (rank, bands), e = revealEnd (rank, bands);
    if (a <= s)
        return 0.0;
    if (a >= e)
        return 1.0;
    const double g = smoothstep ((a - s) / (e - s));
    return g * g;
}

void Disperse::prepare (double sampleRate)
{
    sr = sampleRate;
    mixSmooth = (float)(1.0 - std::exp (-1.0 / (0.02 * sr)));
    sliceSmooth = 1.0 - std::exp (-(double)kSlice / (0.03 * sr));
    fadeStep = (float)(1.0 / (0.01 * sr));
    // the normalization: the mean power of the bank's sum (every band full) from 100 Hz to 10 kHz, as the
    // filters are (the bilinear transform at this rate), for every band count and width step
    constexpr int kPoints = 96;
    std::complex<double> sz[kPoints];
    for (int i = 0; i < kPoints; ++i)
    {
        const double f = 100.0 * std::pow (100.0, (double)i / (kPoints - 1));
        const std::complex<double> z = std::polar (1.0, 2.0 * kPi * f / sr);
        sz[i] = (z - 1.0) / (z + 1.0);
    }
    for (int n = kMinBands; n <= kMaxBands; ++n)
    {
        double g[kMaxBands];
        for (int b = 0; b < n; ++b)
            g[b] = std::tan (kPi * std::min (centreHz (b, n), 0.47 * sr) / sr);
        for (int w = 0; w <= kWidthSteps; ++w)
        {
            const double kk = 1.0 / qFor (n, (double)w / kWidthSteps);
            double power = 0.0;
            for (int i = 0; i < kPoints; ++i)
            {
                std::complex<double> sum = 0.0;
                for (int b = 0; b < n; ++b)
                {
                    const std::complex<double> s = sz[i] / g[b];
                    sum += kk * s / (s * s + kk * s + 1.0);
                }
                power += std::norm (sum);
            }
            norm[n - kMinBands][w] = (float)(1.0 / std::sqrt (power / kPoints));
        }
    }
    configure ();
    reset ();
}

void Disperse::reset ()
{
    amount = amountT;
    width = widthT;
    nBands = bandsT;
    switching = false;
    fade = 1.0f;
    configure ();
    mixNow = onT ? mixT : 0.0f;
    active = onT;
    float t[kMaxBands];
    targets (t);
    for (int b = 0; b < kMaxBands; ++b)
    {
        gain[b] = b < nBands ? t[b] : 0.0f;
        step[b] = 0.0f;
        s1[0][b] = s1[1][b] = s2[0][b] = s2[1][b] = 0.0f;
    }
    pos = 0;
}

void Disperse::setAmount (double a) { amountT = std::clamp (a, 0.0, 1.0); }
void Disperse::setBands (int bands) { bandsT = std::clamp (bands, kMinBands, kMaxBands); }
void Disperse::setWidth (double w) { widthT = std::clamp (w, 0.0, 1.0); }
void Disperse::setMix (double m) { mixT = (float)std::clamp (m, 0.0, 1.0); }

void Disperse::setSeed (int s)
{
    seed = s;
    order (nBands, seed, rankOf);
}

void Disperse::configure ()
{
    order (nBands, seed, rankOf);
    updateCoefs ();
}

double Disperse::normFor (int bands, double w) const
{
    const double x = std::clamp (w, 0.0, 1.0) * kWidthSteps;
    const int i = std::min ((int)x, kWidthSteps - 1);
    const float* row = norm[std::clamp (bands, kMinBands, kMaxBands) - kMinBands];
    return row[i] + (x - i) * (row[i + 1] - row[i]);
}

void Disperse::updateCoefs ()
{
    const double q = qFor (nBands, width), kk = 1.0 / q;
    for (int b = 0; b < kMaxBands; ++b)
    {
        const double fc = std::min (centreHz (std::min (b, nBands - 1), nBands), 0.47 * sr);
        const double g = std::tan (kPi * fc / sr);
        const double c1 = 1.0 / (1.0 + g * (g + kk));
        a1[b] = (float)c1;
        a2[b] = (float)(g * c1);
        a3[b] = (float)(g * g * c1);
        k[b] = (float)kk;
    }
    level = (float)normFor (nBands, width);
}

void Disperse::targets (float* out) const
{
    for (int b = 0; b < nBands; ++b)
        out[b] = (float)revealGain (amount, rankOf[b], nBands);
}

void Disperse::process (float* l, float* r, int n)
{
    if (onT && !active)
    {
        // switched on from idle: the bank starts clean, at its settings (the mix fades it in)
        const float m = mixNow;
        reset ();
        mixNow = m;
        active = true;
    }
    int i = 0;
    while (i < n)
    {
        if (pos == 0)
        {
            // the slice's targets: the dial and the width glide, every band's gain glides (10 ms) to the
            // dial's and ramps sample by sample to it
            amount += (amountT - amount) * sliceSmooth;
            if (std::fabs (amountT - amount) < 1e-5)
                amount = amountT;
            if (width != widthT)
            {
                width += (widthT - width) * sliceSmooth;
                if (std::fabs (widthT - width) < 1e-4)
                    width = widthT;
                updateCoefs ();
            }
            float t[kMaxBands];
            targets (t);
            const float gs = (float)std::min (1.0, 3.0 * sliceSmooth);
            for (int b = 0; b < nBands; ++b)
            {
                float next = gain[b] + (t[b] - gain[b]) * gs;
                if (std::fabs (t[b] - next) < 1e-4f)
                    next = t[b];
                step[b] = (next - gain[b]) / (float)kSlice;
            }
        }
        const int m = std::min (n - i, kSlice - pos);
        float yl[kSlice], yr[kSlice];
        std::fill (yl, yl + m, 0.0f);
        std::fill (yr, yr + m, 0.0f);
        for (int b = 0; b < nBands; ++b)
        {
            float g = gain[b];
            const float st = step[b];
            if (g == 0.0f && st == 0.0f)
            {
                // silent and staying so: skipped (it starts from rest when it is raised, its gain from 0)
                s1[0][b] = s1[1][b] = s2[0][b] = s2[1][b] = 0.0f;
                continue;
            }
            const float c1 = a1[b], c2 = a2[b], c3 = a3[b], kk = k[b];
            float l1 = s1[0][b], l2 = s2[0][b], r1 = s1[1][b], r2 = s2[1][b];
            const float* xl = l + i;
            const float* xr = r + i;
            for (int j = 0; j < m; ++j)
            {
                g += st;
                const float gk = g * kk;
                {
                    const float v3 = xl[j] - l2;
                    const float v1 = c1 * l1 + c2 * v3;
                    const float v2 = l2 + c2 * l1 + c3 * v3;
                    l1 = 2.0f * v1 - l1;
                    l2 = 2.0f * v2 - l2;
                    yl[j] += gk * v1;
                }
                {
                    const float v3 = xr[j] - r2;
                    const float v1 = c1 * r1 + c2 * v3;
                    const float v2 = r2 + c2 * r1 + c3 * v3;
                    r1 = 2.0f * v1 - r1;
                    r2 = 2.0f * v2 - r2;
                    yr[j] += gk * v1;
                }
            }
            s1[0][b] = l1;
            s2[0][b] = l2;
            s1[1][b] = r1;
            s2[1][b] = r2;
            gain[b] = std::max (0.0f, g);
        }
        const float mixTarget = onT ? mixT : 0.0f;
        for (int j = 0; j < m; ++j)
        {
            mixNow += (mixTarget - mixNow) * mixSmooth;
            if (std::fabs (mixTarget - mixNow) < 1e-4f)
                mixNow = mixTarget; // (-80 dB: the last step is inaudible)
            if (switching || bandsT != nBands)
            {
                switching = true;
                fade -= fadeStep;
                if (fade <= 0.0f)
                {
                    // silent: the new bands, from rest, at their gains (then faded back in)
                    fade = 0.0f;
                    nBands = bandsT;
                    configure ();
                    float t[kMaxBands];
                    targets (t);
                    for (int b = 0; b < kMaxBands; ++b)
                    {
                        gain[b] = b < nBands ? t[b] : 0.0f;
                        step[b] = 0.0f;
                        s1[0][b] = s1[1][b] = s2[0][b] = s2[1][b] = 0.0f;
                    }
                    switching = false;
                    // (the rest of this chunk's bank output belongs to the old bands: muted)
                    for (int q = j; q < m; ++q)
                        yl[q] = yr[q] = 0.0f;
                }
            }
            else if (fade < 1.0f)
                fade = std::min (1.0f, fade + fadeStep);
            const float wet = mixNow * level * fade;
            l[i + j] = l[i + j] * (1.0f - mixNow) + yl[j] * wet;
            r[i + j] = r[i + j] * (1.0f - mixNow) + yr[j] * wet;
        }
        i += m;
        pos = (pos + m) % kSlice;
    }
    if (!onT && mixNow == 0.0f)
        active = false; // faded out: idle from the next block
}

} // namespace ciphr
