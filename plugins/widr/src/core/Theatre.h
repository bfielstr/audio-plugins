// The cinema stage's Theatre: a large, dark hall. A theatre-size pattern of early reflections (19 to
// 127 ms, alternating sides) and a feedback delay network of 8 long lines (67 to 151 ms, Hadamard
// feedback) behind a pre-delay of 30 to 80 ms, decaying in 2.2 to 4.8 s, darkened in the loop (3.8 kHz)
// with the lowest lows leaving fast (so the sub stays tight), then a "screen" EQ on its outputs: a
// gentle top roll-off (-6 dB shelf from 4.5 kHz) and low-mid body (+3 dB at 220 Hz). Two unrelated
// outputs, left and right. The engine feeds it only the Wide and Beyond lanes.
#pragma once

#include "Dsp.h"

#include <array>

namespace widr {

class Theatre
{
public:
    static constexpr int kLines = 8, kTaps = 12;

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        static constexpr double kMs[kLines] = {67.3, 79.1, 89.9, 101.3, 113.7, 127.1, 139.9, 151.3};
        for (int i = 0; i < kLines; ++i)
        {
            len[(size_t)i] = std::max (8, (int)std::lround (kMs[i] * 0.001 * sr));
            lines[(size_t)i].prepare (len[(size_t)i]);
        }
        pre.prepare ((int)(0.13 * sr) + 4);
        for (int t = 0; t < kTaps; ++t)
            tapAt[(size_t)t] = std::max (1, (int)std::lround (kTapMs[t] * 0.001 * sr));
        dampA = OnePole::coeff (3800.0, sr);
        lowCut = OnePole::coeff (90.0, sr);
        inHpC = BiquadCoeffs::highPass (120.0, M_SQRT1_2, sr);
        inLpC = BiquadCoeffs::lowPass (7000.0, M_SQRT1_2, sr);
        shelfC = BiquadCoeffs::highShelf (4500.0, -6.0, sr);
        bodyC = BiquadCoeffs::peaking (220.0, 0.7, 3.0, sr);
        set (0.5);
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
        for (auto* b : {&inHp, &inLp})
            b->reset ();
        for (auto& b : eq)
            b.reset ();
    }

    // Per block: the Theatre amount (0 .. 1) sets the size (decay and pre-delay).
    void set (double amount)
    {
        amount = std::clamp (amount, 0.0, 1.0);
        const double t60 = 2.2 + 2.6 * amount;
        for (int i = 0; i < kLines; ++i)
            g[(size_t)i] = (float)std::pow (10.0, -3.0 * len[(size_t)i] / (t60 * sr));
        preSamples = std::clamp ((int)std::lround ((0.03 + 0.05 * amount) * sr), 1, pre.capacity ());
    }

    inline void tick (float in, float& outL, float& outR)
    {
        const float x = (float)inLp.tick (inLpC, inHp.tick (inHpC, in));
        pre.push (x);
        // early reflections: the theatre's walls, alternating sides, fading
        float eL = 0.0f, eR = 0.0f;
        for (int t = 0; t < kTaps; t += 2)
        {
            eL += kTapGain[t] * pre.tap (tapAt[(size_t)t]);
            eR += kTapGain[t + 1] * pre.tap (tapAt[(size_t)(t + 1)]);
        }
        const float xin = pre.tap (preSamples) * 0.3f;
        std::array<float, kLines> y;
        float oL = 0.0f, oR = 0.0f;
        for (int i = 0; i < kLines; ++i)
        {
            const float v = lines[(size_t)i].tap (len[(size_t)i]);
            oL += kOutL[i] * v;
            oR += kOutR[i] * v;
            float d = damp[(size_t)i].lp (dampA, v);
            d -= lowState[(size_t)i].lp (lowCut, d);
            y[(size_t)i] = d;
        }
        hadamard (y);
        for (int i = 0; i < kLines; ++i)
            lines[(size_t)i].push (xin * kInSign[i] + g[(size_t)i] * y[(size_t)i]);
        // the screen EQ
        const float l = 0.6f * eL + oL * 0.35355339f, r = 0.6f * eR + oR * 0.35355339f;
        outL = (float)eq[1].tick (bodyC, eq[0].tick (shelfC, l));
        outR = (float)eq[3].tick (bodyC, eq[2].tick (shelfC, r));
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

    static constexpr float kInSign[kLines] = {1, 1, -1, 1, -1, -1, 1, -1};
    static constexpr float kOutL[kLines] = {1, -1, 1, 1, -1, 1, -1, -1};
    static constexpr float kOutR[kLines] = {1, 1, 1, -1, -1, -1, -1, 1}; // orthogonal to kOutL
    static constexpr double kTapMs[kTaps] = {19.0, 23.0, 31.0, 37.0, 43.0, 53.0, 61.0, 71.0, 83.0, 97.0, 109.0, 127.0};
    static constexpr float kTapGain[kTaps] = {0.55f, -0.5f, 0.45f, 0.42f, -0.38f, 0.35f, 0.3f, -0.28f, 0.24f, 0.22f, -0.18f, 0.16f};

    double sr = 48000.0;
    std::array<DelayLine, kLines> lines;
    std::array<int, kLines> len {};
    std::array<float, kLines> g {};
    std::array<OnePole, kLines> damp, lowState;
    std::array<int, kTaps> tapAt {};
    DelayLine pre;
    int preSamples = 1;
    float dampA = 0.4f, lowCut = 0.01f;
    BiquadCoeffs inHpC, inLpC, shelfC, bodyC;
    Biquad inHp, inLp;
    std::array<Biquad, 4> eq;
};

} // namespace widr
