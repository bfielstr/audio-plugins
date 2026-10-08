// Moistr parameters. IDs are persisted in projects: only ever append.
#pragma once

#include "pluginkit/ParamTable.h"
#include "pluginkit/TailParams.h"

#include <cstdint>

namespace moistr {

enum ParamId : uint32_t
{
    // INPUT
    kDrive = 0, // 0 .. 1: light saturation before the split (0: untouched)
    // BANDS
    kLowFreq,   // Hz: the Low band's low-pass
    kLowRes,    // 0 .. 1: its resonance (Q 0.5 .. 12)
    kLowLevel,  // dB (the minimum: off)
    kMidFreq,   // Hz: the Mid band's band-pass (the low mids)
    kMidRes,    // 0 .. 1
    kMidLevel,  // dB
    kHighFreq,  // Hz: the High band's high-pass
    kHighRes,   // 0 .. 1
    kHighLevel, // dB
    kGap,       // -1 .. 1: moves Mid down and High up (right) or both towards each other (left), +-1 octave each
    kSlope,     // 12 dB / 24 dB per octave
    // MOVEMENT
    kMovement,  // 0 .. 1: how far every band's frequency and level drift (0: still)
    kRate,      // Hz: how fast (free)
    kSync,      // Off / On: the rate from the song tempo (Sync Rate) instead
    kSyncRate,  // a cycle's length in beats (choice)
    kLowMove,   // 0 .. 1: the Low band's share of the movement
    kMidMove,   // 0 .. 1
    kHighMove,  // 0 .. 1
    kLevelMove, // dB: how far a band's level moves at full movement
    kSeed,      // 1 .. 128: the movement's pattern
    // GLUE
    kGlue,   // 0 .. 1: the compressor after the bands (threshold and ratio together, makeup automatic)
    kGrit,   // 0 .. 1: soft clipping after it
    kPasses, // 1 / 2: the second pass runs the result through the bands again with its own movement
    // OUTPUT
    kMix,    // 0 .. 1: dry .. wet
    kOutput, // dB
    // the Smacheratr at the end of the chain (the suite's end saturator), every block of it
    kTailBase,
    kTailExtBase = kTailBase + pk::kTailFields,
    kTailExt2Base = kTailExtBase + pk::kTailExtFields,
    kTailExt3Base = kTailExt2Base + pk::kTailExt2Fields,
    kTailExt4Base = kTailExt3Base + pk::kTailExt3Fields,
    // --- added in 0.19 (append only): the multiband split. The bands are now a crossover split (Low, Mid,
    // High and, with 4 bands, Air) instead of three separate filters. The Low band is locked (it never
    // moves); the others rise and fall on a seeded pattern. Seed picks when each band rises and falls,
    // how quickly, and the Low band's crossover (100 .. 500 Hz). Unused from 0.19 (kept for old
    // projects): kLowFreq, kLowRes, kMidFreq, kMidRes, kHighFreq, kHighRes, kGap, kSlope, kLowMove, kLevelMove ---
    kBandCount = kTailExt4Base + pk::kTailExt4Fields, // 3 / 4 bands
    kXoverMid,  // Hz: the crossover above Mid (3 bands: Mid | High; 4 bands: Mid | High)
    kXoverHigh, // Hz: the crossover above High (4 bands only: High | Air)
    kAirLevel,  // dB: the Air band's level (4 bands)
    kAirMove,   // 0 .. 1: the Air band's share of the movement
    kRise,      // x0.25 .. x4: scales the seeded rise times
    kFall,      // x0.25 .. x4: scales the seeded fall times
    kDepth,     // dB: how far a moving band falls below its Level
    // the frequency shifter on the bands above Low (the sub frequencies are never shifted)
    kShiftOn,   // Off / On (off by default: moistr as without it)
    kShift,     // Hz: -500 .. +500, up or down
    kShiftMix,  // 0 .. 1: the shifted bands against the unshifted ones
    // --- added in 0.22 (append only): a second seed to blend with, more extreme movement, and a Low band
    // that may come forward (and dip back only a little) ---
    kSeedB,     // 1 .. 128: the second movement pattern
    kSeedBlend, // 0 .. 1: Seed only (0) .. both overlapping (0.5) .. Seed B only (1)
    kDensity,   // x0.25 .. x8: how many rises and falls per cycle
    kLowPush,   // dB 0 .. 12: how far the Low band may rise above its Level on its own events (0: locked)
    kLowDip,    // dB 0 .. 6: how far the Low band may fall below its Level (0: never)
    kDropOut,   // Off / On: a full Depth falls to silence
    kSpeed,     // x1 .. x16: divides every rise and fall time (down to about 1 ms)
    // --- added in 0.23 (append only): the moving bands rising and falling together, and a moving resonance
    // on the bands above Low (both off by default: moistr as 0.22, bit for bit) ---
    kLink,       // 0 .. 1: how much Mid, High and Air share one stream of events (the Mid band's): 1, all together
    kLiquid,     // 0 .. 1: a moving resonance (two formants, a gliding vowel) on the bands above Low (0: off)
    kLiquidRes,  // 0 .. 1: its resonance (Q 1.5 .. 12)
    kLiquidLow,  // Hz: the lowest the resonance goes
    kLiquidHigh, // Hz: the highest
    // --- added in 0.24 (append only): the SWEEP stage at the front of the chain (before Drive and the split):
    // two sweeping bell EQs (A, then B), a sweeping High Shelf on a smooth orbit, then a saturator with
    // automatic level compensation (Sweep.h). On in a new instance; a state saved before 0.24 reads it as off
    // (legacyDefaultNormalized), so old projects and presets sound as they did ---
    kSweep,       // Off / On: the whole stage
    kSweepDrive,  // dB 0 .. 36: the saturator after the sweep (level compensated)
    kARate,       // Hz: bell A's sweep rate (free)
    kASync,       // Off / On: bell A's rate from the song tempo (A Sync Rate)
    kASyncRate,   // a sweep cycle's length in beats (choice, as Sync Rate)
    kALow,        // Hz: the lowest bell A's centre goes
    kAHigh,       // Hz: the highest
    kAGain,       // dB -24 .. +24: bell A's gain
    kAWidth,      // Q 0.2 .. 10: bell A's width (low: broad)
    kAPhase,      // degrees 0 .. 360: where in its sweep bell A starts (0: at Low)
    kBRate,       // bell B: as bell A
    kBSync,
    kBSyncRate,
    kBLow,
    kBHigh,
    kBGain,
    kBWidth,
    kBPhase,
    kShelf,       // Off / On: the High Shelf (after the bells, before the saturator)
    kShelfRate,   // Hz: how fast it goes round its orbit
    kShelfLow,    // Hz: the lowest its corner goes
    kShelfHigh,   // Hz: the highest
    kShelfMin,    // dB: its lowest gain
    kShelfMax,    // dB: its highest gain
    kShelfQ,      // 0.3 .. 24: its resonance at the corner
    kShelfWander, // 0 .. 1: 0 a perfect circle, 1 a loose, smooth random orbit (Seed's)
    kShelfTilt,   // 0 .. 1: lowers the gain ceiling as the corner rises above 1 kHz (less nasal)
    kNumParams
};

// pinned: these numbers are in saved projects
static_assert (kDrive == 0 && kLowFreq == 1 && kLowRes == 2 && kLowLevel == 3 && kMidFreq == 4 && kMidRes == 5 && kMidLevel == 6 &&
                   kHighFreq == 7 && kHighRes == 8 && kHighLevel == 9 && kGap == 10 && kSlope == 11,
               "Moistr's parameter IDs are fixed");
static_assert (kMovement == 12 && kRate == 13 && kSync == 14 && kSyncRate == 15 && kLowMove == 16 && kMidMove == 17 &&
                   kHighMove == 18 && kLevelMove == 19 && kSeed == 20 && kGlue == 21 && kGrit == 22 && kPasses == 23 &&
                   kMix == 24 && kOutput == 25 && kTailBase == 26,
               "Moistr's parameter IDs are fixed");
static_assert (kTailExtBase == 32 && kTailExt2Base == 49 && kTailExt3Base == 58 && kTailExt4Base == 64,
               "saved IDs: the end saturator's blocks at 26 .. 68");
static_assert (kBandCount == 69 && kXoverMid == 70 && kXoverHigh == 71 && kAirLevel == 72 && kAirMove == 73 && kRise == 74 &&
                   kFall == 75 && kDepth == 76 && kShiftOn == 77 && kShift == 78 && kShiftMix == 79 && kSeedB == 80 && kSeedBlend == 81 &&
                   kDensity == 82 && kLowPush == 83 && kLowDip == 84 && kDropOut == 85 && kSpeed == 86,
               "saved IDs: the multiband split at 69 .. 76, the shifter at 77 .. 79, the 0.22 controls at 80 .. 86");
static_assert (kLink == 87 && kLiquid == 88 && kLiquidRes == 89 && kLiquidLow == 90 && kLiquidHigh == 91,
               "saved IDs: Link and Liquid at 87 .. 91");
static_assert (kSweep == 92 && kSweepDrive == 93 && kARate == 94 && kASync == 95 && kASyncRate == 96 && kALow == 97 && kAHigh == 98 &&
                   kAGain == 99 && kAWidth == 100 && kAPhase == 101 && kBRate == 102 && kBSync == 103 && kBSyncRate == 104 &&
                   kBLow == 105 && kBHigh == 106 && kBGain == 107 && kBWidth == 108 && kBPhase == 109 && kShelf == 110 &&
                   kShelfRate == 111 && kShelfLow == 112 && kShelfHigh == 113 && kShelfMin == 114 && kShelfMax == 115 &&
                   kShelfQ == 116 && kShelfWander == 117 && kShelfTilt == 118 && kNumParams == 119,
               "saved IDs: the SWEEP stage at 92 .. 118");

enum Slope { kSlope12 = 0, kSlope24 };
enum Passes { kPasses1 = 0, kPasses2 };
enum Bands { kBands3 = 0, kBands4 };

// the Low band's crossover: picked by Seed in this range (Hz)
constexpr double kLowXoverMin = 100.0, kLowXoverMax = 500.0;

// Sync Rate: a movement cycle's length in beats (quarter notes)
constexpr int kNumSyncRates = 6;
constexpr double kSyncBeats[kNumSyncRates] = {16.0, 8.0, 4.0, 2.0, 1.0, 0.5};

constexpr int kMinSeed = 1, kMaxSeed = 128;
// Liquid Low and Liquid High's ranges (Hz)
constexpr double kLiquidLowMin = 150.0, kLiquidLowMax = 800.0, kLiquidHighMin = 600.0, kLiquidHighMax = 4000.0;
constexpr double kLevelOffDb = -48.0; // a band's Level at its minimum: off
// the SWEEP stage's ranges: the bells' and the shelf's rates (Hz), the bells' centres (Hz) and widths (Q),
// the shelf's Q, the saturator's drive (dB)
constexpr double kSweepRateMin = 0.05, kSweepRateMax = 8.0, kBellFreqMin = 20.0, kBellFreqMax = 2000.0;
constexpr double kBellQMin = 0.2, kBellQMax = 10.0, kBellQDefault = 0.71, kShelfQMin = 0.3, kShelfQMax = 24.0, kSweepDriveMax = 36.0;

const pk::ParamTable& paramTable ();
inline double toPlain (uint32_t id, double n) { return paramTable ().toPlain (id, n); }
inline double toNormalized (uint32_t id, double p) { return paramTable ().toNormalized (id, p); }
inline double defaultNormalized (uint32_t id) { return paramTable ().defaultNormalized (id); }
// The default a parameter had before 0.24, for a state saved before then that lacks it (State.cpp): 0.24 made
// the SWEEP stage the default sound (Sweep on) and the rest neutral (Drive, Movement, Glue and Grit at 0).
double legacyDefaultNormalized (uint32_t id);

// Menu > Defaults (pluginkit/GentlrDefaults.h): the parameters Gentlr On by Default and Advanced On by
// Default set in a new instance: the end saturator's Saturator and Gentlr switches and Gentlr's Advanced
inline constexpr pk::GentlrIds kGentlrIds = pk::tailGentlrIds (kTailBase, kTailExtBase, kTailExt2Base);

} // namespace moistr
