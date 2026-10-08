// Moistr: splits a sound (typically a detuned bass) into three or four bands with a phase-coherent
// crossover (Linkwitz-Riley 4th order), keeps the low end locked and lets the bands above it rise and fall
// on a seeded pattern, then glues the bands back together with a compressor and a little soft clipping.
//
//   input -> SWEEP -> Drive -> pass 1 -> [pass 2] -> Mix (dry / wet) -> Output -> Smacheratr (the end saturator)
//
// SWEEP (0.24, Sweep.h; on in a new instance, off in a state saved before 0.24): eight sweeping bell EQs
// (0.26; two before), a High Shelf on a smooth orbit, a level-compensated saturator (Curve, Clean Sub, Sub
// Boost) and Tone, before everything else. A new instance has the rest neutral (Drive, Movement, Glue and Grit
// at 0), so its sound is the sweep alone. With Sweep off the stage is not run (the engine is 0.23's, bit for
// bit).
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
// its Level (with Low Push and Low Dip at 0, the default). Mid, High and Air rise from (Level - Depth x Movement x their Move) to their Level and
// fall back at the seeded moments, with seeded rise and fall times (x Rise, x Fall); the upper crossovers
// drift a little with Movement (Movement.h). At Movement 0 a pass is exactly the static split. The bands
// sum flat when they are at the same gain (an all-pass of the input). Switching between 3 and 4 bands
// fades the Air band from following High to its own gain over 20 ms (the split always has four bands).
//
// 0.22, all off by default (the engine is then 0.21's, bit for bit):
//   Low Push / Low Dip  the Low band's own seeded events push it up to Low Push dB above its Level and dip it
//                       at most Low Dip dB (6 dB at most) below, scaled by Movement. Its crossover stays put.
//   Seed B / Seed Blend a second pattern (with Seed's Low crossover); Blend crossfades Seed's (0) and Seed B's
//                       (1) lifts, overlapping in between (blendLifts: at 0.5 a band is up when either is up).
//                       Changing Seed, Seed B or Density crossfades the old patterns into the new over 100 ms.
//   Density             x0.25 .. x8 the steps per cycle (the events)
//   Speed               divides every rise and fall time (down to 1 ms)
//   Drop Out            the floor of a moving band falls to silence as its fall (Depth x Movement x Move)
//                       nears 48 dB: from kDropFromDb a smooth curve takes the floor's gain to 0 at 48 dB.
//
// 0.23, both off by default (the engine is then 0.22's, bit for bit):
//   Link    how much Mid, High and Air share one stream of events: each band's lift moves towards the Mid band's
//           (the lead) by Link, so at 100 % they open and close together. Each keeps its own Level, Move and
//           Depth (only when it rises and falls is shared). The Low band is not linked.
//   Liquid  a moving resonance on the bands above Low (after their rise and fall, before the shifter): two
//           peaking TPT state-variable filters in series, F1 and F2 of a vowel (Movement.h: LiquidPath), F1
//           gliding between Liquid Low and Liquid High on the movement's clock (Rate or Sync, x Density),
//           with now and then a quick jump. Liquid sets the peaks' height (F1 up to kLiquidMaxDb, F2 to
//           kLiquidF2Db), Liquid Res their Q. With Link, F1 also rises as the bands open (kLinkFollow of its
//           place follows the lead's lift). One path for both passes (Seed's, blended with Seed B's). The
//           Low band never goes through it. At Liquid 0 it is not run.
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
#include "Sweep.h"

#include "smacheratr/src/core/Tail.h"

#include <algorithm>
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
    // Liquid (0.23): its two formants now (Hz; 0 while it is off) and how strong (0 .. 1, as Liquid, gliding)
    std::atomic<float> liquidHz {0.0f}, liquidF2Hz {0.0f}, liquidAmount {0.0f};
    // the SWEEP stage (0.24): the bells' centres and the shelf's corner now (Hz: [0 .. 7] A .. H, [8] the
    // shelf), the bells' gains now (dB: gliding to 0 while off), the shelf's gain now and its ceiling there
    // (dB), and how far the stage and the shelf are faded in (0 .. 1)
    std::array<std::atomic<float>, kNumBells + 1> sweepHz {};
    std::array<std::atomic<float>, kNumBells> bellDb {};
    std::atomic<float> shelfDb {0.0f}, shelfCeiling {0.0f}, sweepAmount {0.0f}, shelfAmount {0.0f};
};

// Liquid: the peaks' height at Liquid 100 % (dB), Liquid Res's Q range, and with Link the share of F1's
// place that follows the bands' opening
constexpr double kLiquidMaxDb = 18.0, kLiquidF2Db = 12.0, kLiquidQMin = 1.5, kLiquidQMax = 12.0, kLinkFollow = 0.5;

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
    // a band's rise and fall times (seconds): the seeded ones x Rise / Fall / Speed (at least kMinRampSec)
    double riseSeconds (int pass, int band) const { return std::max (cur.a[pass].band[band].rise * p[kRise] / p[kSpeed], kMinRampSec); }
    double fallSeconds (int pass, int band) const { return std::max (cur.a[pass].band[band].fall * p[kFall] / p[kSpeed], kMinRampSec); }
    double phase () const { return theta; }
    double glueReductionDb () const { return state[0].glue.gainReductionDb (); }
    const Pattern& pattern (int pass) const { return cur.a[pass]; }   // Seed's
    const Pattern& patternB (int pass) const { return cur.b[pass]; }  // Seed B's (Seed's Low crossover)
    bool crossfading () const { return xfade < 1.0; }                  // from the old Seed / Seed B / Density
    // a band's lift (0 .. 1) at phase th as the engine has it now (Seed Blend, the crossfade, Density, Speed);
    // dips: the Low band's dips instead of its pushes
    double liftAt (int pass, int band, double th, bool dips = false) const;
    double shiftNow () const { return shiftHz; }       // Hz, gliding to Shift
    double shiftAmount () const { return shiftFade; } // 0 (off, not run) .. 1 (on)
    // Liquid: its formants now (Hz, F1 within Liquid Low .. Liquid High), its strength (0: off, not run) and
    // the lead's lift that Link pulls the moving bands towards (pass 0)
    double liquidHz () const { return liqF1; }
    double liquidF2Hz () const { return liqF2; }
    double liquidAmount () const { return liquid; }
    // F1's place (0 .. 1) and log2 (F2 / F1) at phase th, as the engine has them now (Seed Blend, the crossfade)
    void liquidAt (double th, double& pos, double& logRatio) const;
    // the SWEEP stage now (its centres, gains, clocks and orbit)
    const Sweep& sweepStage () const { return sweep; }

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
        // Liquid's two peaks (per channel), and the lead's lift Link pulls towards (this tick)
        dsp::Svf liq1[2], liq2[2];
        double sharedLift = 0.0;
        void resetFilters ();
        void resetShifter ();
        void resetLiquid ();
    };
    // Liquid's filters at a tick's start and end: g of F1 and F2, k (1 / Q) and the peaks' gains - 1
    struct LiquidCoefs
    {
        double g1 = 0.0, g2 = 0.0, k = 1.0, a1 = 0.0, a2 = 0.0;
    };
    // the patterns of Seed and Seed B (both passes) at a Density
    struct PatternSet
    {
        Pattern a[kMaxPasses], b[kMaxPasses];
        int seed = 0, seedB = 0;
        double density = 1.0;
    };
    void applyPattern (bool fade); // (fade: crossfade from the patterns now, while running)
    double setLift (const PatternSet& ps, int pass, int band, double th, bool dips) const;
    double driftAt (int pass, int x, double th) const;
    double cycleSeconds () const; // a movement cycle's length now (Rate, or Sync Rate at the tempo)
    // the targets at theta th for a pass: g per corner and gain per band (snap: no smoothing)
    void targets (int pass, double th, double* g, double* gain, bool snap);
    void runPass (int pass, double* l, double* r, int m, double thetaEnd);
    void liquidTargets (double th, bool snap); // Liquid's filters at th (after pass 0's targets)

    ParamArray p = defaultParams ();
    double sr = 48000.0;
    PatternSet cur, old;  // old: fading out while xfade < 1
    double xfade = 1.0;   // 0 .. 1 over 100 ms (the new patterns' weight: raised cosine)
    bool running = false; // processed since the last reset (a pattern change crossfades)
    PassState state[kMaxPasses];
    Sweep sweep;
    dsp::Saturator drive;
    // smoothed settings (per tick)
    double logX[kMaxXovers] {}, levelDb[kMaxBands] {}, share[kMaxBands] {};
    double move = 0.0, depth = 0.0, logRise = 0.0, logFall = 0.0;
    double blend = 0.0, logSpeed = 0.0, lowPush = 0.0, lowDip = 0.0, dropOut = 0.0; // (0.22; Drop Out fades over 20 ms)
    double riseScale = 1.0, fallScale = 1.0; // this tick's Rise / Speed, Fall / Speed
    double airOwn = 0.0; // 0: Air follows High (3 bands) .. 1: Air at its own gain (4 bands)
    // the shifter: Shift (Hz) and Shift Mix gliding, and its fade in (0: off, not run) at the tick's start and end
    double shiftHz = 0.0, shiftHzPrev = 0.0, shiftMix = 1.0, shiftMixPrev = 1.0, shiftFade = 0.0, shiftFadePrev = 0.0;
    // Link and Liquid (0.23): gliding settings, and Liquid's filters at the tick's start and end
    double link = 0.0, liquid = 0.0, liqRes = 0.5, logLiqLo = 0.0, logLiqHi = 0.0;
    LiquidCoefs liqPrev, liqNow;
    double liqF1 = 0.0, liqF2 = 0.0;
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
