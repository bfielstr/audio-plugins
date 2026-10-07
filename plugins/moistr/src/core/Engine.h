// Moistr: splits a sound (typically a detuned bass) into three or four bands with a phase-coherent
// crossover (Linkwitz-Riley 4th order), keeps the low end locked and lets the bands above it rise and fall
// on a seeded pattern, then glues the bands back together with a compressor and a little soft clipping.
//
//   input -> Drive -> pass 1 -> [pass 2] -> Mix (dry / wet) -> Output -> Smacheratr (the end saturator)
//   a pass: the split (Low | Mid | High [| Air]), each band at its gain -> [Shift: the bands above Low] ->
//           sum -> Glue -> Grit
//
// Shift (optional, off by default): a single-sideband frequency shifter (dsp::Hilbert and a complex
// oscillator) on the sum of the bands above Low, after their rise and fall and before they meet the Low
// band again, in each pass. The Low band never goes through it, so nothing below Low X is shifted; the
// shifted signal is high-passed at Low X (12 dB/oct) so a shift down cannot push energy into the sub
// region or to DC. Shift Mix blends the shifted bands with the unshifted ones (through the same allpasses,
// so they do not comb). Shift glides (30 ms) and its phase runs on, so moving it does not click; switching
// it on or off fades over 20 ms; while off it is not run at all (the output is the engine's without it).
//
// The Low band's crossover is picked by the Seed (100 .. 500 Hz) and never moves; the Low band's level is
// its Level, always. Mid, High and Air rise from (Level - Depth x Movement x their Move) to their Level and
// fall back at the seeded moments, with seeded rise and fall times (x Rise, x Fall); the upper crossovers
// drift a little with Movement (Movement.h). At Movement 0 a pass is exactly the static split. The bands
// sum flat when they are at the same gain (an all-pass of the input). Switching between 3 and 4 bands
// fades the Air band from following High to its own gain over 20 ms (the split always has four bands).
//
// The second pass runs the first one's result through the split again with a movement of its own (the
// same Low crossover), as if it were bounced and split once more. While the host plays, the movement
// follows the song position (setTransport), so a render is the same every time.
//
// The latency is the end saturator's (always in the path).
#pragma once

#include "Dsp.h"
#include "Movement.h"
#include "Params.h"

#include "smacheratr/src/core/Tail.h"

#include <array>
#include <atomic>
#include <cmath>

namespace moistr {

using ParamArray = std::array<double, kNumParams>;
ParamArray defaultParams ();

// What the editor shows (written by the audio thread once per block; read with relaxed loads after an
// acquire load of `blocks`).
struct Meters
{
    std::atomic<uint32_t> blocks {0};
    std::atomic<bool> active {false}; // input heard in the last half second (the display follows the movement)
    std::atomic<int> passes {1};
    // (legacy, the 0.18 display) per pass: freq = {Low X, the Mid band's centre, Mid X} (Hz), level = the
    // Low, Mid and High bands' gains now (dB, as gainDb)
    std::array<std::array<std::atomic<float>, kBands>, kMaxPasses> freq {}, level {};
    std::atomic<float> glueDb {0.0f}; // the first pass's gain reduction
    // the split
    std::atomic<int> bands {3};                // 3 or 4 (Bands)
    std::atomic<float> lowXover {270.0f};      // Hz: the Seed's Low crossover (locked, both passes)
    // per pass, the crossovers now (Hz): [0] Low X (= lowXover), [1] Mid X, [2] High X (with the drift)
    std::array<std::array<std::atomic<float>, kMaxXovers>, kMaxPasses> xover {};
    // per pass, each band's gain now (dB, with the movement; -100: off): [0] Low, [1] Mid, [2] High, [3] Air.
    // With 3 bands, Air follows High (its value is High's).
    std::array<std::array<std::atomic<float>, kMaxBands>, kMaxPasses> gainDb {};
    // per pass, each band's lift now: 0 at its floor (Level - Depth x Movement x Move), 1 at its Level
    // ([0], the Low band, is always 1)
    std::array<std::array<std::atomic<float>, kMaxBands>, kMaxPasses> lift {};
    // the frequency shifter: its shift now (Hz, gliding; 0 while off) and how far it is faded in (0 .. 1)
    std::atomic<float> shiftHz {0.0f}, shiftAmount {0.0f};
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
    int bandCount () const { return std::lround (p[kBandCount]) == kBands4 ? 4 : 3; }
    double lowXover () const { return state[0].xoverHz[0]; }                     // Hz, now (both passes)
    double xoverHz (int pass, int i) const { return state[pass].xoverHz[i]; }      // Hz, with the drift
    double bandGainDb (int pass, int band) const { return state[pass].gainDb[band]; } // dB, with the movement
    double bandLift (int pass, int band) const { return state[pass].lift[band]; }    // 0 .. 1
    // a moving band's rise and fall times (seconds): the seeded ones x Rise / Fall
    double riseSeconds (int pass, int band) const { return patterns[pass].band[band].rise * p[kRise]; }
    double fallSeconds (int pass, int band) const { return patterns[pass].band[band].fall * p[kFall]; }
    double phase () const { return theta; }
    double glueReductionDb () const { return state[0].glue.gainReductionDb (); }
    const Pattern& pattern (int pass) const { return patterns[pass]; }
    double shiftNow () const { return shiftHz; }       // Hz, gliding to Shift
    double shiftAmount () const { return shiftFade; } // 0 (off, not run) .. 1 (on)

private:
    struct PassState
    {
        dsp::Split4 split[2];
        double gNow[kMaxXovers] {}, gainNow[kMaxBands] {}; // the corners' g and the bands' gains at the tick's start
        double dbNow[kMaxBands] {};                         // the bands' gains (dB) before the off switch
        double xoverHz[kMaxXovers] {}, gainDb[kMaxBands] {}, lift[kMaxBands] {}; // (the meters and the tests)
        dsp::Glue glue;
        dsp::Saturator grit;
        // the frequency shifter (per channel; the oscillator's phase shared)
        dsp::Hilbert hilbert[2];
        dsp::Svf shiftHp[2];
        double shiftPhase = 0.0;
        void resetFilters ();
        void resetShifter ();
    };
    void applyPattern ();
    double cycleSeconds () const; // a movement cycle's length now (Rate, or Sync Rate at the tempo)
    // the targets at theta th for a pass: g per corner and gain per band (snap: no smoothing)
    void targets (int pass, double th, double* g, double* gain, bool snap);
    void runPass (int pass, double* l, double* r, int m, double thetaEnd);

    ParamArray p = defaultParams ();
    double sr = 48000.0;
    Pattern patterns[kMaxPasses];
    PassState state[kMaxPasses];
    dsp::Saturator drive;
    // smoothed settings (per tick)
    double logX[kMaxXovers] {}, levelDb[kMaxBands] {}, share[kMaxBands] {};
    double move = 0.0, depth = 0.0, logRise = 0.0, logFall = 0.0;
    double airOwn = 0.0; // 0: Air follows High (3 bands) .. 1: Air at its own gain (4 bands)
    // the shifter: Shift (Hz) and Shift Mix gliding, and its fade in (0: off, not run) at the tick's start and end
    double shiftHz = 0.0, shiftHzPrev = 0.0, shiftMix = 1.0, shiftMixPrev = 1.0, shiftFade = 0.0, shiftFadePrev = 0.0;
    double secPerCycle = 1.0;
    double tickSmooth = 0.1, gainSmooth = 0.3;
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
