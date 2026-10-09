// Moistr's SWEEP stage (0.24; 0.26: eight bells, Curve, Clean Sub, Sub Boost and Tone), the default sound: at
// the front of the chain, before Drive and the split.
//
//   input -> bells A .. H -> High Shelf -> saturator (level compensated; Clean Sub, Sub Boost) -> Tone
//         -> (the rest of moistr)
//
// Bells A .. H       peaking EQs in series (the RBJ peaking EQ's response: a TPT state-variable filter with
//                    k = 1 / (Q A), out = x + k (A^2 - 1) band-pass, A = 10^(Gain / 40)), each sweeping its
//                    centre between Low and High on a log scale:
//                      m = 0.5 - 0.5 cos (2 pi (theta + Phase / 360)), centre = Low (High / Low)^m
//                    theta is the bell's clock in cycles (Rate, or its Sync Rate at the song tempo), so at
//                    Phase 0 a bell starts at Low. Each has its own switch: off, its gain glides to 0 dB (30 ms)
//                    and then it is not run. The defaults are the "Ocean" recipe (Params.h: kOceanBells).
// High Shelf         the RBJ high shelf with a Q (TPT form: g = tan (pi f / sr) sqrt A, k = 1 / Q,
//                    out = A^2 x + k (1 - A) A band-pass + (1 - A^2) low-pass). A high Q gives a resonant
//                    bump and dip at the corner. Its corner (Low .. High, log) and gain (Min .. Max) go round
//                    an orbit in the (log frequency, gain) plane: a circle at Wander 0; with Wander up its
//                    angle, radius and centre drift on smooth random curves (sums of slow sines, their rates
//                    and phases from Seed), so it never jumps and never quite repeats. It always goes the same
//                    way round (the angle never runs backwards). Tilt lowers its gain ceiling as the corner
//                    rises above 1 kHz (at Tilt 100 % by 3/4 of Max - Min at 5 kHz and above): high
//                    corners turned up sound nasal. Off in a new instance since 0.26.
// Saturator          tanh with first-order antiderivative anti-aliasing (as moistr's Drive and Grit: no
//                    oversampling and so no latency), driven by Drive (dB) on its Curve: Hard tanh (g x), Soft
//                    tanh (0.7 g x) (a gentler knee). Then compensated: a fixed curve of the drive and of the
//                    bells' average gain on a bass (compensation (), bellsAverageDb) at the input's level, so a
//                    sine at that level comes out as loud as it went in. The input's level is a slow RMS
//                    (kLevelSec) of the stage's input, before the bells (both channels: one make-up for both,
//                    so the stereo image stays), so the sweep's own movement never moves the make-up (no
//                    pumping), and it holds through silence (below kLevelGateRms) so a note after a gap starts
//                    at the right level.
// Clean Sub          a Linkwitz-Riley 4th-order split at Split Freq before the saturator: only the band above it
//                    is saturated; the band below goes around it at the saturator's small-signal gain (its
//                    drive x make-up) x Split Level, and the two are summed after it. The split's sides add up
//                    to an all-pass (flat), so with the saturator linear and Split Level at 0 dB the sound is the
//                    same as without it. Split Drive (0 dB: clean) gives the lows a tanh of their own
//                    (tanh (g x) / g, faded in over its first 6 dB) so they can crunch too.
// Sub Boost          the saturator's input low-passed at Sub Freq (Linkwitz-Riley 4th order: two Butterworth
//                    sections) and added after it, the saturated sound kept whole: Sub Level 1 adds it as loud
//                    (RMS) as the saturated signal. Both levels are the mean square since it was switched on
//                    (the first kSubSec seconds), then a slow RMS (kSubSec), held through silence: the match
//                    follows the material, not the sweep or a single note.
// Tone               a 2nd-order Butterworth low-pass after the saturator (the bilinear transform's, TPT form).
//
// Both channels go through the same filters with the same clocks and coefficients, and the make-up is one gain
// for both: the stage never changes the stereo image. The coefficients are worked out every kTick samples (at
// the tick's end) and glide linearly sample by sample in between (no zipper); the TPT filters stay stable
// however they move. Every setting glides over 30 ms. While the host plays the clocks follow the song
// position (synced: locked to it; free: set from it when playback starts or jumps), so a render is the same
// every time. Switching Sweep, High Shelf, Clean Sub, Sub Boost or Tone fades over 20 ms. With Sweep off the
// stage is not run at all (moistr is then 0.23's, bit for bit); with bells C .. H, Clean Sub, Sub Boost and
// Tone off and Curve Hard it is 0.25's, bit for bit.
//
// Taps (0.27, for the gestures' Dirt and Bells targets; only worked out when asked for, so without them the
// stage is exactly as before): two more versions of the stage's output, each level matched to it, from the same
// filters' states and the same Tone corner (with filters of their own):
//   clean  the bells and the High Shelf without the saturator, at the make-up the saturator's input would have
//          (10^(-average bells' gain / 20): about as loud as the saturated sound)
//   bare   the saturator (and Tone) without the bells and the shelf, at its make-up for flat bells
// Clean Sub and Sub Boost (the lows) are left out of both: the taps are only used above the Low crossover.
//
// Loop Lock (0.30, Engine.h: the motion clock): while it is on (setLock), every clock is a pure function of the motion
// clock's beats instead of running on: a synced bell's theta = beats / its Sync Rate's beats, a free one's (and the
// shelf's) beats x 60 / the tempo x its Rate. Off, the clocks run on exactly as before (bit for bit).
// Guard Bells (0.30, with Sub Guard): no bell moves the lows below Sub Guard Freq. The bells' output above the guard's
// corner plus the stage's input below it, through a Linkwitz-Riley 8th-order pair (each side a 4th-order Butterworth
// squared: they add up to an all-pass, flat where the bells are flat; steep, so a bell's large boost near the corner
// hardly leaks under it), and each bell's gain fades to flat as its centre comes down to the corner, from an octave
// above it (guardedBellDb: a smoothstep over that octave). A bell higher up acts as before. The saturator's make-up
// follows (bellsAverageDb with the guard). It fades in and out over 20 ms; off it is not run.
// With Guard Bells the stage also gives Sub Guard its tap (steadyTap): the saturator's input at the gain the saturator
// has on the lows alone (satLowGain: tanh (g x) x make-up against x, x the stage's input below the corner, its RMS over
// kSatLowSec), so the saturator's own movement (its knee pressed harder as the bells rise above the corner) stays out
// of the lows too; with Clean Sub at the gain its lows go around it at; Sub Boost's lows added as they are.
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
// Split Drive: fully in from this (dB; below it the clean lows and the saturated ones are blended)
constexpr double kSplitDriveBlendDb = 6.0;
// Sub Boost: its level matching's RMS time constant (s), and the most it may lift the lows (x)
constexpr double kSubSec = 10.0, kSubMaxGain = 16.0;

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

// Guard Bells: the saturator's gain on the lows alone follows them over this (s)
constexpr double kSatLowSec = 0.3;
// Guard Bells: a bell's gain (dB) at a centre (Hz): flat at the guard's corner and below, all of it from an octave above
double guardedBellDb (double gainDb, double centre, double guardHz);


// the taps' samples for one tick (both channels)
struct SweepTaps
{
    double clean[2][16] {}, bare[2][16] {};
};

class Sweep
{
public:
    static constexpr int kTick = 16;
    static_assert (kTick == 16, "SweepTaps holds a tick");
    static constexpr int kShelfIdx = kNumBells; // the shelf's place in the per-filter arrays (after the bells)
    static constexpr int kFilters = kNumBells + 1;

    Sweep () { orbit.setSeed (1); }
    void prepare (double sampleRate);
    // every setting at its value, the clocks at 0 (p: the parameters, plain)
    void reset (const double* p);
    // where the clocks are for the next block: while the host plays, from the song position (relocate: playback
    // started or jumped, so the free clocks are set from it too); then they run on by n samples per block
    void beginBlock (const double* p, bool playing, bool relocate, double songPpq, double bpm);
    // one tick of m <= kTick samples, in place (taps: also the clean and bare versions, else nullptr)
    void tick (const double* p, double* l, double* r, int m, SweepTaps* taps = nullptr);
    // Loop Lock: the clocks at the motion clock's beats (and the tempo it turns them into seconds with) from the next tick
    // on, or (off) running on by themselves
    void setLock (bool on, double beats, double bpm)
    {
        lockOn = on;
        lockBeats = beats;
        lockBpm = bpm > 1.0 ? bpm : 120.0;
    }
    // Drift (LoopWindow.h): each clock's phase used as theta x factor + offset (per bell, then the shelf); off: as set
    void setDrift (bool on, const double* factor, const double* offset)
    {
        driftOn = on;
        for (int b = 0; on && b < kFilters; ++b)
        {
            driftF[b] = factor[b];
            driftO[b] = offset[b];
        }
    }
    // run at all (on, or fading out): when false, tick does nothing
    bool running (const double* p) const { return p[kSweep] >= 0.5 || fade > 0.0; }

    // for the tests and the display: now (at the last tick's end)
    double bellHz (int b) const { return hz[b]; }
    double bellDb (int b) const { return gainDb[b]; } // (gliding to 0 dB while the bell is off)
    double bellQ (int b) const { return std::exp2 (logQ[b]); }
    bool bellRunning (int b) const { return bellRun[b]; }
    double shelfHz () const { return hz[kShelfIdx]; }
    double shelfDb () const { return shelfGainNow; }
    double shelfQ () const { return std::exp2 (logQ[kShelfIdx]); }
    double shelfCeiling () const { return ceilingNow; }
    double orbitU () const { return uNow; }
    double orbitV () const { return vNow; }
    double clock (int i) const { return theta[i]; } // cycles: bells A .. H, then the shelf
    double amount () const { return fade; }          // 0 .. 1: Sweep faded in
    double shelfAmount () const { return shelfFade; }
    double subAmount () const { return subFade; }   // 0 .. 1: Clean Sub faded in
    double boostAmount () const { return boostFade; } // 0 .. 1: Sub Boost faded in
    double boostUnit () const { return boostNow; }     // Sub Boost's gain at Sub Level 1 now (the RMS match)
    double toneAmount () const { return toneFade; } // 0 .. 1: Tone faded in
    // Sub Guard's tap with Guard Bells (whether the stage made one this tick: running, Guard Bells faded in) and the
    // saturator's gain on the lows alone now
    bool steadyTapped () const { return steadyNow; }
    const double (&steadyTap () const)[2][kTick] { return steady; }
    double satLowGain () const { return satLow; }
    double guardBellsAmount () const { return guardFade; } // 0 .. 1: Guard Bells faded in
    double compensationNow () const { return compNow; }
    double driveGainNow () const { return gNow; } // the saturator's input gain (Drive on its Curve)
    double inputLevel () const { return levelMs > 0.0 ? std::sqrt (levelMs) : 0.0; } // the make-up's input RMS
    // the saturator's make-up for a drive (dB), an average gain before it (dB) and an input level (RMS):
    // inRms / rms (tanh (g x)), x a sine at inRms x 10^(avgDb / 20)
    static double compensation (double driveDb, double avgDb, double inRms = kSweepRefRms);
    // n bells' average gain (dB) on a bass: their gain at kAvgRefHz (power, weighted by kAvgRefWeight),
    // averaged in power over their sweeps (lo, hi: Hz; q: Width). A bell at 0 dB is flat and left out.
    // guardHz (> 0, Guard Bells): each bell's gain as guardedBellDb has it at each place of its sweep
    static double bellsAverageDb (const double* lo, const double* hi, const double* gainDb, const double* q, int n = 2, double guardHz = 0.0);


private:
    struct Coefs
    {
        double g = 0.0, k = 1.0, m0 = 1.0, m1 = 0.0, m2 = 0.0;
    };
    void targets (const double* p, bool snap);

    double sr = 48000.0, tickSmooth = 0.1, fadeStep = 0.01;
    double theta[kFilters] {};
    // gliding settings: per filter (the bells, then the shelf) log2 Low and High and log2 Q; the bells' gains
    // (to 0 dB while off) and phases
    double logLo[kFilters] {}, logHi[kFilters] {}, logQ[kFilters] {}, gainDb[kNumBells] {}, phaseDeg[kNumBells] {};
    bool bellRun[kNumBells] {}; // (a bell at 0 dB at both ends of the tick is flat: not run)
    double shelfMin = -18.0, shelfMax = 6.0, wander = 0.5, tilt = 0.65, driveDb = 18.0;
    double curveDb = 0.0; // the Curve's drive offset (dB): 0 Hard, 20 log10 (0.7) Soft (gliding)
    double uNow = 0.0, vNow = 0.5, shelfGainNow = 0.0, ceilingNow = 0.0;
    double hz[kFilters] {};
    Coefs prev[kFilters], now[kFilters];
    double gPrev = 1.0, gNow = 1.0, compPrev = 1.0, compNow = 1.0, avgNow = 0.0;
    // the taps' gains: clean's make-up (10^(-avgNow / 20)) and bare's (the saturator's for flat bells)
    double cleanPrev = 1.0, cleanNow = 1.0, barePrev = 1.0, bareNow = 1.0;
    static constexpr int kKeySize = 4 * kNumBells + 3;
    double compKey[kKeySize] {}; // (the settings the make-up was worked out for)
    double levelMs = kSweepRefRms * kSweepRefRms, levelCoef = 0.0;
    double fade = 0.0, shelfFade = 0.0, subFade = 0.0, toneFade = 0.0, boostFade = 0.0;
    // Clean Sub (log2 of its crossover; Split Level and Split Drive in dB), Sub Boost (log2 of its corner, Sub
    // Level) and Tone (log2 of its corner), gliding; their filters' g and the gains at the tick's start and end
    double logSplit = 0.0, splitLevelDb = 0.0, splitDriveDb = 0.0, logBoost = 0.0, boostLevel = 0.0, logTone = 0.0;
    double splitGPrev = 0.0, splitGNow = 0.0, boostGPrev = 0.0, boostGNow = 0.0, toneGPrev = 0.0, toneGNow = 0.0;
    double splitLevelPrev = 1.0, splitLevelNow = 1.0, boostPrev = 0.0, boostNow = 0.0;
    // Sub Boost's level match: the slow mean squares of its lows and of the saturated signal (both channels)
    double boostLowMs = 0.0, boostOutMs = 0.0, boostCoef = 0.0;
    bool boostPrimed = false; // (the level match starts from the first sound it hears)
    double boostHeard = 0.0;  // (samples it has heard: a plain mean until kSubSec)
    dsp::Svf filt[kFilters][2], tone[2], boostLp[2][2];
    dsp::Lr4Split split[2];
    dsp::AdaaTanh sat[2], satHigh[2], splitSat[2]; // (sat: all of it; satHigh: above Clean Sub's split; splitSat: below)
    dsp::AdaaTanh satBare[2]; // (the taps: bare's saturator, and Tone for clean and bare)
    dsp::Svf toneClean[2], toneBare[2];
    ShelfOrbit orbit, oldOrbit; // (oldOrbit: fading out after a Seed change while orbitFade < 1)
    // Loop Lock: the clocks from the motion clock's beats; Guard Bells: its fade, its corner (log2, gliding; g at the
    // tick's start and end) and its two splits (the input's lows, the bells' output's highs)
    bool lockOn = false, driftOn = false;
    double driftF[kFilters] {}, driftO[kFilters] {};
    double phaseOf (int b) const { return driftOn ? theta[b] * driftF[b] + driftO[b] : theta[b]; }
    double lockBeats = 0.0, lockBpm = 120.0;
    double guardFade = 0.0, logGuard = 0.0, guardGPrev = 0.0, guardGNow = 0.0;
    dsp::Lr8Split guardIn[2], guardOut[2]; // (only their low and high sides are run)
    double steady[2][kTick] {}, satLow = 1.0, satLowIn = 0.0, satLowOut = 0.0, satLowCoef = 0.01;
    bool steadyNow = false;
    double orbitFade = 1.0, lastBpm = 120.0;
};

} // namespace moistr
