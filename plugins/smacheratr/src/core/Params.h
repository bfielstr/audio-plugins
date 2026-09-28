// Smacheratr parameters. IDs are persisted in projects: only ever append.
#pragma once

#include "pluginkit/ParamTable.h"

#include <cstdint>

namespace smacheratr {

enum ParamId : uint32_t
{
    kDrive = 0,         // dB, gain into the shaper (after the pre-limiter)
    kPreLimit,          // look-ahead limiter before the drive
    kPreLimitThreshold, // dB, where the pre-limiter holds the input
    kPostClip,          // No Clip / Soft Clip / Hard Clip after the shaper
    kColorOn,           // colour filters (pre-shaper EQ, undone after the shaper)
    kColorLo,           // -1 .. 1, low shelf amount (Amt Lo, +-24 dB)
    kColorHi,           // -1 .. 1, peak amount (Amt Hi, +-24 dB)
    kColorFreq,         // Hz, peak centre
    kColorWidth,        // peak width (1 / Q)
    kOutput,            // dB, final attenuation
    kDryWet,
    kHiQuality, // 4x oversampling around the shaper
    kDcFilter,  // high-pass at the input

    kNumParams
};

enum PostClipMode { kPostOff = 0, kPostSoft, kPostHard };

const pk::ParamTable& paramTable ();
inline double toPlain (uint32_t id, double n) { return paramTable ().toPlain (id, n); }
inline double toNormalized (uint32_t id, double p) { return paramTable ().toNormalized (id, p); }
inline double defaultNormalized (uint32_t id) { return paramTable ().defaultNormalized (id); }

} // namespace smacheratr
