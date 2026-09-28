// Smacheratr parameters. IDs are persisted in projects: only ever append.
#pragma once

#include "pluginkit/ParamTable.h"

#include <cstdint>

namespace smacheratr {

enum ParamId : uint32_t
{
    kDrive = 0,     // dB, gain into the shaper
    kCurve,         // shaping curve
    kBassThreshold, // dB, where the Bass Shaper curve starts to clip
    kPostClip,      // Off / Soft Clip / Hard Clip after the shaper
    kColorOn,       // colour filters (pre-shaper EQ, undone after the shaper)
    kColorLo,       // -1 .. 1, low shelf amount (Amt Lo, +-24 dB)
    kColorHi,       // -1 .. 1, peak amount (Amt Hi, +-24 dB)
    kColorFreq,     // Hz, peak centre
    kColorWidth,    // peak width (1 / Q)
    kOutput,        // dB, final attenuation
    kDryWet,
    kWsDrive,       // Waveshaper curve controls
    kWsCurve,
    kWsDepth,
    kWsLinear,
    kWsDamp,
    kWsPeriod,
    kHiQuality,     // 4x oversampling around the shaper
    kDcFilter,      // high-pass at the input

    kNumParams
};

enum CurveType
{
    kAnalogClip = 0,
    kSoftSine,
    kBassShaper,
    kMediumCurve,
    kHardCurve,
    kSinoidFold,
    kDigitalClip,
    kWaveshaper,
    kNumCurves
};

enum PostClipMode { kPostOff = 0, kPostSoft, kPostHard };

const pk::ParamTable& paramTable ();
inline double toPlain (uint32_t id, double n) { return paramTable ().toPlain (id, n); }
inline double toNormalized (uint32_t id, double p) { return paramTable ().toNormalized (id, p); }
inline double defaultNormalized (uint32_t id) { return paramTable ().defaultNormalized (id); }

} // namespace smacheratr
