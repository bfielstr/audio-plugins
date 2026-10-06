// Moistr: splits a sound (typically a detuned bass) into three bands with a gap between the low mids and
// the high end, lets each band's frequency and level drift slowly on its own (the High band the most),
// then glues the bands back together with a compressor and a little soft clipping.
//
//   input -> Drive -> pass 1 -> [pass 2] -> Mix (dry / wet) -> Output -> Smacheratr (the end saturator)
//   a pass: Low (low-pass) + Mid (band-pass) + High (high-pass), each moving -> Glue -> Grit
//
// The second pass runs the first one's result through the same bands again with a movement of its own
// (Movement.h), as if it were bounced and filtered once more. Seed picks the movement's pattern; while
// the host plays, the movement follows the song position (setTransport), so a render is the same every
// time.
//
// The latency is the end saturator's (always in the path).
#pragma once

#include "Dsp.h"
#include "Movement.h"
#include "Params.h"

#include "smacheratr/src/core/Tail.h"

#include <array>
#include <atomic>

namespace moistr {

using ParamArray = std::array<double, kNumParams>;
ParamArray defaultParams ();

// What the editor shows (written by the audio thread once per block).
struct Meters
{
    std::atomic<uint32_t> blocks {0};
    std::atomic<bool> active {false}; // input heard in the last half second (the display follows the movement)
    std::atomic<int> passes {1};
    // each pass's bands now: their frequencies (Hz) and levels (dB, with the movement)
    std::array<std::array<std::atomic<float>, kBands>, kMaxPasses> freq {}, level {};
    std::atomic<float> glueDb {0.0f}; // the first pass's gain reduction
};

class Engine
{
public:
    static constexpr int kTick = 16; // the movement and the smoothing are worked out every kTick samples

    Engine ();
    void prepare (double sampleRate, int maxBlock);
    void reset ();
    void setParam (uint32_t id, double plain);
    double param (uint32_t id) const { return p[id]; }
    int latency () const { return tail.latency (); }
    void setMeters (Meters* m) { meters = m; }
    void setTailMeters (smacheratr::Meters* m) { tail.setMeters (m); }

    // The host's transport for the next process (): tempo, the song position at its first sample (in
    // quarter notes) and whether it plays. While it plays the movement's phase follows the song position.
    void setTransport (double bpm, double ppq, bool playing);

    // In place is fine.
    void process (const float* inL, const float* inR, float* outL, float* outR, int n);

    // for the tests and the display
    double bandFreq (int pass, int band) const { return state[pass].freqNow[band]; }   // Hz, with the movement
    double bandLevelDb (int pass, int band) const { return state[pass].levelDbNow[band]; }
    double phase () const { return theta; }
    double glueReductionDb () const { return state[0].glue.gainReductionDb (); }
    const Pattern& pattern (int pass) const { return patterns[pass]; }
    // A band's set frequency (Hz, with Gap, without the movement) from parameters.
    static double setFrequency (const ParamArray& p, int band);

private:
    struct PassState
    {
        dsp::Svf stage1[kBands][2], stage2[kBands][2];
        double gNow[kBands] {}, gainNow[kBands] {};       // the filters' g and the bands' gains at the tick's start
        double freqNow[kBands] {}, levelDbNow[kBands] {}; // (for the meters and the tests)
        dsp::Glue glue;
        dsp::Saturator grit;
        void resetFilters ();
    };
    void applyPattern ();
    // the targets at the end of the next tick (theta there) for a pass: g and gain per band
    void targets (int pass, double th, double* g, double* gain);
    void runPass (int pass, double* l, double* r, int m, double thetaEnd);

    ParamArray p = defaultParams ();
    double sr = 48000.0;
    Pattern patterns[kMaxPasses];
    PassState state[kMaxPasses];
    dsp::Saturator drive;
    // smoothed settings (per tick)
    double logFreq[kBands] {}, levelDb[kBands] {}, res[kBands] {}, move = 0.0, levelMove = 0.0;
    double tickSmooth = 0.1;
    // the movement's phase (cycles of Rate) and the transport
    double theta = 0.0;
    double bpm = 120.0, songPpq = 0.0, expectPpq = 0.0;
    bool playing = false, wasPlaying = false, transportSet = false;
    // the second pass's fade (0 .. 1) and the output's smoothing
    double pass2 = 0.0;
    float mix = 1.0f, out = 1.0f, smooth = 0.001f;
    int quiet = 0; // samples since the input was last heard
    Meters* meters = nullptr;
    smacheratr::Tail tail;
};

} // namespace moistr
