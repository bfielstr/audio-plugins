// Gentlr parameters. IDs are persisted in projects: only ever append. The end saturator's first three
// blocks came last; Gentlr's parameters added after them (the High band, No Overlap) sit right after
// them, at fixed numbers, and the end saturator's fourth block after those. That block grew by one
// field (its Gentlr's Slope) while it was still the last; Gentlr's own Slope came after it, so it is
// closed in now. Gentlr's glue switches came next, its own and then the end saturator's (a fifth
// block, at the very end). Every ID below is pinned (static_asserts at the end of this file).
#pragma once

#include "pluginkit/ParamTable.h"
#include "pluginkit/TailParams.h"

#include "smacheratr/src/core/Params.h"

#include <cstdint>

namespace gentlr {

static_assert (pk::kTailFields == 6, "Gentlr's tail blocks follow its bands");

// Two bands, as in Smacheratr, then the Sub band and the High band (bands kSub and kHigh where all four
// are counted: the engine, the display and the Threshold sliders work on kAllBands, in Smacheratr's
// order).
constexpr int kBands = 2;
static_assert (kBands == smacheratr::kClarityBands, "one Threshold slider per band");
constexpr int kSub = kBands;
constexpr int kHigh = kBands + 1;
constexpr int kAllBands = kBands + 2;
static_assert (kSub == smacheratr::kSubBand && kHigh == smacheratr::kHighBand && kAllBands == smacheratr::kGentlrBands,
               "Smacheratr's bands, in its order");
// a band (not Sub or High), that has a Width
constexpr bool hasWidth (int k) { return k < kBands; }

// Each band: on, where it sits, how wide it is, the most it cuts and where it starts cutting.
enum BandField : uint32_t
{
    kOn = 0,
    kFreq,      // Hz, the band's centre (20 Hz - 20 kHz)
    kWidth,     // octaves between the band's edges (0.5 - 4)
    kRange,     // dB, the most the band is turned down (0 - 24; 0 cuts nothing)
    kThreshold, // dB, where the band starts cutting (Advanced; without it -18 dB, smacheratr::kClarityThresholdDb)
    kBandBlock
};

enum StereoMode : int
{
    kStereoLinked = 0, // left and right, one detector for both (the image stays put)
    kMidSide,          // mid and side, each with its own detector
    kMidOnly,          // only the mid is worked on (the side passes)
    kSideOnly,         // only the side
    kNumStereoModes
};

enum ParamId : uint32_t
{
    kAdvanced = 0, // the bands' Thresholds and the region Drive work
    kDrive,        // Advanced: saturate the band region Gentlr works on (Smacheratr's Analog curve, level-matched)
    kDriveAmount,  // dB into the curve for that region (0 - 36)
    kAttack,       // ms, how fast the detector follows a band getting louder
    kRelease,      // ms, how fast it lets go
    kStereo,       // StereoMode
    kMix,          // dry / wet
    kOutput,       // dB, before the Smacheratr at the end
    kBandBase,                                         // kBands x kBandBlock
    // the Sub band: a shelf, everything from the very bottom up to where its cut starts to let go (
    // smacheratr::subBand), compressed like the bands; it has no Width, and no button: it works while its
    // Range is above 0 dB (0 by default, as in Smacheratr)
    kSubOn = kBandBase + kBands * kBandBlock, // unused since the Sub band lost its button (State.cpp converts older states)
    kSubFreq,                                 // Hz, where the band starts to taper off (20 - 100 Hz)
    kSubRange,                                // dB, the most it turns the sub region down (0 - 24; 0 by default)
    kSubThreshold,                            // dB, where it starts cutting (Advanced; without it -18 dB)
    kTailBase,                                         // the Smacheratr at the end of the chain: pk::kTailFields entries
    kTailExtBase = kTailBase + pk::kTailFields,        // the rest of it: pk::kTailExtFields entries
    kTailExt2Base = kTailExtBase + pk::kTailExtFields, // its Gentlr's Advanced mode and Sub band: pk::kTailExt2Fields entries
    // --- after the end saturator's first three blocks ---
    // the High band, the Sub band's mirror: a shelf, everything from where its cut starts to let go up
    // to the very top (smacheratr::highBand), compressed like the bands; no Width and no button, as Sub
    kHighOn = kTailExt2Base + pk::kTailExt2Fields, // unused since the High band lost its button
    kHighFreq,                                     // Hz, where the band starts to taper off going down (2 - 16 kHz)
    kHighRange,                                    // dB, the most it turns the top down (0 - 24; 0 by default)
    kHighThreshold,                                // dB, where it starts cutting (Advanced; without it -18 dB)
    kNoOverlap,    // the bands never cover the same frequencies (they push each other: smacheratr/src/core/NoOverlap.h)
    kTailExt3Base, // the end Smacheratr's Gentlr High band, No Overlap and Slope: pk::kTailExt3Fields entries
    // --- after the end saturator's fourth block ---
    // Slope: the shape of bands 1 and 2 (smacheratr::ClaritySlope: 12 / 12 for a new instance; State.cpp
    // gives states from before it Classic, the shape there was then). The Sub and High bands keep theirs.
    kSlope = kTailExt3Base + pk::kTailExt3Fields,
    // Glue: two neighbouring bands held at a shared border (smacheratr/src/core/Glue.h), one switch per
    // pair that can meet, in smacheratr::GluePair's order; all off by default (and in states from before)
    kGlue12,    // band 1 and band 2
    kGlueSub1,  // the Sub band and band 1
    kGlueSub2,  // the Sub band and band 2
    kGlue1High, // band 1 and the High band
    kGlue2High, // band 2 and the High band
    kTailExt4Base, // the end Smacheratr's Gentlr glue: pk::kTailExt4Fields entries (the last block)
    kNumParams = kTailExt4Base + pk::kTailExt4Fields
};
// Gentlr's glue switches (smacheratr::GluePair's order)
inline constexpr uint32_t kGlueIds[smacheratr::kGluePairs] = {kGlue12, kGlueSub1, kGlueSub2, kGlue1High, kGlue2High};

// the detector's default times: Smacheratr's (15 ms attack, 150 ms release)
constexpr double kDefaultAttackMs = 15.0, kDefaultReleaseMs = 150.0;

constexpr uint32_t bandParam (int band, uint32_t field) { return kBandBase + (uint32_t)band * kBandBlock + field; }
// A band's parameters, counting the Sub band (k == kSub) and the High band (k == kHigh) too. They have
// no Width, and their On (kSubOn, kHighOn) is unused (hasOn).
constexpr bool hasOn (int k) { return k < kBands; }
constexpr uint32_t onParam (int k) { return k == kSub ? (uint32_t)kSubOn : k == kHigh ? (uint32_t)kHighOn : bandParam (k, kOn); }
constexpr uint32_t freqParam (int k) { return k == kSub ? (uint32_t)kSubFreq : k == kHigh ? (uint32_t)kHighFreq : bandParam (k, kFreq); }
constexpr uint32_t rangeParam (int k) { return k == kSub ? (uint32_t)kSubRange : k == kHigh ? (uint32_t)kHighRange : bandParam (k, kRange); }
constexpr uint32_t thresholdParam (int k)
{
    return k == kSub ? (uint32_t)kSubThreshold : k == kHigh ? (uint32_t)kHighThreshold : bandParam (k, kThreshold);
}
// a band's parameter, the Sub and High bands' included (every ID from the first band's to the Sub
// band's last, and the High band's)
constexpr bool isSubParam (uint32_t id) { return id >= kSubOn && id < kTailBase; }
constexpr bool isHighParam (uint32_t id) { return id >= kHighOn && id <= kHighThreshold; }
constexpr bool isBandParam (uint32_t id) { return (id >= kBandBase && id < kTailBase) || isHighParam (id); }
constexpr bool isTailParam (uint32_t id)
{
    return (id >= kTailBase && id < kHighOn) || (id >= kTailExt3Base && id < kSlope) || (id >= kTailExt4Base && id < kNumParams);
}
// its field in smacheratr::Tail (the first three blocks are consecutive, the fourth and fifth come after
// Gentlr's own)
constexpr uint32_t tailField (uint32_t id)
{
    return id >= kTailExt4Base   ? pk::kTailFields + pk::kTailExtFields + pk::kTailExt2Fields + pk::kTailExt3Fields + (id - kTailExt4Base)
           : id >= kTailExt3Base ? pk::kTailFields + pk::kTailExtFields + pk::kTailExt2Fields + (id - kTailExt3Base)
                                 : id - kTailBase;
}

const pk::ParamTable& paramTable ();
inline double toPlain (uint32_t id, double n) { return paramTable ().toPlain (id, n); }
inline double toNormalized (uint32_t id, double p) { return paramTable ().toNormalized (id, p); }
inline double defaultNormalized (uint32_t id) { return paramTable ().defaultNormalized (id); }

// Whether band k works, from plain values: on (its onParam), and its Range above 0 dB (with no range it
// would cut nothing). The Sub and High bands have no On of their own: they work while their Range is
// above 0 dB (`on` is ignored), as Smacheratr's.
inline bool bandWorks (int k, double on, double rangeDb) { return (!hasOn (k) || on >= 0.5) && rangeDb > 0.0; }

// Gentlr's Threshold sliders are Smacheratr's (smacheratr::ThresholdSlider, bound to Smacheratr's
// Threshold IDs): the Gentlr parameter behind Smacheratr parameter `id`, or -1.
inline int64_t fromSmacheratr (uint32_t id)
{
    for (int k = 0; k < kAllBands; ++k)
        if (id == smacheratr::kGentlrThresholdIds[k])
            return (int64_t)thresholdParam (k);
    if (id == smacheratr::kClarityAdvanced)
        return kAdvanced;
    if (id == smacheratr::kClarityNoOverlap)
        return kNoOverlap;
    if (id == smacheratr::kClaritySlope)
        return kSlope;
    for (int g = 0; g < smacheratr::kGluePairs; ++g)
        if (id == smacheratr::kClarityGlueIds[g])
            return kGlueIds[g];
    return -1;
}

// The IDs are persisted: pinned.
static_assert (kAdvanced == 0 && kDrive == 1 && kDriveAmount == 2 && kAttack == 3 && kRelease == 4 && kStereo == 5 && kMix == 6 &&
                   kOutput == 7,
               "Gentlr's IDs are persisted");
static_assert (kBandBase == 8 && kBandBlock == 5 && bandParam (1, kThreshold) == 17, "the bands: 8 - 17");
static_assert (kSubOn == 18 && kSubFreq == 19 && kSubRange == 20 && kSubThreshold == 21, "the Sub band: 18 - 21");
static_assert (kTailBase == 22 && kTailExtBase == 28 && kTailExt2Base == 45, "the end saturator's blocks: 22, 28, 45");
static_assert (kHighOn == 54 && kHighFreq == 55 && kHighRange == 56 && kHighThreshold == 57 && kNoOverlap == 58,
               "the High band: 54 - 57, No Overlap 58");
static_assert (kTailExt3Base == 59 && kSlope == 65, "the end saturator's fourth block 59 - 64, Slope 65");
static_assert (kGlue12 == 66 && kGlue2High == 70 && kTailExt4Base == 71 && kNumParams == 76,
               "the glue switches 66 - 70, the end saturator's fifth block 71 - 75: 76 parameters");

// Menu > Defaults (pluginkit/GentlrDefaults.h): the parameters Gentlr On by Default and Advanced On by
// Default set in a new instance: its own Advanced only (it is Gentlr: no Gentlr switch)
inline constexpr pk::GentlrIds kGentlrIds = pk::GentlrIds {-1, -1, (int32_t)kAdvanced};

} // namespace gentlr
