// Locus parameters. IDs are persisted in projects: only ever append.
#pragma once

#include "pluginkit/ParamTable.h"
#include "pluginkit/TailParams.h"

#include <cstdint>
#include <string>

namespace locus {

enum ParamId : uint32_t
{
    kContrast = 0, // -1 .. +1
    kMode,         // Punchy / Smooth
    kGain,         // dB, applied to the focus range
    kLowFreq,
    kHighFreq,
    kSolo,         // hear only the focus range
    kOutput,
    kTailBase, // the Smacheratr at the end of the chain: pk::kTailFields entries

    kTailExtBase = kTailBase + pk::kTailFields, // the rest of the saturator: pk::kTailExtFields entries
    kTailExt2Base = kTailExtBase + pk::kTailExtFields, // Gently's Advanced mode in the end Smacheratr: pk::kTailExt2Fields entries
    kTailExt3Base = kTailExt2Base + pk::kTailExt2Fields, // Gently's High band and No Overlap in it: pk::kTailExt3Fields entries (the last block)
    kNumParams = kTailExt3Base + pk::kTailExt3Fields
};

enum Mode { kPunchy = 0, kSmooth };

const pk::ParamTable& paramTable ();
inline double toPlain (uint32_t id, double n) { return paramTable ().toPlain (id, n); }
inline double toNormalized (uint32_t id, double p) { return paramTable ().toNormalized (id, p); }
inline double defaultNormalized (uint32_t id) { return paramTable ().defaultNormalized (id); }

} // namespace locus
