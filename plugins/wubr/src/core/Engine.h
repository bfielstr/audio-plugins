// Wubr: two bell bands (Width octaves wide, exact in dB both ways: deep dips as well as boosts)
// whose gain and/or centre a drawn shape moves.
//   LFO: each band's shape runs as a cycle, synced to the host's beat grid (the phase follows the
//        song position while it plays) or free in Hz.
//   Envelope: a MIDI note, or a transient in the audio, starts the shapes from the top; each runs to
//        its hold point and stays there. With MIDI, letting go of the last note plays the rest of it.
// Target Gain: the band's level is Gain + Depth x the shape (dB). Frequency: the band is at Gain and
// its centre moves Sweep/2 octaves up at +1, down at -1. Both: both.
// Then dry/wet, output, and the Smacheratr at the end of the chain.
#pragma once

#include "Params.h"
#include "Shape.h"

#include "smacheratr/src/core/Biquad.h"
#include "smacheratr/src/core/Tail.h"

#include <array>
#include <atomic>

namespace wubr {

using ParamArray = std::array<double, kNumParams>;
double bellQ (double widthOct); // a bell's Q for a width in octaves (shared with the display)
ParamArray defaultParams ();

// For the editor (written by the audio thread once per block).
struct Meters
{
    std::atomic<float> pos[kBands] {};                                // where each band is in its shape (0..1)
    std::atomic<float> value[kBands] {};                              // the shape's value there (-1..1)
    std::atomic<float> gainDb[kBands] {};                             // the band's level now
    std::atomic<float> freqHz[kBands] = {120.0f, 2000.0f};            // its centre now
    std::atomic<uint32_t> triggers {0};                               // counts envelope triggers
    std::atomic<uint32_t> blocks {0};                                 // counts processed blocks
    std::atomic<float> sampleRate {48000.0f};
};

class Engine
{
public:
    void prepare (double sampleRate, int maxBlock);
    void reset ();
    void setParam (uint32_t id, double plain);
    double param (uint32_t id) const { return p[id]; }
    int latency () const { return tail.latency (); }
    void setMeters (Meters* m) { meters = m; }
    void setTailMeters (smacheratr::Meters* m) { tail.setMeters (m); }

    // the host's transport for the next block (tempo in BPM, the song position in quarter notes)
    void setTransport (double bpm, double ppq, bool playing);
    void noteOn ();
    void noteOff ();
    void trigger (); // starts the envelopes (MIDI or a transient)

    // In place capable.
    void process (const float* inL, const float* inR, float* outL, float* outR, int n);

private:
    struct Band
    {
        smacheratr::Biquad bell[2];
        double phase = 0.0;    // LFO position (0..1)
        double envPos = 0.0;   // Envelope position (0..1)
        double freqNow = 1000.0, dbNow = 0.0; // smoothed centre (Hz) and gain (dB)
        double shownPos = 0.0, shownValue = 0.0, shownDb = 0.0;
        Shape shape;
        bool shapeDirty = true;
    };
    double cycleHz (int band) const; // how many cycles a second the band's shape runs
    void refreshShape (int band);

    ParamArray p = defaultParams ();
    double sr = 48000.0;
    Band bands[kBands];
    double bpm = 120.0, ppq = 0.0;
    bool playing = false, wasPlaying = false;
    int notesHeld = 0;
    bool released = true; // Envelope: no note holds the shapes at their hold points
    // transient detection: fast and slow level followers (dB) and a short hold-off after a trigger
    double fastEnv = 0.0, slowEnv = 0.0, fastA = 0.0, fastR = 0.0, slowC = 0.0;
    int holdOff = 0;
    double smoothGain = 0.0, smoothFreq = 0.0;
    float mix = 1.0f, out = 1.0f;
    smacheratr::Tail tail;
    Meters* meters = nullptr;
};

} // namespace wubr
