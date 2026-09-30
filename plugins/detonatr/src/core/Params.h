// Detonatr parameters. IDs are persisted in projects: only ever append. The Saturator stage's
// extended block comes last, so it can grow; a new Detonatr parameter goes in a block after it (and
// from then on the extended block stays as it is).
#pragma once

#include "pluginkit/ParamTable.h"
#include "pluginkit/TailParams.h"

#include "multidyn/src/core/Params.h"

#include <cstdint>

namespace detonatr {

static_assert (pk::kTailFields == 6, "Detonatr's Multiband IDs start after the six tail fields");

// The stages, in the default order (a sound designer's chain: clean the source, make it tonal,
// squash it, cut it to a spike and a quiet body, then drive it back up loud).
enum Stage { kStageClean = 0, kStageTone, kStageMultiband, kStageTransient, kStageSaturator, kNumStages };
const char* stageName (int stage);

// The Multiband stage is Multidyn without its side-chain and its own saturator, in two blocks. The first:
// block position j holds Multidyn parameter j for j < kSatOn, then RMS Window and Soften. The second
// (at the end of the table, kMb2Base): Multidyn's parameters from the crossovers' Slope on (Slope,
// Soften Color and the Sub band), in Multidyn's order.
constexpr uint32_t kMbBlock = multidyn::kSatOn + 2;
constexpr uint32_t kMb2Block = 9; // multidyn::kXoverSlope .. multidyn::kSubOutput
static_assert (multidyn::kXoverSlope + kMb2Block == multidyn::kNumParams,
               "a Multidyn parameter added after the Sub band goes in a new block at the end of Detonatr's table");
int64_t mbIdAt (uint32_t block);    // the Multidyn ID at a first-block position (-1: none)
int64_t mbBlockOf (uint32_t mdId); // the first-block position of a Multidyn ID (-1: not in the first block)
int64_t mdIdOf (uint32_t id);      // the Multidyn ID of a Detonatr parameter (-1: not a Multiband one)
int64_t detIdOfMd (uint32_t mdId); // the Detonatr ID of a Multidyn parameter (-1: not in Detonatr)

enum ParamId : uint32_t
{
    kOutput = 0, // dB
    kDryWet,
    kOrder1, // which stage runs 1st .. 5th (a Stage); a stage chosen twice runs once, where it comes
    kOrder2, // first, and a stage left out runs after the others (in the default order)
    kOrder3,
    kOrder4,
    kOrder5,
    kCleanOn,
    kDenoise,  // 0 .. 1
    kDereverb, // 0 .. 1
    kToneOn,
    kRoot,       // Hz, the resonators' fundamental
    kMaterial,   // a Material
    kDecay,      // ms the resonators ring
    kResonators, // 0 .. 1
    kCarriers,   // 0 .. 1: the vocoded recordings
    kCarrierLevel1, // 0 .. 1 per recording
    kCarrierLevel2,
    kCarrierLevel3,
    kCarrierLevel4,
    kToneDry,     // 0 .. 1: the input through the Tone stage as it is
    kDisperse,    // 0 .. 1
    kDisperseFreq, // Hz
    kMultibandOn,
    kTransientOn,
    kSpike,       // ms
    kDrop,        // dB
    kFall,        // ms
    kSensitivity, // dB
    kTailBase,                             // the Saturator stage: pk::kTailFields entries (its On is the stage's)
    kMbBase = kTailBase + pk::kTailFields, // the Multiband stage: kMbBlock entries (mbIdAt)
    kTailExtBase = kMbBase + kMbBlock,     // the rest of the Saturator stage: pk::kTailExtFields entries
    kTailExt2Base = kTailExtBase + pk::kTailExtFields, // Gently's Advanced mode in the Saturator stage: pk::kTailExt2Fields entries (the last block)
    kMb2Base = kTailExt2Base + pk::kTailExt2Fields, // the Multiband stage's second block: kMb2Block entries
    kNumParams = kMb2Base + kMb2Block
};
// pinned: IDs are persisted
static_assert (kMbBase == 35 && kTailExtBase == 93 && kTailExt2Base == 110 && kMb2Base == 119 && kNumParams == 128,
               "the Multiband stage's second block is 119 .. 127");

constexpr uint32_t kOrderBase = kOrder1;
constexpr bool isTailParam (uint32_t id)
{
    return (id >= kTailBase && id < kTailBase + pk::kTailFields) || (id >= kTailExtBase && id < kTailExtBase + pk::kTailExtFields) ||
           (id >= kTailExt2Base && id < kTailExt2Base + pk::kTailExt2Fields);
}
constexpr uint32_t tailField (uint32_t id)
{
    return id >= kTailExt2Base  ? pk::kTailFields + pk::kTailExtFields + (id - kTailExt2Base)
           : id >= kTailExtBase ? pk::kTailFields + (id - kTailExtBase)
                                : id - kTailBase;
}
constexpr bool isMbParam (uint32_t id) { return (id >= kMbBase && id < kMbBase + kMbBlock) || (id >= kMb2Base && id < kMb2Base + kMb2Block); }
// the Detonatr ID of a Multidyn parameter in Detonatr (detIdOfMd must not be -1)
constexpr uint32_t mbParam (uint32_t mdId)
{
    return mdId >= multidyn::kXoverSlope ? kMb2Base + (mdId - multidyn::kXoverSlope)
                                         : kMbBase + (mdId == multidyn::kRmsWindow ? multidyn::kSatOn : mdId == multidyn::kSoften ? multidyn::kSatOn + 1 : mdId);
}

// Where each stage runs, from the five order parameters (see kOrder1).
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
