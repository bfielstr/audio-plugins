// Widr's Space: a compact feedback delay network (8 lines, Hadamard feedback, damping in the loop)
// behind a pre-delay. The engine adds its output to the side signal only, so it reads as width,
// not distance, and adds nothing to the mono fold.
#pragma once

#include "Dsp.h"

#include <array>

namespace widr {

class Reverb
{
public:
    static constexpr int kLines = 8;

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        static constexpr double kMs[kLines] = {23.3, 29.1, 33.7, 37.9, 41.3, 46.9, 53.1, 59.7};
        for (int i = 0; i < kLines; ++i)
        {
            len[(size_t)i] = std::max (8, (int)std::lround (kMs[i] * 0.001 * sr));
            lines[(size_t)i].prepare (len[(size_t)i]);
        }
        pre.prepare ((int)(0.085 * sr) + 4);
        lowCut = OnePole::coeff (120.0, sr);
        reset ();
    }

    void reset ()
    {
        for (auto& l : lines)
            l.reset ();
        pre.reset ();
        for (auto& d : damp)
            d.reset ();
        for (auto& d : lowState)
            d.reset ();
    }

    // Per block.
    void set (double decayMs, double preDelayMs, double dampHz)
    {
        const double t60 = std::max (0.05, decayMs * 0.001);
        for (int i = 0; i < kLines; ++i)
            g[(size_t)i] = (float)std::pow (10.0, -3.0 * len[(size_t)i] / (t60 * sr));
        preSamples = std::clamp ((int)std::lround (preDelayMs * 0.001 * sr), 1, pre.capacity ());
        dampA = OnePole::coeff (dampHz, sr);
    }

    // One sample in, the side contribution out.
    inline float tick (float in)
    {
        pre.push (in);
        const float x = pre.tap (preSamples) * 0.35f;
        std::array<float, kLines> y;
        float out = 0.0f;
        for (int i = 0; i < kLines; ++i)
        {
            const float v = lines[(size_t)i].tap (len[(size_t)i]);
            out += kOutSign[i] * v;
            // high damping (low-pass at Damping) and low damping (the lows below ~120 Hz decay fast)
            float d = damp[(size_t)i].lp (dampA, v);
            d -= lowState[(size_t)i].lp (lowCut, d);
            y[(size_t)i] = d;
        }
        hadamard (y);
        for (int i = 0; i < kLines; ++i)
            lines[(size_t)i].push (x * kInSign[i] + g[(size_t)i] * y[(size_t)i]);
        return out * 0.35355339f; // 1/sqrt(8)
    }

private:
    static void hadamard (std::array<float, kLines>& a)
    {
        for (int h = 1; h < kLines; h <<= 1)
            for (int i = 0; i < kLines; i += 2 * h)
                for (int j = i; j < i + h; ++j)
                {
                    const float u = a[(size_t)j], v = a[(size_t)(j + h)];
                    a[(size_t)j] = u + v;
                    a[(size_t)(j + h)] = u - v;
                }
        for (auto& v : a)
            v *= 0.35355339f;
    }

    static constexpr float kInSign[kLines] = {1, -1, 1, 1, -1, 1, -1, -1};
    static constexpr float kOutSign[kLines] = {1, 1, -1, 1, -1, -1, 1, -1};

    double sr = 48000.0;
    std::array<DelayLine, kLines> lines;
    std::array<int, kLines> len {};
    std::array<float, kLines> g {};
    std::array<OnePole, kLines> damp, lowState;
    DelayLine pre;
    int preSamples = 1;
    float dampA = 0.5f, lowCut = 0.01f;
};

} // namespace widr
