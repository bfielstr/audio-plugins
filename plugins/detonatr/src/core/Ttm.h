// The Comp stages (two in the chain): multiband upward and downward compression towards a target,
// modelled on FabFilter Pro-C 3's TTM ("To The Max") style: "The TTM style combines upwards and
// downwards compression on multiple bands, making the input signal louder when it's quiet and
// quieter when it's loud. In this style the threshold effectively becomes a target level. The knee
// then controls the blending between the two stages, allowing you to further shape the pumping
// behavior."
//
// Three bands, split at Low and High by 24 dB/oct Linkwitz-Riley crossovers (Multidyn's, the low band
// through the high crossover's all-pass, so the bands add up flat). In each band:
//   - its level: the mean square of both channels over 5 ms, in dB;
//   - the target: Threshold, or with Auto, the band's own long-term level (its mean square, rising
//     over 300 ms and falling over 1 s; from silence it starts at the first sound's level), so each
//     band is pulled towards where it has been sitting and the spectrum's balance stays;
//   - the gain: d = level - target; the gain is -(1 - 1/Ratio) x d (down when louder, up when quieter),
//     within +-Range. Knee: within Knee dB of the target the correction fades out (quadratically, from
//     none at the target to full Knee dB away): 0 dB pumps on every dB, a wide knee leaves the level
//     near the target alone and only pulls in what strays far. A band under -60 dBFS is not raised
//     (fully raised from -50 dBFS up), so silence and noise floors stay down.
//   - two stages make the gain: the downward one (the cuts) and the upward one (the boosts). Each
//     engages with Attack and lets go with Release; a cut holds for Hold before it lets go. So with a
//     long Attack a hit passes before the downward stage catches it, and the body after it swells
//     as the upward stage comes in: the pumping. With Auto Release the release is program dependent:
//     1.5 x Release on held material, down to 0.05 x Release the further the band's level is over
//     its 100 ms average (12 dB over: a hit), and a boost lets go of a hit within a millisecond.
// Auto Gain makes up the level the stage adds or takes: the input's power against the output's,
// both averaged over 500 ms (the make-up follows over 50 ms; at most +-24 dB), so the stage stays
// about as loud as its input (its peaks can still rise: hits passing a slow attack stay raised). Dry mixes the bands, unprocessed,
// back in at its level (-60 dB: none). Output is the stage's output gain. No latency (no look-ahead).
#pragma once

#include "Dsp.h"

#include "multidyn/src/core/Crossover.h"

#include <array>

namespace detonatr {

class Ttm
{
public:
    static constexpr int kBands = 3;

    void prepare (double sampleRate, int maxBlock);
    void reset ();
    int latency () const { return 0; }

    void setThresholdDb (double db) { threshDb = (float)db; }
    void setAutoThreshold (bool on) { autoThresh = on; }
    void setRatio (double r) { slope = (float)(1.0 - 1.0 / std::max (1.0, r)); }
    void setAttackMs (double ms);
    void setReleaseMs (double ms);
    void setAutoRelease (bool on) { autoRelease = on; }
    void setKneeDb (double db) { knee = (float)std::max (0.0, db); }
    void setRangeDb (double db) { range = (float)std::max (0.0, db); }
    void setHoldMs (double ms);
    void setAutoGain (bool on) { autoGain = on; }
    void setDryDb (double db) { dry = db <= -60.0 ? 0.0f : (float)std::pow (10.0, db / 20.0); }
    void setCrossovers (double lowHz, double highHz);
    void setOutputDb (double db) { outGain = (float)std::pow (10.0, db / 20.0); }

    void process (float* l, float* r, int n);

    struct BandMeter
    {
        float levelDb = -100.0f, targetDb = -100.0f, gainDb = 0.0f;
    };
    BandMeter meter (int b) const { return {levelDb[(size_t)b], targetOf (b), gain[(size_t)b]}; }
    float makeupDb () const { return makeup; }
    double crossover (int i) const { return i == 0 ? xLow : xHigh; }

private:
    float targetOf (int b) const { return autoThresh ? dsp::powToDb (longTerm[(size_t)b] + 1e-20f) : threshDb; }
    double sr = 48000.0, xLow = 150.0, xHigh = 2500.0, releaseMs = 400.0, holdMs = 330.0, attackMs = 200.0;
    float threshDb = -20.0f, slope = 0.95f, knee = 3.0f, range = 40.0f, dry = 0.0f, outGain = 1.0f;
    bool autoThresh = true, autoRelease = true, autoGain = true;
    float attC = 0.001f, relC = 0.001f, levelC = 0.01f, longC = 0.0001f, longRiseC = 0.0001f, powC = 0.0001f, susC = 0.001f, makeupC = 0.01f;
    int holdSamples = 0;
    multidyn::XoverSplit splitLow, splitHigh;
    multidyn::XoverAllpass lowAllpass;
    std::array<float, kBands> ms {}, longTerm {}, levelDb {}, gain {}, slowDb {}, down {}, up {};
    std::array<int, kBands> hold {};
    float makeup = 0.0f, inPow = 0.0f, outPow = 0.0f;
    int ctl = 0;
};

} // namespace detonatr
