// The Limiter stages (two in the chain): a look-ahead brickwall limiter modelled on FabFilter Pro-L2's
// controls (Gain, Output Ceiling, Lookahead, Attack, Release, channel Link, True Peak).
//
// Gain drives the input. The gain is worked out in two parts, like Pro-L2's two stages:
//   - a slow part on the body: how far each sample (and with True Peak, the peaks between samples)
//     goes over the ceiling, followed with Attack (rising) and Release (falling), no look-ahead;
//   - a fast, look-ahead part for what the slow part leaves over the ceiling: the largest reduction
//     in a window of Lookahead samples, a Release (in dB), then two moving averages one after the
//     other whose spans add up to the window, so the gain glides into each peak along an S-curve over
//     the Lookahead. Every average mixes only values from windows that contain the sample, so the
//     gain on a sample is never above what that sample needs: the ceiling holds by construction.
// True Peak: the peaks between samples are found at 4x (a 16-tap windowed-sinc interpolator per
// phase, as Pro-L2's oversampled true-peak detection does) and count for the samples on either side,
// 0.1 dB under the ceiling (what the interpolator can miss). A last clamp at the ceiling guarantees
// the sample peak (it counts how often it acts: the tests expect never).
// Link: each channel's own reduction (0 %) against the louder channel's (100 %).
// Latency: constant, 8 samples (the interpolator) plus 5 ms (the longest Lookahead); a shorter
// Lookahead delays the detection to line up.
#pragma once

#include "Dsp.h"

#include <array>
#include <cstdint>
#include <vector>

namespace detonatr {

class Limiter
{
public:
    static constexpr double kMaxLookMs = 5.0;
    static constexpr int kHalfTaps = 16, kOver = 4;

    void prepare (double sampleRate, int maxBlock);
    void reset ();
    int latency () const { return kHalfTaps + maxWindow - 1; }

    void setGainDb (double db) { drive = (float)std::pow (10.0, db / 20.0); }
    void setCeilingDb (double db);
    void setLookaheadMs (double ms);
    void setAttackMs (double ms);
    void setReleaseMs (double ms);
    void setLink (double l) { link = (float)std::clamp (l, 0.0, 1.0); }
    void setTruePeak (bool on);

    void process (float* l, float* r, int n);

    float takeReductionDb (); // the most gain reduction since the last call (dB, 0 or more)
    int64_t clamps () const { return clampCount; }

private:
    struct FastGain
    {
        int window = 1;
        std::vector<float> wVal;
        std::vector<int64_t> wIdx;
        int wHead = 0, wCount = 0;
        int64_t count = 0;
        float env = 0.0f;
        struct Box
        {
            std::vector<double> buf;
            double sum = 0.0;
            int pos = 0;
            void prepare (int len);
            double push (double x);
        } b1, b2;
        void prepare (int w);
        float push (float reductionDb, float rel); // the gain (linear) for the sample window - 1 back
    };
    struct Ring
    {
        std::vector<float> buf;
        int pos = 0;
        void prepare (int len) { buf.assign ((size_t)std::max (1, len), 0.0f), pos = 0; }
        void reset () { std::fill (buf.begin (), buf.end (), 0.0f), pos = 0; }
        // returns the value pushed len samples ago (len 0: the value itself; see prepare: len = size)
        inline float push (float x, int len)
        {
            if (len <= 0)
                return x;
            const float y = buf[(size_t)pos];
            buf[(size_t)pos] = x;
            if (++pos >= len)
                pos = 0;
            return y;
        }
    };
    void configure ();

    double sr = 48000.0, lookMs = 1.0, attackMs = 3.0, releaseMs = 50.0;
    int maxWindow = 241, window = 49;
    float drive = 1.0f, ceil = 1.0f, link = 0.0f, attC = 0.1f, relC = 0.001f, fastRel = 0.001f;
    bool truePeak = true, dirty = true;
    // the interpolator: phase f's taps for x[m - kHalfTaps + 1 .. m + kHalfTaps], and each channel's history
    float taps[kOver - 1][2 * kHalfTaps] {};
    std::array<std::vector<float>, 2> hist; // written twice (i and i + 2 kHalfTaps), so the last 2 kHalfTaps are in a row
    int histPos = 0;
    float prevSeg[2] {};
    Ring audio[2], overDelay[2], slowDelay[2];
    float slowEnv[2] {};
    FastGain fast[2];
    float maxReduction = 0.0f;
    int64_t clampCount = 0;
};

} // namespace detonatr
