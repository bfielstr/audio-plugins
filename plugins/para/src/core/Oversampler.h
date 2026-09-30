// Para's 4x oversampler for its drives: Smacheratr's (two 2x stages, each a linear-phase Kaiser-windowed
// sinc half-band FIR, the same taps and so the same sound and the same latency), computed polyphase.
// A half-band FIR's taps are zero at every other place but the centre, and up-sampling feeds it a zero
// every other sample: of the 2 x taps multiplies per input sample Smacheratr's does, up and down, only
// about a quarter are left (each stage's output at the centre phase is the input delayed, its other
// phase the odd taps), and the windows are contiguous so the dot products vectorise. Two drives in
// stereo, 4x, fit in a small part of a core.
#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

namespace para {

class Halfband2xPoly
{
public:
    // As smacheratr::Halfband2x::design (passEdge: the end of the passband as a fraction of the input
    // rate; 4k + 1 taps).
    void design (double passEdge, double attenDb, int maxTaps)
    {
        auto besselI0 = [] (double x) {
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
        };
        const double df = std::max (0.005, 0.5 - passEdge);
        const double beta = attenDb > 50.0   ? 0.1102 * (attenDb - 8.7)
                            : attenDb >= 21.0 ? 0.5842 * std::pow (attenDb - 21.0, 0.4) + 0.07886 * (attenDb - 21.0)
                                              : 0.0;
        int n = (int)std::ceil ((attenDb - 8.0) / (2.285 * 2.0 * M_PI * df)) + 1;
        n = std::clamp (n, 5, maxTaps);
        n = 4 * ((n + 2) / 4) + 1;
        std::vector<float> h ((size_t)n, 0.0f);
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
        // the odd taps h[1], h[3] .. h[n - 2], ordered oldest sample first (h[2j + 1] weighs the sample
        // j steps back), and the centre
        half = M / 2; // k
        len = 2 * half;
        odd.assign ((size_t)len, 0.0f);
        for (int j = 0; j < len; ++j)
            odd[(size_t)(len - 1 - j)] = h[(size_t)(2 * j + 1)];
        centre = h[(size_t)M];
        lat = M;
        for (auto* b : {&upHist, &oddHist, &evenHist})
            b->assign ((size_t)(2 * len), 0.0f);
        reset ();
    }
    void reset ()
    {
        for (auto* b : {&upHist, &oddHist, &evenHist})
            std::fill (b->begin (), b->end (), 0.0f);
        upPos = downPos = 0;
        lastOdd = 0.0f;
    }
    int latency () const { return lat; } // input samples, up + down together

    // out: 2n samples
    void up (const float* in, float* out, int n)
    {
        for (int i = 0; i < n; ++i)
        {
            const float* w = push (upHist, upPos, 2.0f * in[i]);
            out[2 * i] = centre * w[len - 1 - half];
            out[2 * i + 1] = dot (w);
        }
    }
    // in: 2n samples
    void down (const float* in, float* out, int n)
    {
        for (int i = 0; i < n; ++i)
        {
            // the odd sample before this pair and this pair's even one; out: the even one half the
            // window back at the centre, the odd ones through the odd taps
            oddHist[(size_t)downPos] = oddHist[(size_t)(downPos + len)] = lastOdd;
            evenHist[(size_t)downPos] = evenHist[(size_t)(downPos + len)] = in[2 * i];
            advance (downPos);
            out[i] = centre * evenHist[(size_t)(downPos + len - 1 - half)] + dot (oddHist.data () + downPos);
            lastOdd = in[2 * i + 1];
        }
    }

private:
    // writes x (twice, so the last len samples are always contiguous) and returns the window, oldest first
    const float* push (std::vector<float>& b, int& pos, float x)
    {
        b[(size_t)pos] = b[(size_t)(pos + len)] = x;
        advance (pos);
        return b.data () + pos;
    }
    void advance (int& pos)
    {
        if (++pos >= len)
            pos = 0;
    }
    float dot (const float* w) const
    {
        // eight partial sums, so the compiler can keep them in one vector register
        const float* t = odd.data ();
        float a[8] = {};
        int j = 0;
        for (; j + 8 <= len; j += 8)
            for (int l = 0; l < 8; ++l)
                a[l] += t[j + l] * w[j + l];
        float acc = ((a[0] + a[4]) + (a[1] + a[5])) + ((a[2] + a[6]) + (a[3] + a[7]));
        for (; j < len; ++j)
            acc += t[j] * w[j];
        return acc;
    }

    std::vector<float> odd, upHist, oddHist, evenHist;
    float centre = 0.5f, lastOdd = 0.0f;
    int half = 0, len = 0, lat = 0, upPos = 0, downPos = 0;
};

class Oversampler4x
{
public:
    // As smacheratr::Oversampler::prepare.
    void prepare (double sr, int maxBlock)
    {
        const double pass = std::min (20000.0, 0.46 * sr);
        s1.design (pass / sr, 80.0, 257);
        s2.design (pass / (2.0 * sr), 80.0, 257);
        mid.assign ((size_t)std::max (1, maxBlock) * 2, 0.0f);
        reset ();
    }
    void reset ()
    {
        s1.reset ();
        s2.reset ();
    }
    int latency () const { return s1.latency () + s2.latency () / 2; } // input samples
    void up (const float* in, float* out, int n) // out: 4n samples
    {
        s1.up (in, mid.data (), n);
        s2.up (mid.data (), out, 2 * n);
    }
    void down (const float* in, float* out, int n) // in: 4n samples
    {
        s2.down (in, mid.data (), 2 * n);
        s1.down (mid.data (), out, n);
    }

private:
    Halfband2xPoly s1, s2;
    std::vector<float> mid;
};

} // namespace para
