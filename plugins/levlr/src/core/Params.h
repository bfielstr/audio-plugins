// Levlr parameters. IDs are persisted in projects: only ever append. The end saturator's extended
// block comes last, so it can grow; a new Levlr parameter goes in a block after it (and from then on
// the extended block stays as it is).
#pragma once

#include "pluginkit/ParamTable.h"
#include "pluginkit/TailParams.h"

#include <cstdint>

namespace levlr {

static_assert (pk::kTailFields == 6, "Levlr's band IDs start after the six tail fields");

constexpr int kBands = 4;
constexpr int kCrossovers = kBands - 1;
constexpr double kMinXoverHz = 20.0, kMaxXoverHz = 20000.0;
constexpr double kMinGapOct = 1.0 / 6.0; // crossovers stay at least this far apart

// Each band: its level, and whether it is heard.
enum BandField : uint32_t
{
    kGain = 0, // dB
    kMute,
    kSolo,
    kBandBlock
};

enum ParamId : uint32_t
{
    kSlope = 0, // the crossovers' Linkwitz-Riley slope (Slope: 12 .. 96 dB/oct in 12 dB steps)
    kOutput,    // dB, before the Smacheratr at the end
    kXover,     // kCrossovers x Hz: band k's top edge is kXover + k
    kTailBase = kXover + kCrossovers,               // the Smacheratr at the end of the chain: pk::kTailFields entries
    kBandBase = kTailBase + pk::kTailFields,        // kBands x kBandBlock
    kTailExtBase = kBandBase + kBands * kBandBlock, // the rest of the end Smacheratr
    kTailExt2Base = kTailExtBase + pk::kTailExtFields, // Gently's Advanced mode in the end Smacheratr: pk::kTailExt2Fields entries (the last block)
    kNumParams = kTailExt2Base + pk::kTailExt2Fields
};

constexpr uint32_t bandParam (int band, uint32_t field) { return kBandBase + (uint32_t)band * kBandBlock + field; }
constexpr uint32_t xoverParam (int k) { return kXover + (uint32_t)k; }
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

const pk::ParamTable& paramTable ();
inline double toPlain (uint32_t id, double n) { return paramTable ().toPlain (id, n); }
inline double toNormalized (uint32_t id, double p) { return paramTable ().toNormalized (id, p); }
inline double defaultNormalized (uint32_t id) { return paramTable ().defaultNormalized (id); }

} // namespace levlr
