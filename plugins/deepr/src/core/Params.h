// Deepr parameters. IDs are persisted in projects: only ever append.
#pragma once

#include "pluginkit/ParamTable.h"
#include "pluginkit/TailParams.h"

#include <cmath>
#include <cstdint>

namespace deepr {

enum ParamId : uint32_t
{
    kDepth = 0,  // dB: the most the low mids are dipped while the sub plays
    kDipFreq,    // Hz, the centre of the dipped band
    kDipWidth,   // octaves between its edges
    kThreshold,  // dB: the sub level where the dip starts (full depth 12 dB above it)
    kAttack,     // ms: how fast the dip follows the sub coming in
    kRelease,    // ms: and lets go after it
    kSplit,      // Hz: the sub band is everything below (Linkwitz-Riley, 24 dB/oct)
    kMonoSub,    // 0 .. 1: how much of the sub's side is folded into its mid
    kSubGain,    // dB, the sub band's level
    kListen,     // Off / Sub / Cut
    kMix,        // dry / wet
    kOutput,     // dB
    kTailBase,   // the Smacheratr at the end of the chain: pk::kTailFields entries

    kTailExtBase = kTailBase + pk::kTailFields,        // the rest of the saturator: pk::kTailExtFields entries
    kTailExt2Base = kTailExtBase + pk::kTailExtFields, // Gentlr's Advanced mode and Sub band: pk::kTailExt2Fields
    kTailExt3Base = kTailExt2Base + pk::kTailExt2Fields, // Gentlr's High band, No Overlap and Slope in it: pk::kTailExt3Fields entries
    kTailExt4Base = kTailExt3Base + pk::kTailExt3Fields, // Gentlr's glue in it: pk::kTailExt4Fields entries (the last block)
    kNumParams = kTailExt4Base + pk::kTailExt4Fields
};

enum Listen { kListenOff = 0, kListenSub, kListenCut };

// The dip's law: nothing below the threshold, the full Depth 12 dB (kKeyRangeDb) above it, in
// between in proportion to how far over the sub's level is.
constexpr double kKeyRangeDb = 12.0;
inline double dipKey (double subDb, double thresholdDb)
{
    const double k = (subDb - thresholdDb) / kKeyRangeDb;
    return k < 0.0 ? 0.0 : (k > 1.0 ? 1.0 : k);
}

// The dip band's Q for a width in octaves (between its -3 dB points).
inline double dipQ (double widthOct)
{
    const double w = widthOct < 0.1 ? 0.1 : widthOct, r = std::pow (2.0, w);
    return std::sqrt (r) / (r - 1.0);
}

const pk::ParamTable& paramTable ();
inline double toPlain (uint32_t id, double n) { return paramTable ().toPlain (id, n); }
inline double toNormalized (uint32_t id, double p) { return paramTable ().toNormalized (id, p); }
inline double defaultNormalized (uint32_t id) { return paramTable ().defaultNormalized (id); }

// Menu > Defaults (pluginkit/GentlrDefaults.h): the parameters Gentlr On by Default and Advanced On by
// Default set in a new instance: the end saturator's Saturator and Gentlr switches and Gentlr's Advanced
inline constexpr pk::GentlrIds kGentlrIds = pk::tailGentlrIds (kTailBase, kTailExtBase, kTailExt2Base);

} // namespace deepr
