// Lowfocus parameters. IDs are persisted in projects: only ever append.
#pragma once

#include "pluginkit/ParamTable.h"

#include <cstdint>
#include <string>

namespace lowfocus {

enum ParamId : uint32_t
{
    kContrast = 0, // -1 .. +1
    kMode,         // Punchy / Smooth
    kGain,         // dB, applied to the focus range
    kLowFreq,
    kHighFreq,
    kSolo,         // hear only the focus range
    kOutput,

    kNumParams
};

enum Mode { kPunchy = 0, kSmooth };

const pk::ParamTable& paramTable ();
inline double toPlain (uint32_t id, double n) { return paramTable ().toPlain (id, n); }
inline double toNormalized (uint32_t id, double p) { return paramTable ().toNormalized (id, p); }
inline double defaultNormalized (uint32_t id) { return paramTable ().defaultNormalized (id); }

} // namespace lowfocus
