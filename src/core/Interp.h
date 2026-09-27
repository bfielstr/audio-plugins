// Band-limited (windowed-sinc) fractional reads. The kernel is widened when reading faster
// than real time so that pitching up doesn't alias.
#pragma once

#include <algorithm>
#include <cmath>

namespace simplr {

class SincTable
{
public:
    static constexpr int kHalf = 8;    // zero crossings each side at full bandwidth
    static constexpr int kRes = 512;   // table points per unit
    static constexpr float kMinCutoff = 0.25f;

    static const SincTable& get ()
    {
        static const SincTable t;
        return t;
    }

    float at (float x) const // x >= 0, in zero-crossing units
    {
        const float f = x * kRes;
        const int i = (int)f;
        if (i >= kHalf * kRes)
            return 0.0f;
        const float fr = f - (float)i;
        return table[i] + fr * (table[i + 1] - table[i]);
    }

private:
    SincTable ()
    {
        const double beta = 7.5;
        auto bessel0 = [] (double x) {
            double sum = 1.0, term = 1.0;
            for (int k = 1; k < 40; ++k)
            {
                term *= (x / (2.0 * k)) * (x / (2.0 * k));
                sum += term;
            }
            return sum;
        };
        const double norm = bessel0 (beta);
        for (int i = 0; i <= kHalf * kRes + 1; ++i)
        {
            const double x = (double)i / kRes;
            const double sinc = x < 1e-9 ? 1.0 : std::sin (M_PI * x) / (M_PI * x);
            const double r = x / kHalf;
            const double w = r >= 1.0 ? 0.0 : bessel0 (beta * std::sqrt (1.0 - r * r)) / norm;
            table[i] = (float)(sinc * w);
        }
    }
    float table[kHalf * kRes + 2];
};

// Reads one or two channels at a fractional position. Positions outside [0, len) read as 0.
// cutoff: 1 = full band, < 1 when the read head moves faster than 1 sample per output sample.
inline void readSinc (const float* a, const float* b, int len, double pos, float cutoff, float& outA, float& outB)
{
    const auto& t = SincTable::get ();
    cutoff = std::clamp (cutoff, SincTable::kMinCutoff, 1.0f);
    const int base = (int)std::floor (pos);
    const float frac = (float)(pos - base);
    const int half = (int)std::ceil (SincTable::kHalf / cutoff);
    float sa = 0.0f, sb = 0.0f, wsum = 0.0f;
    const int lo = base - half + 1, hi = base + half;
    if (lo >= 0 && hi < len)
    {
        for (int i = lo; i <= hi; ++i)
        {
            const float w = t.at (std::fabs ((float)(i - base) - frac) * cutoff);
            wsum += w;
            sa += w * a[i];
            if (b)
                sb += w * b[i];
        }
    }
    else
    {
        for (int i = lo; i <= hi; ++i)
        {
            const float w = t.at (std::fabs ((float)(i - base) - frac) * cutoff);
            wsum += w;
            if (i < 0 || i >= len)
                continue;
            sa += w * a[i];
            if (b)
                sb += w * b[i];
        }
    }
    // Normalise the DC gain (the truncated kernel doesn't sum to exactly 1).
    const float g = wsum > 1e-6f ? 1.0f / wsum : 0.0f;
    outA = sa * g;
    outB = b ? sb * g : outA;
}

// Same as readSinc but for a power-of-two ring buffer addressed by an absolute index.
inline void readSincRing (const float* a, const float* b, int mask, double pos, float cutoff, float& outA, float& outB)
{
    const auto& t = SincTable::get ();
    cutoff = std::clamp (cutoff, SincTable::kMinCutoff, 1.0f);
    const long long base = (long long)std::floor (pos);
    const float frac = (float)(pos - (double)base);
    const int half = (int)std::ceil (SincTable::kHalf / cutoff);
    float sa = 0.0f, sb = 0.0f, wsum = 0.0f;
    for (int k = -half + 1; k <= half; ++k)
    {
        const float w = t.at (std::fabs ((float)k - frac) * cutoff);
        const int idx = (int)((base + k) & mask);
        wsum += w;
        sa += w * a[idx];
        if (b)
            sb += w * b[idx];
    }
    const float g = wsum > 1e-6f ? 1.0f / wsum : 0.0f;
    outA = sa * g;
    outB = b ? sb * g : outA;
}

inline int sincReach (float cutoff)
{
    return (int)std::ceil (SincTable::kHalf / std::clamp (cutoff, SincTable::kMinCutoff, 1.0f)) + 1;
}

} // namespace simplr
