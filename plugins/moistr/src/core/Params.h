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
                   kDensity == 82 && kLowPush == 83 && kLowDip == 84 && kDropOut == 85 && kSpeed == 86 && kNumParams == 87,
               "saved IDs: the multiband split at 69 .. 76, the shifter at 77 .. 79");

enum Slope { kSlope12 = 0, kSlope24 };
enum Passes { kPasses1 = 0, kPasses2 };
enum Bands { kBands3 = 0, kBands4 };

// the Low band's crossover: picked by Seed in this range (Hz)
constexpr double kLowXoverMin = 100.0, kLowXoverMax = 500.0;

// Sync Rate: a movement cycle's length in beats (quarter notes)
constexpr int kNumSyncRates = 6;
constexpr double kSyncBeats[kNumSyncRates] = {16.0, 8.0, 4.0, 2.0, 1.0, 0.5};

constexpr int kMinSeed = 1, kMaxSeed = 128;
constexpr double kLevelOffDb = -48.0; // a band's Level at its minimum: off

const pk::ParamTable& paramTable ();
inline double toPlain (uint32_t id, double n) { return paramTable ().toPlain (id, n); }
inline double toNormalized (uint32_t id, double p) { return paramTable ().toNormalized (id, p); }
inline double defaultNormalized (uint32_t id) { return paramTable ().defaultNormalized (id); }

// Menu > Defaults (pluginkit/GentlrDefaults.h): the parameters Gentlr On by Default and Advanced On by
// Default set in a new instance: the end saturator's Saturator and Gentlr switches and Gentlr's Advanced
inline constexpr pk::GentlrIds kGentlrIds = pk::tailGentlrIds (kTailBase, kTailExtBase, kTailExt2Base);

} // namespace moistr
