// Bubr parameters. IDs are persisted in projects: only ever append.
#pragma once

#include "pluginkit/GentlrDefaults.h"
#include "pluginkit/ParamTable.h"

#include <cstdint>

namespace bubr {

enum ParamId : uint32_t
{
    kBoost = 0,  // 0 .. 1.1 (0 .. 110 %): the effect's level (+0.3 dB a percent, part into the clipper, part after it)
    kFreq,       // Hz, 20 .. 125: where the effect sits (the band's centre)
    kIntensity,  // 0 .. 1: the gain into the clipper (no change to the effect's ceiling)
    kQFactor,    // 0 .. 1: the band's focus (narrower and louder up)
    kDrive,      // 0 .. 1: the gain after the clipper
    kDriveX2,    // Drive's stage doubled
    kGate,       // dB, -60 .. 0: the gate's threshold (on the input's peak)
    kAttack,     // ms, 0 .. 300: how fast the gate opens
    kRelease,    // ms, 20 .. 500: how fast it closes
    kRatio,      // 1 .. 100: the gate's expansion below the threshold (from about 50 it shuts)
    kLowCut,     // Hz, 20 .. 125: a fourth-order high-pass on the effect (and on the dry signal while Dry/Wet mixes it in)
    kDryWet,     // -1 .. 1: dry only .. both at full level (0) .. the effect only
    kOversample, // Off, 2x, 4x: the clipper oversampled
    kLookahead,  // the output limiter's lookahead (off: no latency, a little more distortion when it limits)
    kOutput,     // dB, -60 .. 0 (-60: silence), after the output limiter
    kSoftClip,   // the end of the chain: Soft Clip, then Hard Clip (pluginkit/EndClip.h)
    kHardClip,
    kNumParams
};

// pinned: these numbers are in saved projects
static_assert (kBoost == 0 && kFreq == 1 && kIntensity == 2 && kQFactor == 3 && kDrive == 4 && kDriveX2 == 5 && kGate == 6 &&
                   kAttack == 7 && kRelease == 8 && kRatio == 9 && kLowCut == 10 && kDryWet == 11 && kOversample == 12 &&
                   kLookahead == 13 && kOutput == 14 && kSoftClip == 15 && kHardClip == 16 && kNumParams == 17,
               "Bubr's parameter IDs are fixed");

enum Oversample
{
    kOsOff = 0,
    kOs2x,
    kOs4x
};

const pk::ParamTable& paramTable ();
inline double toPlain (uint32_t id, double n) { return paramTable ().toPlain (id, n); }
inline double toNormalized (uint32_t id, double p) { return paramTable ().toNormalized (id, p); }
inline double defaultNormalized (uint32_t id) { return paramTable ().defaultNormalized (id); }

// Menu > Defaults (pluginkit/GentlrDefaults.h): none (bubr has no end saturator)
inline constexpr pk::GentlrIds kGentlrIds {};

} // namespace bubr
