// Smeezr parameters. IDs are persisted in projects: only ever append.
#pragma once

#include "pluginkit/ParamTable.h"
#include "pluginkit/TailParams.h"

#include <cstdint>

namespace smeezr {

enum ParamId : uint32_t
{
    kSqueeze = 0, // 0 .. 1: the one knob (0: untouched; up to 50 %: toward pink; 50 .. 100 %: the OTT boost on top)
    kSpeed,       // Fast / Slow: how quickly the pink stage follows the music
    kMix,         // 0 .. 1: dry .. wet
    kOutput,      // dB
    // the Smacheratr at the end of the chain (the suite's end saturator), every block of it
    kTailBase,
    kTailExtBase = kTailBase + pk::kTailFields,
    kTailExt2Base = kTailExtBase + pk::kTailExtFields,
    kTailExt3Base = kTailExt2Base + pk::kTailExt2Fields,
    kTailExt4Base = kTailExt3Base + pk::kTailExt3Fields,
    kNumParams = kTailExt4Base + pk::kTailExt4Fields
};

// pinned: these numbers are in saved projects
static_assert (kSqueeze == 0 && kSpeed == 1 && kMix == 2 && kOutput == 3 && kTailBase == 4, "Smeezr's parameter IDs are fixed");
static_assert (kTailExtBase == 10 && kTailExt2Base == 27 && kTailExt3Base == 36 && kTailExt4Base == 42 && kNumParams == 47,
               "saved IDs: the end saturator's blocks at 4 .. 46");

enum Speed { kSpeedFast = 0, kSpeedSlow };

const pk::ParamTable& paramTable ();
inline double toPlain (uint32_t id, double n) { return paramTable ().toPlain (id, n); }
inline double toNormalized (uint32_t id, double p) { return paramTable ().toNormalized (id, p); }
inline double defaultNormalized (uint32_t id) { return paramTable ().defaultNormalized (id); }

// Menu > Defaults (pluginkit/GentlrDefaults.h): the parameters Gentlr On by Default and Advanced On by
// Default set in a new instance: the end saturator's Saturator and Gentlr switches and Gentlr's Advanced
inline constexpr pk::GentlrIds kGentlrIds = pk::tailGentlrIds (kTailBase, kTailExtBase, kTailExt2Base);

} // namespace smeezr
