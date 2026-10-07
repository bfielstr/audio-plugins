// The cinema stage's Depth, on the Bass lane (mono: it goes to the mid only):
//   - a sub-octave: the 40 .. 120 Hz band drives a flip-flop that changes sign once per cycle (a
//     Schmitt trigger, so noise does not chatter it), a square at half the frequency, shaped by the
//     band's envelope (fast release: it stops with the note) and low-passed at 70 Hz into a round
//     20 .. 60 Hz sub, high-passed at 22 Hz;
//   - a slow low shelf: the lows below ~90 Hz (a 4th-order low-pass in parallel) lifted by up to
//     6 dB, less as they get louder (a slow RMS, 300 ms), so a loud low end does not flab;
//   - headroom: what Depth adds backs off where the low end peaks near -4.4 dBFS.
#pragma once

#include "Dsp.h"

namespace widr {

class Depth
{
public:
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        bandHpC = BiquadCoeffs::highPass (40.0, M_SQRT1_2, sr);
        bandLpC = BiquadCoeffs::lowPass (120.0, M_SQRT1_2, sr);
        subLpC = BiquadCoeffs::lowPass (70.0, M_SQRT1_2, sr);
        subHpC = BiquadCoeffs::highPass (22.0, M_SQRT1_2, sr);
        lowC[0] = BiquadCoeffs::lowPass (90.0, 0.54119610, sr);
        lowC[1] = BiquadCoeffs::lowPass (90.0, 1.30656296, sr);
        envAtt = (float)(1.0 - std::exp (-1.0 / (0.002 * sr)));
        envRel = (float)(1.0 - std::exp (-1.0 / (0.05 * sr)));
        rmsA = (float)(1.0 - std::exp (-1.0 / (0.3 * sr)));
        gainA = (float)(1.0 - std::exp (-1.0 / (0.2 * sr)));
        peakAtt = (float)(1.0 - std::exp (-1.0 / (0.001 * sr)));
        peakRel = (float)(1.0 - std::exp (-1.0 / (0.3 * sr)));
        reset ();
    }

    void reset ()
    {
        for (auto* b : {&bandHp, &bandLp, &subLp1, &subLp2, &subHp, &low1, &low2})
            b->reset ();
        env = rms = peak = 0.0f;
        gain = 1.0f;
        backOff = 1.0f;
        flip = 1.0f;
        armed = false;
    }

    // One sample of the Bass lane's mid in, what Depth adds out. amount 0 .. 1.
    inline float tick (float x, float amount)
    {
        // the sub-octave
        const float band = (float)bandLp.tick (bandLpC, bandHp.tick (bandHpC, x));
        const float a = std::fabs (band);
        env += (a - env) * (a > env ? envAtt : envRel);
        const float hyst = 0.25f * env + 1e-6f;
        if (band < -hyst)
            armed = true;
        else if (armed && band > hyst)
        {
            armed = false;
            flip = -flip;
        }
        float sub = (float)subLp2.tick (subLpC, subLp1.tick (subLpC, flip * env));
        sub = (float)subHp.tick (subHpC, sub);
        // the slow shelf: up to +6 dB, less once the lows are loud (above -20 dB RMS)
        const float low = (float)low2.tick (lowC[1], low1.tick (lowC[0], x));
        rms += (low * low - rms) * rmsA;
        const float level = std::sqrt (rms);
        const float dyn = level > 0.1f ? std::sqrt (0.1f / level) : 1.0f;
        gain += (1.0f + amount * dyn - gain) * gainA;
        float add = (gain - 1.0f) * low + amount * 0.9f * sub;
        // headroom
        const float pk = std::fabs (x + add);
        peak += (pk - peak) * (pk > peak ? peakAtt : peakRel);
        const float target = peak > kCeiling ? std::max (0.0f, kCeiling / peak) : 1.0f;
        backOff += (target - backOff) * (target < backOff ? peakAtt : gainA);
        return add * backOff;
    }

private:
    static constexpr float kCeiling = 0.6f;
    double sr = 48000.0;
    BiquadCoeffs bandHpC, bandLpC, subLpC, subHpC, lowC[2];
    Biquad bandHp, bandLp, subLp1, subLp2, subHp, low1, low2;
    float env = 0.0f, rms = 0.0f, gain = 1.0f, peak = 0.0f, backOff = 1.0f, flip = 1.0f;
    float envAtt = 0.0f, envRel = 0.0f, rmsA = 0.0f, gainA = 0.0f, peakAtt = 0.0f, peakRel = 0.0f;
    bool armed = false;
};

} // namespace widr
