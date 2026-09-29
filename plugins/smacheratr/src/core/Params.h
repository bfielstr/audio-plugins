// Smacheratr parameters. IDs are persisted in projects: only ever append.
#pragma once

#include "pluginkit/ParamTable.h"

#include <algorithm>
#include <cmath>
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
    kMidSide,   // saturate the mid and the side apart (keeps the width when pushed)
    kClarity,   // keep the low mids from going muddy when the drive is pushed (see Engine.h)
    kClarityFreq,  // Hz, the centre of Clarity's band (ClarityBand.h); 20 Hz - 20 kHz (20 - 500 Hz before); 20 Hz - 20 kHz (20 - 500 Hz before)
    kClarityWidth, // octaves between the band's edges
    kClarityRange, // dB: the most Clarity turns its band down (before the curve; half as much after)

    kNumParams
};

enum PostClipMode { kPostOff = 0, kPostSoft, kPostHard };

const pk::ParamTable& paramTable ();
inline double toPlain (uint32_t id, double n) { return paramTable ().toPlain (id, n); }
inline double toNormalized (uint32_t id, double p) { return paramTable ().toNormalized (id, p); }
inline double defaultNormalized (uint32_t id) { return paramTable ().defaultNormalized (id); }

// Clarity Frequency covered 20 - 500 Hz (log) at first: a value saved then (normalized), in today's range.
// States from before the change run their Clarity Frequency values through this when they load.
inline double clarityFreqFromNarrowRange (double oldNorm)
{
    return toNormalized (kClarityFreq, 20.0 * std::pow (25.0, std::clamp (oldNorm, 0.0, 1.0)));
}

} // namespace smacheratr
