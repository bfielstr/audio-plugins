// Ciphr: an 8-voice instrument. Each voice plays an oscillator cluster (Timbre scanning each oscillator's
// list of waves and pitches, Cross modulating neighbours) through a morphing filter and an amp envelope;
// the stereo input bus joins either the voices' sum (Direct) or each voice before its filter (Voices).
// The voices' sum then goes through the processor (SpaceFx: taps, diffusion, the frequency shifter in
// the feedback), Blend, Output and the suite's end saturator.
//
//   voices (+ input) -> processor -> Blend (dry / wet) -> Output -> Smacheratr (the end saturator)
//
// Variant picks the patch (Variant.h); Drift glides the oscillators' list positions and detune and the
// taps' times and gains between random states (0: static). Character widens the cluster's detune and
// sets the processor's tap count and loop brightness.
//
// The latency is the end saturator's (always in the path).
#pragma once

#include "Params.h"
#include "SpaceFx.h"
#include "Variant.h"
#include "Voice.h"

#include "smacheratr/src/core/Tail.h"

#include <array>
#include <atomic>

namespace ciphr {

using ParamArray = std::array<double, kNumParams>;
ParamArray defaultParams ();

// What the editor shows (written by the audio thread once per block).
struct Meters
{
    std::atomic<uint32_t> blocks {0};
    std::atomic<int> voices {0};                      // sounding voices
    std::array<std::atomic<float>, kOscs> oscPos {};  // each oscillator's place in its list (Timbre and Drift)
    std::array<std::atomic<float>, kTaps> tapMs {};   // each tap's time now (ms)
    std::array<std::atomic<float>, kTaps> tapLevel {}; // and how much it sounds (0 .. 1)
};

class Engine
{
public:
    static constexpr int kSlice = 32;           // the voices' settings are worked out every kSlice samples
    static constexpr float kVoiceGain = 0.35f;  // each voice's share of the sum
    static constexpr double kDetuneCents = 18.0; // Character 100 %: the outer oscillators this far apart (each way)
    static constexpr double kDriftPos = 0.75;    // Drift 100 %: how far an oscillator's list position wanders
    static constexpr double kDriftCents = 12.0;  // and its pitch

    Engine ();
    void prepare (double sampleRate, int maxBlock);
    void reset ();
    void setParam (uint32_t id, double plain);
    double param (uint32_t id) const { return p[id]; }
    int latency () const { return tail.latency (); }
    void setMeters (Meters* m) { meters = m; }
    void setTailMeters (smacheratr::Meters* m) { tail.setMeters (m); }

    void noteOn (int note, float velocity);
    void noteOff (int note);
    void allNotesOff ();

    // inL / inR: the stereo input bus (nullptr: silence). In place is fine.
    void process (const float* inL, const float* inR, float* outL, float* outR, int n);

    // for the tests and the display
    const Patch& patch () const { return current; }
    const SpaceFx& processorStage () const { return space; }
    int activeVoices () const;
    const Voice& voice (int i) const { return voices[(size_t)i]; }
    double oscPosition (int k) const { return block.oscPos[k]; }

private:
    void applyEnvelopes ();
    void applyVariant ();
    void advanceDrift (int n);
    void prepareBlock (int n); // the slice's VoiceBlock

    ParamArray p = defaultParams ();
    double sr = 48000.0;
    Patch current;
    std::array<Voice, kVoices> voices;
    uint32_t order = 0;
    SpaceFx space;
    VoiceBlock block;
    // smoothed settings
    double timbre = 0.0, cutoffLog = 0.0;
    float cross = 0.0f;
    float blend = 0.35f, out = 1.0f, smooth = 0.001f;
    double sliceSmooth = 0.1;
    // Drift: smooth random glides, one per oscillator position, oscillator pitch, tap time and tap gain
    static constexpr int kDriftChannels = 2 * kOscs + 2 * kTaps;
    double driftFrom[kDriftChannels] {}, driftTo[kDriftChannels] {}, driftT[kDriftChannels] {};
    Rng driftRng {1};
    Meters* meters = nullptr;
    smacheratr::Tail tail;
};

} // namespace ciphr
