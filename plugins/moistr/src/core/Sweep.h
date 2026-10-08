// Moistr's SWEEP stage (0.24), the default sound: at the front of the chain, before Drive and the split.
//
//   input -> bell A -> bell B -> High Shelf -> saturator (level compensated) -> (the rest of moistr)
//
// Bell A and bell B  peaking EQs (the RBJ peaking EQ's response: a TPT state-variable filter with
//                    k = 1 / (Q A), out = x + k (A^2 - 1) band-pass, A = 10^(Gain / 40)), each sweeping its
//                    centre between Low and High on a log scale:
//                      m = 0.5 - 0.5 cos (2 pi (theta + Phase / 360)), centre = Low (High / Low)^m
//                    theta is the bell's clock in cycles (Rate, or its Sync Rate at the song tempo), so at
//                    Phase 0 a bell starts at Low. The defaults are the recipe: A +18 dB from 20 to 120 Hz at
//                    0.70 Hz, B -18 dB from 30 to 300 Hz at 0.77 Hz, both broad (Q 0.71).
// High Shelf         the RBJ high shelf with a Q (TPT form: g = tan (pi f / sr) sqrt A, k = 1 / Q,
//                    out = A^2 x + k (1 - A) A band-pass + (1 - A^2) low-pass). A high Q gives a resonant
//                    bump and dip at the corner. Its corner (Low .. High, log) and gain (Min .. Max) go round
//                    an orbit in the (log frequency, gain) plane: a circle at Wander 0; with Wander up its
//                    angle, radius and centre drift on smooth random curves (sums of slow sines, their rates
//                    and phases from Seed), so it never jumps and never quite repeats. It always goes the same
//                    way round (the angle never runs backwards). Tilt lowers its gain ceiling as the corner
//                    rises above 1 kHz (at Tilt 100 % by 3/4 of Max - Min at 5 kHz and above): high
//                    corners turned up sound nasal.
// Saturator          tanh with first-order antiderivative anti-aliasing (as moistr's Drive and Grit: no
//                    oversampling and so no latency; a bass's harmonics fall off fast enough that the
//                    residual aliasing at 18 dB stays far down), driven by Drive (dB), then compensated: a
//                    fixed curve of Drive and of the bells' average gain on a bass (compensation (),
//                    bellsAverageDb) at the input's level, so a sine at that level comes out as loud as it
//                    went in. The input's level is a slow RMS (kLevelSec) of the stage's input, before the
//                    bells, so the sweep's own movement never moves the make-up (no pumping), and it holds
//                    through silence (below kLevelGateRms) so a note after a gap starts at the right level.
//
// The coefficients are worked out every kTick samples (at the tick's end) and glide linearly sample by sample
// in between (no zipper); the TPT filters stay stable however they move. Every setting glides over 30 ms.
// While the host plays the clocks follow the song position (synced: locked to it; free: set from it when
// playback starts or jumps), so a render is the same every time. Switching Sweep or High Shelf fades over
// 20 ms; with Sweep off the stage is not run at all (moistr is then 0.23's, bit for bit).
#pragma once

#include "Dsp.h"
#include "Params.h"

#include <cmath>

namespace moistr {

// the saturator's level compensation: the reference level (RMS), and the bass the bells' average gain is
// taken on (bellsAverageDb: a low note's first harmonics, weighted as a saw's)
constexpr double kSweepRefRms = 0.25;
// the input level the make-up is worked out for: a slow RMS (time constant, s), held below the gate (RMS), and
// kept within these (RMS)
constexpr double kLevelSec = 0.8, kLevelGateRms = 0.001, kLevelMinRms = 0.003, kLevelMaxRms = 1.5;
constexpr double kAvgRefHz[3] = {40.0, 80.0, 160.0}, kAvgRefWeight[3] = {1.0, 0.25, 0.0625};
// Tilt: where the ceiling starts to fall (Hz) and where it has fallen all the way, and how far (of Max - Min)
constexpr double kTiltFromHz = 1000.0, kTiltToHz = 5000.0, kTiltDepth = 0.75;

// The High Shelf's gain ceiling (dB) at a corner `hz`: Max up to 1 kHz, then down smoothly (log frequency)
// by Tilt x kTiltDepth x (Max - Min) at 5 kHz.
double shelfCeilingDb (double hz, double minDb, double maxDb, double tilt);

// The High Shelf's orbit: where it is (u: 0 .. 1 over Low .. High on a log scale; v: 0 .. 1 over Min .. the
// ceiling) at theta (cycles of Shelf Rate), for a Wander. Seed picks the smooth random curves.
struct ShelfOrbit
{
    // four smooth random curves (angle, radius, centre u, centre v), each three slow sines
    double rate[4][3] {}, phase[4][3] {};
    int seed = 0;
    void setSeed (int s);
    double curve (int which, double th) const; // -1 .. 1
    void at (double th, double wander, double& u, double& v) const;
};

class Sweep
{
public:
    static constexpr int kTick = 16;

    Sweep () { orbit.setSeed (1); }
    void prepare (double sampleRate);
    // every setting at its value, the clocks at 0 (p: the parameters, plain)
    void reset (const double* p);
    // where the clocks are for the next block: while the host plays, from the song position (relocate: playback
    // started or jumped, so the free clocks are set from it too); then they run on by n samples per block
    void beginBlock (const double* p, bool playing, bool relocate, double songPpq, double bpm);
    // one tick of m <= kTick samples, in place
    void tick (const double* p, double* l, double* r, int m);
    // run at all (on, or fading out): when false, tick does nothing
    bool running (const double* p) const { return p[kSweep] >= 0.5 || fade > 0.0; }

    // for the tests and the display: now (at the last tick's end)
    double bellHz (int b) const { return hz[b]; }
    double bellDb (int b) const { return gainDb[b]; }
    double bellQ (int b) const { return std::exp2 (logQ[b]); }
    double shelfHz () const { return hz[2]; }
    double shelfDb () const { return shelfGainNow; }
    double shelfQ () const { return std::exp2 (logQ[2]); }
    double shelfCeiling () const { return ceilingNow; }
    double orbitU () const { return uNow; }
    double orbitV () const { return vNow; }
    double clock (int i) const { return theta[i]; } // cycles: bell A, bell B, the shelf
    double amount () const { return fade; }          // 0 .. 1: Sweep faded in
    double shelfAmount () const { return shelfFade; }
    double compensationNow () const { return compNow; }
    double inputLevel () const { return levelMs > 0.0 ? std::sqrt (levelMs) : 0.0; } // the make-up's input RMS
    // the saturator's make-up for a drive (dB), an average gain before it (dB) and an input level (RMS):
    // inRms / rms (tanh (g x)), x a sine at inRms x 10^(avgDb / 20)
    static double compensation (double driveDb, double avgDb, double inRms = kSweepRefRms);
    // the two bells' average gain (dB) on a bass: their gain at kAvgRefHz (power, weighted by kAvgRefWeight),
    // averaged in power over their sweeps (lo, hi: Hz; q: Width)
    static double bellsAverageDb (const double* lo, const double* hi, const double* gainDb, const double* q);

private:
    struct Coefs
    {
        double g = 0.0, k = 1.0, m0 = 1.0, m1 = 0.0, m2 = 0.0;
    };
    void targets (const double* p, bool snap);

    double sr = 48000.0, tickSmooth = 0.1, fadeStep = 0.01;
    double theta[3] {};
    // gliding settings: per filter (A, B, shelf) log2 Low and High and log2 Q; the bells' gains and phases
    double logLo[3] {}, logHi[3] {}, logQ[3] {}, gainDb[2] {}, phaseDeg[2] {};
    double shelfMin = -18.0, shelfMax = 6.0, wander = 0.5, tilt = 0.65, driveDb = 18.0;
    double uNow = 0.0, vNow = 0.5, shelfGainNow = 0.0, ceilingNow = 0.0;
    double hz[3] {};
    Coefs prev[3], now[3];
    double gPrev = 1.0, gNow = 1.0, compPrev = 1.0, compNow = 1.0, avgNow = 0.0;
    double compKey[10] {}; // (the settings the make-up was worked out for)
    double levelMs = kSweepRefRms * kSweepRefRms, levelCoef = 0.0;
    double fade = 0.0, shelfFade = 0.0;
    dsp::Svf filt[3][2];
    dsp::AdaaTanh sat[2];
    ShelfOrbit orbit, oldOrbit; // (oldOrbit: fading out after a Seed change while orbitFade < 1)
    double orbitFade = 1.0, lastBpm = 120.0;
};

} // namespace moistr
