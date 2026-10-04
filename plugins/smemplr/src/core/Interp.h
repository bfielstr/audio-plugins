// Band-limited (windowed-sinc) fractional reads. The kernel is widened when reading faster
// than real time so that pitching up doesn't alias.
#pragma once

#include "SampleData.h"

#include <algorithm>
#include <cmath>

namespace smemplr {

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
    // The same for 0 <= x < kHalf + 1, without the range check: the table goes on with zeros up to there
    // (the kernel is 0 from kHalf on, so this reads exactly what at() returns). The reads' kernels
    // reach at most kHalf + cutoff.
    float atNear (float x) const
    {
        const float f = x * kRes;
        const int i = (int)f;
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
        for (int i = 0; i < kSize; ++i)
            table[i] = 0.0f;
        for (int i = 0; i <= kHalf * kRes + 1; ++i)
        {
            const double x = (double)i / kRes;
            const double sinc = x < 1e-9 ? 1.0 : std::sin (M_PI * x) / (M_PI * x);
            const double r = x / kHalf;
            const double w = r >= 1.0 ? 0.0 : bessel0 (beta * std::sqrt (1.0 - r * r)) / norm;
            table[i] = (float)(sinc * w);
        }
    }
    static constexpr int kSize = (kHalf + 1) * kRes + 2;
    float table[kSize];
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
        // the kernel first (independent taps), then the sums in the same order as ever
        float w[2 * SincTable::kHalf * 4];
        const int cnt = hi - lo + 1;
        for (int j = 0; j < cnt; ++j)
            w[j] = t.atNear (std::fabs ((float)(j + 1 - half) - frac) * cutoff);
        const float* pa = a + lo;
        if (b)
        {
            const float* pb = b + lo;
            for (int j = 0; j < cnt; ++j)
            {
                wsum += w[j];
                sa += w[j] * pa[j];
                sb += w[j] * pb[j];
            }
        }
        else
            for (int j = 0; j < cnt; ++j)
            {
                wsum += w[j];
                sa += w[j] * pa[j];
            }
    }
    else
    {
        for (int i = lo; i <= hi; ++i)
        {
            const float w = t.atNear (std::fabs ((float)(i - base) - frac) * cutoff);
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
        const float w = t.atNear (std::fabs ((float)k - frac) * cutoff);
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

// Reads a sample at any speed without aliasing. readSinc alone narrows its band for a fast read only
// down to kMinCutoff (4x faster than real time); faster, the rest folds down across the spectrum. So a
// fast read takes a band-limited level of the sample instead (SampleData::mips): level k is read
// between 1 and 2^0.75 of its own rate, where readSinc's kernel is short (16 to 28 taps). In the top
// quarter octave before the next level, the two are crossfaded by how far the rate is into it, so a
// pitch bend or glide across a level glides too (the levels are phase-aligned: a plain linear blend).
// Below 2^0.75 (+9 semitones at the same sample rate) the sample itself is read exactly as readSinc
// always did.
class SampleReader
{
public:
    static constexpr double kFadeFrom = 0.75; // of an octave: where the crossfade to the next level starts

    SampleReader () = default;
    SampleReader (const SampleData& s, double rate) { set (s, rate); }

    // rate: sample frames per output sample (the read head's speed)
    void set (const SampleData& s, double rate)
    {
        rate = std::max (rate, 1e-9);
        const int top = s.levels () - 1;
        if (top <= 0 || rate < std::exp2 (kFadeFrom))
        {
            use (0, s, 0, rate, 1.0f);
            count = 1;
            return;
        }
        const double oct = std::log2 (rate);
        const int k = std::min ((int)std::floor (oct), top);
        const double f = oct - k; // above the top level f grows past 1: that level alone, narrowed
        if (k >= top || f < kFadeFrom)
        {
            use (0, s, k, rate, 1.0f);
            count = 1;
            return;
        }
        const float t = (float)((f - kFadeFrom) / (1.0 - kFadeFrom));
        use (0, s, k, rate, 1.0f - t);
        use (1, s, k + 1, rate, t);
        count = 2;
    }

    // Positions are in the sample's frames (whatever the level); outside it reads 0.
    void read (double pos, float& l, float& r) const
    {
        const Src& a = src[0];
        readSinc (a.a, a.b, a.len, pos * a.scale, a.cutoff, l, r);
        if (count == 1)
            return;
        const Src& b = src[1];
        float l2, r2;
        readSinc (b.a, b.b, b.len, pos * b.scale, b.cutoff, l2, r2);
        l = l * a.gain + l2 * b.gain;
        r = r * a.gain + r2 * b.gain;
    }

    int level () const { return src[count - 1].level; } // the (upper) level read

private:
    struct Src
    {
        const float* a = nullptr;
        const float* b = nullptr;
        int len = 0, level = 0;
        double scale = 1.0; // level frames per sample frame (exact: a power of two)
        float cutoff = 1.0f, gain = 1.0f;
    };
    void use (int i, const SampleData& s, int level, double rate, float gain)
    {
        Src& d = src[i];
        d.level = level;
        d.a = s.levelData (level, 0);
        d.b = s.numChannels > 1 ? s.levelData (level, 1) : nullptr;
        d.len = s.levelLength (level);
        d.scale = std::ldexp (1.0, -level);
        d.cutoff = (float)std::min (1.0, 1.0 / (rate * d.scale));
        d.gain = gain;
    }
    Src src[2];
    int count = 1;
};

} // namespace smemplr
