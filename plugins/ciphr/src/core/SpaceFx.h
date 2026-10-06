// Ciphr's processor (after the voices, shared by all of them): a multi-tap delay line per channel and
// allpass diffusers, with a frequency shifter in the feedback path.
//
//   in -> [input diffusers] -> + -> delay line -> taps (time x Length, gain, pan) -> [output diffusers] -> wet
//                              ^                    |
//                              |              tap 0 (Length itself)
//                              |                    v
//         soft clip <- DC block <- Regen <- shifter <- damping <- [loop diffusers] <- L/R mixing
//
// Space crossfades every diffuser between passing the signal untouched (0: discrete taps, a delay) and
// diffusing it fully (1: a reverb-like wash; the loop's diffusers make the repeats denser each time
// round). Length is the longest tap; every tap and diffuser scales with it. Movement wobbles the tap times
// (slow sine LFOs, one per tap). Regen is the feedback: right of centre the loop hears only the shifted
// signal (each repeat moves further: a spiral), left of centre the shifted and unshifted signals summed
// (moving notches). Its magnitude reaches a loop gain of 0.97; with the soft clip and the DC blocker in
// the loop the feedback can never run away. Character sets how many taps sound (2 to 8) and the loop's
// damping (2.5 to 16 kHz).
#pragma once

#include "Dsp.h"
#include "Variant.h"

namespace ciphr {

class SpaceFx
{
public:
    static constexpr double kMaxLengthMs = 2000.0;
    static constexpr double kLoopGain = 0.97; // at Regen +-100 %
    static constexpr int kSlice = 16;         // tap times are worked out every kSlice samples (ramped between)

    void prepare (double sampleRate);
    void reset (); // silence, every smoothed setting at its target
    void setPattern (const Tap* taps);
    void setSpace (double s) { spaceT = (float)std::clamp (s, 0.0, 1.0); }
    void setLength (double ms) { lengthT = std::clamp (ms, 1.0, kMaxLengthMs) * 0.001 * sr; }
    void setMovement (double m) { movement = std::clamp (m, 0.0, 1.0); }
    void setRegen (double r) { regenT = (float)std::clamp (r, -1.0, 1.0); }
    void setShift (double hz);
    void setCharacter (double c);
    // Drift's offsets for each tap (-1 .. 1, already scaled by the Drift amount): time and gain
    void setDrift (const double* time, const double* gain);

    // In place: the input in, the wet signal out.
    void process (float* l, float* r, int n);

    // for the tests and the display: tap i's time now (samples) and how much it sounds (weight x gain)
    double tapDelay (int i) const { return delayNow[i]; }
    double tapLevel (int i) const { return weight[i] * pattern[i].gain; }
    double lengthSamples () const { return lengthNow; }

private:
    void updateTaps (int n); // targets for the slice's end, and the steps there

    double sr = 48000.0;
    Tap pattern[kTaps];
    double weight[kTaps] {};
    double driftTime[kTaps] {}, driftGain[kTaps] {};
    double lfoPhase[kTaps] {};
    double delayNow[kTaps] {}, delayStep[kTaps] {};
    double lengthT = 20160.0, lengthNow = 20160.0, lengthSlew = 0.01;
    double movement = 0.0;
    float spaceT = 0.5f, space = 0.5f, regenT = 0.0f, regen = 0.0f, smooth = 0.001f;
    float dampCoef = 0.5f, dcCoef = 0.997f;
    double apScale = 1.0;
    float tapNorm = 1.0f;

    dsp::DelayLine line[2];
    dsp::Allpass inAp[2][4], outAp[2][4], loopAp[2][2];
    dsp::FreqShifter shifter[2];
    float fb[2] {}, damp[2] {}, dcX[2] {}, dcY[2] {};
};

} // namespace ciphr
