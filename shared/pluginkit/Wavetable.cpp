#include "pluginkit/Wavetable.h"

#include <algorithm>
#include <cmath>

namespace pk::wavetable {

double detectPeriod (const float* x, size_t n, double sampleRate)
{
    if (!x || sampleRate <= 0)
        return 0;
    const auto minTau = (size_t)std::max (2.0, std::floor (sampleRate / 2000.0));
    const auto maxTau = (size_t)std::ceil (sampleRate / 30.0);
    if (n < maxTau * 2 + 64)
        return 0;
    const size_t w = std::min<size_t> (8192, n - maxTau - 2);
    // the loudest stretch of w + maxTau samples (in steps of a quarter of it)
    const size_t span = w + maxTau + 2, step = std::max<size_t> (1, span / 4);
    size_t from = 0;
    double best = -1;
    for (size_t s = 0; s + span <= n; s += step)
    {
        double e = 0;
        for (size_t i = s; i < s + span; i += 4)
            e += (double)x[i] * x[i];
        if (e > best)
        {
            best = e;
            from = s;
        }
    }
    if (best <= 1e-12)
        return 0;
    const float* a = x + from;
    // the difference function and its cumulative mean normalized form (YIN)
    std::vector<double> d (maxTau + 2, 0.0), c (maxTau + 2, 1.0);
    double sum = 0;
    for (size_t tau = 1; tau <= maxTau + 1; ++tau)
    {
        double s = 0;
        for (size_t i = 0; i < w; ++i)
        {
            const double e = (double)a[i] - a[i + tau];
            s += e * e;
        }
        d[tau] = s;
        sum += s;
        c[tau] = sum > 0 ? s * (double)tau / sum : 1.0;
    }
    size_t tau = 0;
    for (size_t t = minTau; t <= maxTau; ++t)
        if (c[t] < 0.15)
        {
            while (t + 1 <= maxTau && c[t + 1] < c[t])
                ++t;
            tau = t;
            break;
        }
    if (tau == 0)
    {
        size_t m = minTau;
        for (size_t t = minTau; t <= maxTau; ++t)
            if (c[t] < c[m])
                m = t;
        if (c[m] > 0.35)
            return 0;
        tau = m;
    }
    // between samples: the parabola through the three around the minimum
    const double y0 = c[tau - 1], y1 = c[tau], y2 = c[tau + 1];
    const double den = y0 - 2 * y1 + y2;
    const double shift = std::fabs (den) > 1e-12 ? std::clamp (0.5 * (y0 - y2) / den, -0.5, 0.5) : 0.0;
    return (double)tau + shift;
}

namespace {
// x at a fractional position (Catmull-Rom between the samples around it)
double at (const float* x, size_t n, double t)
{
    const auto i = (long long)std::floor (t);
    const double f = t - (double)i;
    auto s = [&] (long long k) { return (double)x[(size_t)std::clamp<long long> (k, 0, (long long)n - 1)]; };
    const double p0 = s (i - 1), p1 = s (i), p2 = s (i + 1), p3 = s (i + 2);
    return p1 + 0.5 * f * (p2 - p0 + f * (2 * p0 - 5 * p1 + 4 * p2 - p3 + f * (3 * (p1 - p2) + p3 - p0)));
}
} // namespace

Table make (const float* x, size_t n, double sampleRate, double hintHz, int frameSize, int maxFrames)
{
    Table t;
    if (!x || n < 16 || frameSize < 2 || maxFrames < 1)
    {
        t.error = "There is no audio to make a wavetable of.";
        return t;
    }
    t.period = detectPeriod (x, n, sampleRate);
    t.detected = t.period > 0;
    if (!t.detected && hintHz > 0)
        t.period = sampleRate / hintHz;
    if (t.period < 2 || t.period * 2 > (double)n)
    {
        t.error = "No steady pitch found in the captured audio.";
        return t;
    }
    // the rising zero crossings, between samples
    std::vector<double> rising;
    for (size_t i = 1; i < n; ++i)
        if (x[i - 1] < 0.0f && x[i] >= 0.0f)
            rising.push_back ((double)(i - 1) + (double)(-x[i - 1]) / ((double)x[i] - x[i - 1]));
    if (rising.empty ())
    {
        t.error = "The captured audio does not cross zero.";
        return t;
    }
    // cycles from one crossing to the one nearest a period on (or a period on, if none is near)
    std::vector<std::pair<double, double>> cycles;
    double start = rising.front ();
    size_t k = 0;
    while (start + t.period < (double)n - 2)
    {
        const double target = start + t.period;
        while (k < rising.size () && rising[k] < target - t.period * 0.25)
            ++k;
        double next = target, bestDist = t.period * 0.25;
        for (size_t j = k; j < rising.size () && rising[j] <= target + t.period * 0.25; ++j)
            if (std::fabs (rising[j] - target) <= bestDist)
            {
                bestDist = std::fabs (rising[j] - target);
                next = rising[j];
            }
        if (next >= (double)n - 2)
            break;
        cycles.emplace_back (start, next);
        start = next;
    }
    if (cycles.empty ())
    {
        t.error = "The captured audio is shorter than one cycle.";
        return t;
    }
    // up to maxFrames of them, evenly spaced, each resampled to frameSize
    const size_t total = cycles.size ();
    t.count = (int)std::min<size_t> (total, (size_t)maxFrames);
    t.frames.resize ((size_t)t.count * (size_t)frameSize);
    for (int f = 0; f < t.count; ++f)
    {
        const size_t c = t.count == 1 ? 0 : (size_t)std::llround ((double)f * (double)(total - 1) / (double)(t.count - 1));
        const auto [a, b] = cycles[c];
        for (int s = 0; s < frameSize; ++s)
            t.frames[(size_t)f * (size_t)frameSize + (size_t)s] = (float)at (x, n, a + (b - a) * (double)s / frameSize);
    }
    float peak = 0;
    for (float v : t.frames)
        peak = std::max (peak, std::fabs (v));
    if (peak > 0)
        for (float& v : t.frames)
            v /= peak;
    return t;
}

} // namespace pk::wavetable
