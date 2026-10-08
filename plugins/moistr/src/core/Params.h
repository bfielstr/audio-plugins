// Moistr parameters. IDs are persisted in projects: only ever append.
#pragma once

#include "pluginkit/ParamTable.h"
#include "pluginkit/TailParams.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

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
    // --- added in 0.26 (append only): the saturator's Curve, a Tone low-pass after it, Clean Sub (the lows
    // around the saturator), Sub Boost (the lows added back after it), every bell's own switch and six more
    // bells (C .. H, after B, in series). A new instance is the "Ocean" recipe (kOcean*: eight bells, Curve
    // Soft, Tone 7 kHz, no High Shelf, with its sub option); a state saved before 0.26 reads these at the values
    // that keep its sound (bells C .. H off, Curve Hard, Tone, Clean Sub and Sub Boost off:
    // defaultNormalizedForVersion) ---
    kSweepCurve,  // Hard / Soft: the saturator's curve (Soft: tanh (0.7 g x), a gentler knee)
    kToneOn,      // Off / On: the Tone low-pass after the saturator
    kTone,        // Hz 1000 .. 20000: its corner (2nd-order Butterworth)
    kCleanSub,    // Off / On: the lows (below Split Freq, a Linkwitz-Riley 4th-order split) go around the saturator
    kSplitFreq,   // Hz 40 .. 250: Clean Sub's crossover
    kSplitLevel,  // dB -24 .. +12: the lows against the saturated rest (0 dB: at the saturator's small-signal gain)
    kSplitDrive,  // dB 0 .. 18: a saturation of their own on the lows (0: clean)
    kSubBoost,    // Off / On: the lows before the saturator (a Linkwitz-Riley 4th-order low-pass) added after it
    kSubFreq,     // Hz 40 .. 200: Sub Boost's low-pass
    kSubLevel,    // 0 .. 1: how much (1: as loud, in RMS, as the saturated signal)
    kAOn,         // Off / On: bell A
    kBOn,         // Off / On: bell B
    kCOn,         // bell C: On, then as bell A (Rate, Sync, Sync Rate, Low, High, Gain, Width, Phase)
    kCRate,
    kCSync,
    kCSyncRate,
    kCLow,
    kCHigh,
    kCGain,
    kCWidth,
    kCPhase,
    kDOn,         // bells D .. H: as bell C, 9 IDs each
    kEOn = kDOn + 9,
    kFOn = kEOn + 9,
    kGOn = kFOn + 9,
    kHOn = kGOn + 9,
    // --- added in 0.27 (append only): the gestures (Gesture.h). Four slots, each a breakpoint curve played in
    // time with the song and pulling one target (Target) towards it; Intensity scales them all. Wobble: a tremolo
    // on the bands above Low. All off in a new instance and in a state saved before 0.27 (every Target Off,
    // Wobble Amount 0: moistr as 0.26, bit for bit) ---
    kG1Gesture = kHOn + 9, // slot 1: Gesture, Target, Mode, Length, Speed, Position, Smooth, Depth (kGestureFields)
    kG2Gesture = kG1Gesture + 8, // slots 2 .. 4: as slot 1
    kG3Gesture = kG2Gesture + 8,
    kG4Gesture = kG3Gesture + 8,
    kIntensity = kG4Gesture + 8, // 0 .. 1: scales every slot's Depth
    kWobbleRate,                 // cycles per beat 1 .. 40: the tremolo's rate
    kWobbleAmount,               // 0 .. 1: how deep (0: off, not run)
    // --- added in 0.28 (append only): ONE gesture moving many targets together (Gesture.h: a Scene, one
    // timeline in beats with a lane per target, all on one clock). Replaces the four slots in the editor; the
    // slots (185 .. 217) stay and still play in a state that uses them (a 0.27 project sounds as it did). None in
    // a new instance and in an older state (moistr as 0.27, bit for bit) ---
    kScene = kWobbleAmount + 1, // "Gesture": None, the factory gestures, User (the file picked with File)
    kSceneMode,                 // Loop / Walk
    kSceneLength,               // Own, 1/2 .. 32 beats (as a slot's Length)
    kSceneSpeed,                // Walk's speed: Hold, x1/8 .. x4
    kScenePosition,             // 0 .. 1: where the loop starts (Loop), or where Walk holds
    kSceneSmooth,               // 0 .. 1: 2 ms .. 1/16 beat glides
    kSceneAmount,               // 0 .. 1: scales every lane (0: no gesture)
    // --- added in 0.29 (append only): the LAB (Lab.h). Four chains, each kChainFields IDs (Level, Mute, Solo, Mono,
    // Source and three kept for later): chains 1 .. 3 on the Mid, High and Air bands, chain 4 (in parallel on them
    // all) for later. Then kNumLabSlots effects slots of kLabSlotFields IDs each (a Type, an On and a block, as a
    // smemplr rack slot: smemplr/src/core/FxSlot.h): kChainSlots per chain, chain by chain, then kPostSlots after
    // the bands' sum (POST). Every slot Empty and every chain at 0 dB in an older state and in Init (moistr as
    // 0.28, bit for bit); a new instance starts from the Neuro recipe (newInstanceValues) ---
    kChainBase = kSceneAmount + 1,
    kLabSlotBase = kChainBase + 4 * 8,
    kNumParams = kLabSlotBase + 19 * 88
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
                   kShelfQ == 116 && kShelfWander == 117 && kShelfTilt == 118,
               "saved IDs: the SWEEP stage at 92 .. 118");
static_assert (kSweepCurve == 119 && kToneOn == 120 && kTone == 121 && kCleanSub == 122 && kSplitFreq == 123 && kSplitLevel == 124 &&
                   kSplitDrive == 125 && kSubBoost == 126 && kSubFreq == 127 && kSubLevel == 128 && kAOn == 129 && kBOn == 130 &&
                   kCOn == 131 && kCRate == 132 && kCSync == 133 && kCSyncRate == 134 && kCLow == 135 && kCHigh == 136 && kCGain == 137 &&
                   kCWidth == 138 && kCPhase == 139 && kDOn == 140 && kEOn == 149 && kFOn == 158 && kGOn == 167 && kHOn == 176,
               "saved IDs: Curve, Tone, Clean Sub, Sub Boost, the bells' switches and bells C .. H at 119 .. 184");
static_assert (kG1Gesture == 185 && kG2Gesture == 193 && kG3Gesture == 201 && kG4Gesture == 209 && kIntensity == 217 &&
                   kWobbleRate == 218 && kWobbleAmount == 219,
               "saved IDs: the gestures at 185 .. 219");
static_assert (kScene == 220 && kSceneMode == 221 && kSceneLength == 222 && kSceneSpeed == 223 && kScenePosition == 224 &&
                   kSceneSmooth == 225 && kSceneAmount == 226,
               "saved IDs: the one gesture (0.28) at 220 .. 226");

// the LAB (0.29): its chains and its effects slots
constexpr int kNumChains = 4;      // chains 1 .. 3 on Mid, High and Air; chain 4 kept for later (parallel)
constexpr int kNumBandChains = 3;  // the chains that run now (on the Mid, High and Air bands)
constexpr int kChainSlots = 4;     // effects slots per chain
constexpr int kPostSlots = 3;      // effects slots after the bands' sum (POST)
constexpr int kNumLabSlots = kNumChains * kChainSlots + kPostSlots;
// a chain's parameters by field (offsets from its first): Level, Mute, Solo, Mono; Source and the spares are kept
// for later (not shown, not used)
enum ChainField : uint32_t { kChainLevel = 0, kChainMute, kChainSolo, kChainMono, kChainSource, kChainSpare1, kChainSpare2, kChainSpare3,
                             kChainFields };
// a lab slot's parameters by field: its Type, its On, then its block (kLabBlock + block position; smemplr's SlotField)
constexpr uint32_t kLabType = 0, kLabOn = 1, kLabBlock = 2;
constexpr uint32_t kLabSlotFields = 88; // (smemplr::kSlotFields: Params.cpp checks)
constexpr uint32_t chainId (int chain, ChainField f) { return kChainBase + kChainFields * (uint32_t)chain + (uint32_t)f; }
constexpr int chainSlot (int chain, int k) { return chain * kChainSlots + k; } // a chain's k-th slot
constexpr int postSlot (int k) { return kNumChains * kChainSlots + k; }        // POST's k-th slot
constexpr uint32_t labSlotParam (int slot, uint32_t field) { return kLabSlotBase + kLabSlotFields * (uint32_t)slot + field; }
constexpr uint32_t labBlockParam (int slot, uint32_t j) { return labSlotParam (slot, kLabBlock + j); }
constexpr bool isChainParam (uint32_t id) { return id >= kChainBase && id < kLabSlotBase; }
constexpr bool isLabSlotParam (uint32_t id) { return id >= kLabSlotBase && id < kNumParams; }
constexpr bool isLabParam (uint32_t id) { return id >= kChainBase && id < kNumParams; }
constexpr int labSlotOf (uint32_t id) { return (int)((id - kLabSlotBase) / kLabSlotFields); }   // (a lab slot parameter)
constexpr uint32_t labFieldOf (uint32_t id) { return (id - kLabSlotBase) % kLabSlotFields; }   // (a lab slot parameter)
constexpr bool isChainSpare (uint32_t id) { return isChainParam (id) && (id - kChainBase) % kChainFields >= kChainSource; }
static_assert (kChainBase == 227 && kChainFields == 8 && kLabSlotBase == 259 && kNumLabSlots == 19 && kNumParams == 1931,
               "saved IDs: the LAB's chains at 227 .. 258, its slots at 259 .. 1930");
static_assert (chainId (0, kChainLevel) == 227 && chainId (0, kChainMono) == 230 && chainId (1, kChainLevel) == 235 &&
                   chainId (2, kChainLevel) == 243 && chainId (3, kChainSpare3) == 258,
               "saved IDs: a chain's Level, Mute, Solo, Mono, Source and three spares");
static_assert (labSlotParam (chainSlot (0, 0), kLabType) == 259 && labSlotParam (chainSlot (0, 1), kLabType) == 347 &&
                   labSlotParam (chainSlot (1, 0), kLabType) == 611 && labSlotParam (chainSlot (2, 0), kLabType) == 963 &&
                   labSlotParam (postSlot (0), kLabType) == 1667 && labBlockParam (postSlot (2), 85) == 1930,
               "saved IDs: the slots, kLabSlotFields each (Type, On, 86 block positions), chain by chain, then POST");
// a slot's Type: Empty, then smemplr's kinds (smemplr::FxType, in its order), with room for kinds still to come (their
// names "Kind 10" .. are placeholders; the engine runs them as Empty). The choice's length is saved: never change it.
constexpr int kLabKinds = 32;
// the chains' names (their parameters' and the LAB's)
constexpr const char* kChainNames[kNumChains] = {"Mid", "High", "Air", "Chain 4"};

// the SWEEP stage's eight bells (A .. H, in series in that order). A and B keep their IDs from 0.24 (Rate ..
// Phase at kARate / kBRate, their switches at kAOn / kBOn); C .. H are On, Rate .. Phase from kCOn.
constexpr int kNumBells = 8;
// a bell's parameters by field: rate, sync, sync rate, low, high, gain, width, phase (offsets from its Rate)
enum BellField { kBellRate = 0, kBellSync, kBellSyncRate, kBellLow, kBellHigh, kBellGain, kBellWidth, kBellPhase };
constexpr uint32_t bellRateId (int b) { return b == 0 ? kARate : b == 1 ? kBRate : kCRate + 9u * (uint32_t)(b - 2); }
constexpr uint32_t bellId (int b, BellField f) { return bellRateId (b) + (uint32_t)f; }
constexpr uint32_t bellOnId (int b) { return b == 0 ? kAOn : b == 1 ? kBOn : kCOn + 9u * (uint32_t)(b - 2); }
static_assert (bellId (7, kBellPhase) == kG1Gesture - 1 && bellOnId (2) == kCOn && bellId (3, kBellRate) == kDOn + 1, "the bells' IDs");
enum SweepCurve { kCurveHard = 0, kCurveSoft };

// the gestures (0.27): four slots of eight parameters each, by field (offsets from the slot's Gesture)
constexpr int kNumGestureSlots = 4;
enum GestureField { kGestureChoice = 0, kGestureTarget, kGestureMode, kGestureLength, kGestureSpeed, kGesturePosition, kGestureSmooth,
                    kGestureDepth, kGestureFields };
constexpr uint32_t gestureId (int slot, GestureField f) { return kG1Gesture + (uint32_t)kGestureFields * (uint32_t)slot + (uint32_t)f; }
static_assert (gestureId (3, kGestureDepth) == kIntensity - 1, "the gesture slots' IDs");
constexpr bool isGestureParam (uint32_t id) { return id >= kG1Gesture && id <= kSceneAmount; }
constexpr bool isSlotParam (uint32_t id) { return id >= kG1Gesture && id <= kIntensity; } // (the 0.27 slots and Intensity)
constexpr bool isSceneParam (uint32_t id) { return id >= kScene && id <= kSceneAmount; }
// what a slot pulls (Target). The Low band is never a target: the sub stays steady.
enum GestureTarget
{
    kTargetOff = 0,
    kTargetMidLevel,     // the Mid band's level (1: at its Level, 0: silent)
    kTargetHighLevel,    // the High band's
    kTargetAirLevel,     // the Air band's (4 bands; with 3 it follows High)
    kTargetWobbleRate,   // Wobble's rate (0 .. 1 over 1 .. 40 cycles per beat, log)
    kTargetWobbleAmount, // Wobble's depth
    kTargetClose,        // a resonant low-pass on the bands above Low (1: open at Tone's corner, 0: closed, resonant)
    kTargetLiquid,       // Liquid's place (0: Liquid Low .. 1: Liquid High)
    kTargetDirt,         // the bands above Low between the SWEEP stage's saturated sound (1) and its clean one (0)
    kTargetBells,        // the bands above Low with (1) and without (0) the SWEEP stage's bells (and High Shelf)
    kTargetMidX,         // Mid X over its range
    kTargetHighX,        // High X over its range
    kTargetSeedBlend,    // Seed Blend
    kTargetShift,        // Shift over its range (-500 .. +500 Hz; Shift On must be on)
    kNumTargets
};
// the targets' names (the 0.27 Target choice's entries; a gesture file's lane "target")
constexpr const char* kTargetNames[kNumTargets] = {"Off",        "Mid Level", "High Level", "Air Level", "Wobble Rate",
                                                   "Wobble Amount", "Close",  "Liquid Pos", "Dirt",      "Bells",
                                                   "Mid X",      "High X",    "Seed Blend", "Shift"};
enum GestureMode { kModeLoop = 0, kModeWalk };
// Length: the gesture's own (0), else a loop's length in beats; Speed (Walk): Hold, then x1/8 .. x4
constexpr int kNumGestureLengths = 8, kNumGestureSpeeds = 7;
constexpr double kGestureLengthBeats[kNumGestureLengths] = {0.0, 0.5, 1.0, 2.0, 4.0, 8.0, 16.0, 32.0};
constexpr double kGestureSpeeds[kNumGestureSpeeds] = {0.0, 0.125, 0.25, 0.5, 1.0, 2.0, 4.0};
constexpr double kWobbleRateMin = 1.0, kWobbleRateMax = 40.0;
constexpr double kSoftCurve = 0.7; // Soft: the saturator sees 0.7 x the drive (tanh (0.7 g x))

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
// bells C .. H reach higher than A and B (whose ranges are fixed by saved projects): 20 Hz .. 8 kHz; Tone's range
constexpr double kBellFreqMaxWide = 8000.0, kToneMin = 1000.0, kToneMax = 20000.0;
// The "Ocean" recipe (0.26's default sound), per bell A .. H: rate (Hz), low and high (Hz), gain (dB), Q and
// phase (radians; the Phase parameter is in degrees). (The gains are the first eight-bell recipe's x 1.3.)
struct BellRecipe
{
    double rate, low, high, gainDb, q, phaseRad;
};
constexpr BellRecipe kOceanBells[kNumBells] = {{0.70, 20.0, 120.0, 23.4, 0.5, 0.0},   {0.77, 30.0, 300.0, -23.4, 0.5, 1.1},
                                               {0.53, 80.0, 600.0, 11.7, 0.6, 2.3},   {0.91, 150.0, 1200.0, -11.7, 0.6, 0.7},
                                               {0.41, 250.0, 2000.0, 7.8, 0.7, 3.9},  {1.13, 400.0, 3000.0, -10.4, 0.7, 5.1},
                                               {0.63, 60.0, 450.0, -7.8, 0.5, 4.4},   {0.84, 200.0, 1600.0, 9.1, 0.6, 2.9}};
// the rest of it: Sweep Drive (dB, Curve Soft) and Tone (Hz); the sub options' settings (Clean Sub: Split Freq
// in Hz, Split Level in dB, Split Drive in dB; Sub Boost: Sub Freq in Hz, Sub Level 0 .. 1)
constexpr double kOceanDriveDb = 14.0, kOceanToneHz = 7000.0;
constexpr double kOceanSplitHz = 100.0, kOceanSplitLevelDb = -11.5, kOceanSplitDriveDb = 0.0, kOceanSubHz = 70.0, kOceanSubLevel = 0.7;
// which sub option the Ocean recipe has on (none: the full crunch; Clean Sub: the lows around the saturator;
// Sub Boost: the lows added back after it). The one line that picks the default sound's low end.
enum class OceanSub { None, CleanSub, SubBoost };
constexpr OceanSub kOceanSub = OceanSub::SubBoost;
constexpr double kSplitFreqMin = 40.0, kSplitFreqMax = 250.0, kSplitLevelMin = -24.0, kSplitLevelMax = 12.0, kSplitDriveMax = 18.0;
constexpr double kSubFreqMin = 40.0, kSubFreqMax = 200.0;

const pk::ParamTable& paramTable ();
inline double toPlain (uint32_t id, double n) { return paramTable ().toPlain (id, n); }
inline double toNormalized (uint32_t id, double p) { return paramTable ().toNormalized (id, p); }
inline double defaultNormalized (uint32_t id) { return paramTable ().defaultNormalized (id); }
// The default a parameter had before 0.24, for a state saved before then that lacks it (State.cpp): 0.24 made
// the SWEEP stage the default sound (Sweep on) and the rest neutral (Drive, Movement, Glue and Grit at 0).
double legacyDefaultNormalized (uint32_t id);
// What a state saved with a version (State.cpp's: 1, 2 before 0.24; 3, 4 0.24 and 0.25; 5 0.26; 6 0.27; 7 0.28; 8 0.29) reads for a
// parameter it lacks: before 0.24 legacyDefaultNormalized; 0.24 and 0.25 their defaults (Sweep Drive 18 dB,
// High Shelf on, A and B at Width 0.71 and Phase 0, so before the Ocean recipe) with the 0.26 parameters at the
// values that leave the sound as it was (bells C .. H off, Curve Hard, Tone, Clean Sub and Sub Boost off);
// from 0.26 the defaults; before 0.27 the gestures off (gestureOffNormalized); before 0.28 the one gesture None (its
// default); before 0.29 the LAB's defaults (every slot Empty, every chain at 0 dB: the sound before it).
double defaultNormalizedForVersion (uint32_t id, int version);
// The gestures as a state saved before 0.27 reads them: every Target Off, Wobble Amount 0 (the rest at the defaults)
double gestureOffNormalized (uint32_t id);
// the defaults from 0.24 to 0.25 (the SWEEP stage before the Ocean recipe; the 0.26 parameters as above)
double defaultNormalized025 (uint32_t id);

// A lab slot's name ("Mid FX 1", "Post FX 2") and the kind it is meant for (smemplr::FxType: a chain's first slot
// Smacheratr, its second Multidyn, POST's first Multidyn and its second Smacheratr, the others Empty): its block's
// defaults are that kind's.
std::string labSlotName (int slot);
int labSlotKind (int slot);

// The Neuro recipe (0.29): what a new instance starts from, over the defaults (normalized values by ID). Init and an
// older state keep the defaults (the LAB empty: the Ocean sound); a new instance with no saved default gets these
// (the factory preset Neuro/Neuro is the same). Four bands, each above Low driven into a hard-clipping smacheratr and
// an OTT (multidyn) in its chain, an OTT and a hard clipper on their sum in POST, a quarter-note Wobble after it, the
// bands moving in time with the song; the Low band (below 146 Hz, Seed 2) clean.
std::vector<std::pair<uint32_t, double>> newInstanceValues ();

// Menu > Defaults (pluginkit/GentlrDefaults.h): the parameters Gentlr On by Default and Advanced On by
// Default set in a new instance: the end saturator's Saturator and Gentlr switches and Gentlr's Advanced
inline constexpr pk::GentlrIds kGentlrIds = pk::tailGentlrIds (kTailBase, kTailExtBase, kTailExt2Base);

} // namespace moistr
