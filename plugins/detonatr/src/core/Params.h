// Detonatr parameters. IDs are persisted in projects: only ever append (a new parameter goes after
// the tail's third block, at a fixed number). This table is new in state version 3: Detonatr was
// rebuilt around the user's explosion chain, and a state from before (version 2 or less) loads as
// the defaults (by the user's choice; see the README).
#pragma once

#include "pluginkit/ParamTable.h"
#include "pluginkit/TailParams.h"

#include <cstdint>

namespace detonatr {

// The stages, in the default order (the user's explosion chain in REAPER).
enum Stage
{
    kStageVocoder = 0,
    kStageSpike,
    kStageMotion,
    kStageTransient1,
    kStageLimiter1,
    kStageTransient2,
    kStageComp1,
    kStageComp2,
    kStageTape,
    kStageLimiter2,
    kNumStages
};
const char* stageName (int stage);      // "Transient 1"
const char* stageShortName (int stage); // for the strip: "Trans 1"

// The fields of the stages that come twice (each instance a block of IDs at its base).
// (plain numbers, so they add to the IDs without mixing enumerations)
constexpr uint32_t kTrOn = 0;
constexpr uint32_t kTrGain = 1; // dB, into the process
constexpr uint32_t kTrThreshold = 2; // dB
constexpr uint32_t kTrDeadband = 3; // dB
constexpr uint32_t kTrRatio = 4; // -1 .. 1
constexpr uint32_t kTrOvershoot = 5; // ms
constexpr uint32_t kTrRise = 6; // ms
constexpr uint32_t kTrRecovery = 7; // ms
constexpr uint32_t kTrOverdrive = 8; // 0 .. 1
constexpr uint32_t kTrOutput = 9; // dB
constexpr uint32_t kTrMix = 10; // 0 .. 1
constexpr uint32_t kTrFields = 11;
// (plain numbers, so they add to the IDs without mixing enumerations)
constexpr uint32_t kLimOn = 0;
constexpr uint32_t kLimGain = 1; // dB
constexpr uint32_t kLimCeiling = 2; // dB
constexpr uint32_t kLimLookahead = 3; // ms
constexpr uint32_t kLimAttack = 4; // ms
constexpr uint32_t kLimRelease = 5; // ms
constexpr uint32_t kLimLink = 6; // 0 .. 1
constexpr uint32_t kLimTruePeak = 7;
constexpr uint32_t kLimFields = 8;
// (plain numbers, so they add to the IDs without mixing enumerations)
constexpr uint32_t kCompOn = 0;
constexpr uint32_t kCompThreshold = 1; // dB
constexpr uint32_t kCompAutoThreshold = 2;
constexpr uint32_t kCompRatio = 3;
constexpr uint32_t kCompAttack = 4; // ms
constexpr uint32_t kCompRelease = 5; // ms
constexpr uint32_t kCompAutoRelease = 6;
constexpr uint32_t kCompKnee = 7; // dB
constexpr uint32_t kCompRange = 8; // dB
constexpr uint32_t kCompHold = 9; // ms
constexpr uint32_t kCompAutoGain = 10;
constexpr uint32_t kCompDry = 11; // dB (-60: none)
constexpr uint32_t kCompXoverLow = 12; // Hz
constexpr uint32_t kCompXoverHigh = 13; // Hz
constexpr uint32_t kCompOutput = 14; // dB
constexpr uint32_t kCompFields = 15;

enum ParamId : uint32_t
{
    kOutput = 0, // dB
    kDryWet,
    kOrder1, // which stage runs 1st .. 10th (a Stage); a stage chosen twice runs once, where it comes
    // first, and a stage left out runs after the others (in the default order)
    kOrderLast = kOrder1 + kNumStages - 1,
    // Vocoder
    kVocOn,
    kVocBands,   // 8 .. 100
    kVocLow,     // Hz
    kVocHigh,    // Hz
    kVocOrder,   // 0 .. 2: one to three filter sections
    kVocAttack,  // ms
    kVocRelease, // ms
    kVocRatio,   // 0 .. 1: the input .. the vocoded signal
    // Spike
    kSpkOn,
    kSpkMode,        // Cut, Boost
    kSpkDepth,       // 0 .. 10
    kSpkSensitivity, // 0 .. 10
    kSpkDecay,       // 0 .. 10
    kSpkSharpness,   // 0 .. 10
    kSpkDecayTilt,   // -10 .. 10
    kSpkLink,        // 0 .. 1
    kSpkLow,         // Hz
    kSpkHigh,        // Hz
    kSpkMix,         // 0 .. 1
    kSpkTrim,        // dB
    // Motion
    kMotOn,
    kMotOrbs,     // 1 .. 16
    kMotPattern,  // Orbit, Swarm
    kMotSpeed,    // m/s
    kMotDistance, // m
    kMotRadius,   // m
    kMotSpread,   // 0 .. 1
    kMotRandom,   // 0 .. 1
    kMotFloor,
    kMotMix, // 0 .. 1
    // the stages that come twice, in chain order
    kTr1Base,
    kLim1Base = kTr1Base + kTrFields,
    kTr2Base = kLim1Base + kLimFields,
    kComp1Base = kTr2Base + kTrFields,
    kComp2Base = kComp1Base + kCompFields,
    // Tape
    kTapeOn = kComp2Base + kCompFields,
    kTapeSplit,     // Hz
    kTapeLowDrive,  // dB
    kTapeLowMix,    // 0 .. 1
    kTapeLowDyn,    // -1 .. 1
    kTapeLowLevel,  // dB
    kTapeHighDrive, // dB
    kTapeHighMix,
    kTapeHighDyn,
    kTapeHighLevel,
    kLim2Base,
    // the Smacheratr at the end of the chain: its three blocks
    kTailBase = kLim2Base + kLimFields,
    kTailExtBase = kTailBase + pk::kTailFields,
    kTailExt2Base = kTailExtBase + pk::kTailExtFields,
    kNumParams = kTailExt2Base + pk::kTailExt2Fields
};
// pinned: IDs are persisted
static_assert (kOrderLast == 11 && kVocOn == 12 && kSpkOn == 20 && kMotOn == 32 && kTr1Base == 42 && kLim1Base == 53 && kTr2Base == 61 &&
                   kComp1Base == 72 && kComp2Base == 87 && kTapeOn == 102 && kLim2Base == 112 && kTailBase == 120 && kTailExtBase == 126 &&
                   kTailExt2Base == 143 && kNumParams == 152,
               "Detonatr's parameter IDs are fixed (version 3's table)");
static_assert (pk::kTailFields == 6 && pk::kTailExtFields == 17 && pk::kTailExt2Fields == 9, "the tail's blocks as they were");

constexpr uint32_t kOrderBase = kOrder1;
constexpr uint32_t kTransientBase[2] = {kTr1Base, kTr2Base};
constexpr uint32_t kLimiterBase[2] = {kLim1Base, kLim2Base};
constexpr uint32_t kCompBase[2] = {kComp1Base, kComp2Base};

constexpr bool isTailParam (uint32_t id) { return id >= kTailBase && id < kNumParams; }
constexpr uint32_t tailField (uint32_t id)
{
    return id >= kTailExt2Base  ? (uint32_t)pk::kTailFields + (uint32_t)pk::kTailExtFields + (id - kTailExt2Base)
           : id >= kTailExtBase ? pk::kTailFields + (id - kTailExtBase)
                                : id - kTailBase;
}

// the On parameter of a stage
uint32_t stageOnParam (int stage);
// the stage a parameter belongs to (-1: the master ones, the order, the tail)
int stageOfParam (uint32_t id);

// Where each stage runs, from the order parameters (see kOrder1).
struct Order
{
    int stage[kNumStages]; // stage[i]: the stage that runs i-th
};
Order resolveOrder (const int chosen[kNumStages]);

const pk::ParamTable& paramTable ();
inline double toPlain (uint32_t id, double n) { return paramTable ().toPlain (id, n); }
inline double toNormalized (uint32_t id, double p) { return paramTable ().toNormalized (id, p); }
inline double defaultNormalized (uint32_t id) { return paramTable ().defaultNormalized (id); }

} // namespace detonatr
