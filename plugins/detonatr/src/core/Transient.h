// The Transient stages (two of them in the chain): a transient modulator modelled on Sonnox's Oxford
// TransMod's controls.
//
//   Gain       dB into the process (the signal and so the detector)
//   Threshold  dB: levels under it do not count (the detector's levels are floored at it, so a hit
//              counts only for how far it rises above it)
//   Rise Time  ms: the attack of the peak detector (longer: short, initial transients are left out)
//   Overshoot  ms: the release of the peak detector: how long the gain change lasts after a peak
//              (short: only the leading edges)
//   Recovery   ms: the time constant of the longer-term average level (rising and falling)
//   Ratio      -1 .. +1: the gain change is Ratio x (peak - average) in dB, so at +1 a peak 10 dB
//              over the average comes out 20 dB over it, at -1 it is brought down to the average
//   Deadband   dB: the first Deadband dB of each difference are ignored (no change for small ones)
//   Overdrive  %: soft saturation mixed in where the gain is raised (snap and weight on the attack)
//   Output     dB, and Mix (the input, lined up, against the processed signal).
// One detector for both channels (the louder), so the image stays. The gain change is at most
// +-30 dB and is smoothed over 0.05 ms against zipper noise. No latency.
#pragma once

#include "Dsp.h"

namespace detonatr {

class Transient
{
public:
    static constexpr float kMaxDb = 30.0f;

    void prepare (double sampleRate, int maxBlock);
    void reset ();
    int latency () const { return 0; }

    void setGainDb (double db) { inGain = (float)std::pow (10.0, db / 20.0); }
    void setThresholdDb (double db) { threshDb = (float)db; }
    void setDeadbandDb (double db) { deadband = (float)std::max (0.0, db); }
    void setRatio (double r) { ratio = (float)std::clamp (r, -1.0, 1.0); }
    void setOvershootMs (double ms);
    void setRiseMs (double ms);
    void setRecoveryMs (double ms);
    void setOverdrive (double amount) { drive = (float)std::clamp (amount, 0.0, 1.0); }
    void setOutputDb (double db) { outGain = (float)std::pow (10.0, db / 20.0); }
    void setMix (double m) { mix = (float)std::clamp (m, 0.0, 1.0); }

    void process (float* l, float* r, int n);

    // the largest boost and cut since the last call (dB; for the display)
    void takeGainRange (float& boostDb, float& cutDb);

private:
    double sr = 48000.0, overshootMs = 25.0, riseMs = 0.1, recoveryMs = 77.0;
    float inGain = 1.0f, threshDb = -80.0f, deadband = 0.0f, ratio = 0.27f, drive = 0.0f, outGain = 1.0f, mix = 1.0f;
    float riseC = 0.5f, overC = 0.001f, recC = 0.001f, smoothC = 0.3f;
    float peak = 0.0f, avg = 0.0f, gainDb = 0.0f;
    float maxBoost = 0.0f, maxCut = 0.0f;
};

} // namespace detonatr
