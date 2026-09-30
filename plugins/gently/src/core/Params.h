// Gently parameters. IDs are persisted in projects: only ever append. The end saturator's blocks come
// last (Gently's Advanced block of the end Smacheratr is the very last, so it can grow); a new Gently
// parameter goes in a block after them.
#pragma once

#include "pluginkit/ParamTable.h"
#include "pluginkit/TailParams.h"

#include "smacheratr/src/core/Params.h"

#include <cstdint>

namespace gently {

static_assert (pk::kTailFields == 6, "Gently's tail blocks follow its bands");

// Two bands, as in Smacheratr (its Threshold sliders and meters are made for two).
constexpr int kBands = 2;
static_assert (kBands == smacheratr::kClarityBands, "one Threshold slider per band");

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
    kDrive,        // Advanced: saturate the band region Gently works on (Smacheratr's Analog curve, level-matched)
    kDriveAmount,  // dB into the curve for that region (0 - 36)
    kAttack,       // ms, how fast the detector follows a band getting louder
    kRelease,      // ms, how fast it lets go
    kStereo,       // StereoMode
    kMix,          // dry / wet
    kOutput,       // dB, before the Smacheratr at the end
    kBandBase,                                         // kBands x kBandBlock
    kTailBase = kBandBase + kBands * kBandBlock,       // the Smacheratr at the end of the chain: pk::kTailFields entries
    kTailExtBase = kTailBase + pk::kTailFields,        // the rest of it: pk::kTailExtFields entries
    kTailExt2Base = kTailExtBase + pk::kTailExtFields, // its Gently's Advanced mode: pk::kTailExt2Fields entries (the last block)
    kNumParams = kTailExt2Base + pk::kTailExt2Fields
};

// the detector's default times: Smacheratr's (15 ms attack, 150 ms release)
constexpr double kDefaultAttackMs = 15.0, kDefaultReleaseMs = 150.0;

constexpr uint32_t bandParam (int band, uint32_t field) { return kBandBase + (uint32_t)band * kBandBlock + field; }
constexpr bool isBandParam (uint32_t id) { return id >= kBandBase && id < kTailBase; }
constexpr bool isTailParam (uint32_t id) { return id >= kTailBase && id < kNumParams; }
constexpr uint32_t tailField (uint32_t id) { return id - kTailBase; } // the three blocks are consecutive

const pk::ParamTable& paramTable ();
inline double toPlain (uint32_t id, double n) { return paramTable ().toPlain (id, n); }
inline double toNormalized (uint32_t id, double p) { return paramTable ().toNormalized (id, p); }
inline double defaultNormalized (uint32_t id) { return paramTable ().defaultNormalized (id); }

// Whether a band works (on, and its Range above 0 dB: with no range it would cut nothing).
inline bool bandWorks (double on, double rangeDb) { return on >= 0.5 && rangeDb > 0.0; }

// Gently's Threshold sliders are Smacheratr's (smacheratr::ThresholdSlider, bound to Smacheratr's
// Threshold IDs): the Gently parameter behind Smacheratr parameter `id`, or -1.
inline int64_t fromSmacheratr (uint32_t id)
{
    for (int k = 0; k < kBands; ++k)
        if (id == smacheratr::kClarityThresholdIds[k])
            return (int64_t)bandParam (k, kThreshold);
    if (id == smacheratr::kClarityAdvanced)
        return kAdvanced;
    return -1;
}

} // namespace gently
