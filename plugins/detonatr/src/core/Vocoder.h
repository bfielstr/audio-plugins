// The Vocoder stage: a vocoder whose side-chain is the signal itself (MVocoder in its Vocoder mode
// with the same signal on the side-chain), so the sound vocodes itself.
//
// A bank of Bands band-pass filters, log-spaced from Low to High (each band 0 dB at its centre, its
// width the spacing of the bands; Order cascades 1 .. 3 of them, steeper with each). Each band gets an
// envelope follower (Attack / Release) and is multiplied by its own envelope: the band-wise square of
// the signal, so a loud band comes out louder against the quiet ones (an expansion of the spectrum),
// and a band's attack, where its envelope jumps, louder against its tail. That is the punch.
//
// The vocoded signal is level-matched to the input: its gain follows the ratio of the input's power
// to the vocoded signal's (both averaged over 60 ms), so on held material it is as loud as the input;
// where the level jumps (a hit) the average lags and the hit comes out louder (at most +8 dB on a band).
// Ratio blends the input (0) with the vocoded signal (1).
// The envelopes are linked: one per band for both channels (the louder), so the stereo image stays.
// No latency (the filters are IIRs; the input in the blend is the input itself).
#pragma once

#include "Dsp.h"

#include <array>
#include <vector>

namespace detonatr {

class Vocoder
{
public:
    static constexpr int kMaxBands = 100, kMaxOrder = 3;
    static constexpr float kMaxBandGain = 2.5f; // +8 dB

    void prepare (double sampleRate, int maxBlock);
    void reset ();
    int latency () const { return 0; }

    void setBands (int n);
    void setRange (double lowHz, double highHz);
    void setOrder (int sections); // 1 .. 3
    void setAttack (double ms);
    void setRelease (double ms);
    void setRatio (double r) { ratio = (float)std::clamp (r, 0.0, 1.0); }

    void process (float* l, float* r, int n);

    int bands () const { return numBands; }
    double centre (int b) const { return centres[(size_t)b]; }
    // the bands' envelopes after the last block, in dB (for the display)
    float bandLevelDb (int b) const { return dsp::ampToDb (env[(size_t)b] + 1e-9f); }

private:
    void design ();
    double sr = 48000.0;
    int numBands = 32, order = 2;
    double lowHz = 40.0, highHz = 16000.0, attackMs = 2.0, releaseMs = 60.0;
    float att = 0.1f, rel = 0.01f, norm = 0.01f, ratio = 0.46f;
    std::array<double, kMaxBands> centres {};
    std::array<std::array<dsp::BandPass, kMaxOrder>, kMaxBands> filters {};
    std::array<float, kMaxBands> env {};
    double inPow = 0.0, wetPow = 0.0;
};

} // namespace detonatr
