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
    kClarity,   // Gently (called Clarity before) on (both bands; see Engine.h)
    kClarityFreq,  // Hz, the centre of Gently's band (ClarityBand.h); 20 Hz - 20 kHz (20 - 500 Hz before)
    kClarityWidth, // octaves between the band's edges
    kClarityRange, // dB: the most Clarity turns its band down (before the curve; half as much after)
    kClarity2,      // unused since one Clarity button: a band works while its Range is above 0 dB
    kClarity2Freq,  // Hz
    kClarity2Width, // octaves
    kClarity2Range, // dB (0 by default: the second band does nothing until it gets a range)
    // Gently's Advanced mode (Clarity is called Gently in everything the user sees; the IDs keep the
    // old names). With Advanced off Gently is exactly the Clarity from before.
    kClarityAdvanced,    // Advanced on: the bands' Thresholds and the region Drive work
    kClarityThreshold,   // dB, where band 1 starts cutting (Advanced; without it -18 dB, kClarityThresholdDb)
    kClarity2Threshold,  // dB, band 2's
    kClarityDrive,       // Advanced: saturate the band region Gently works on (Smacheratr's Analog curve)
    kClarityDriveAmount, // dB into the curve for that region (level-matched: denser, not louder)

    kNumParams
};

enum PostClipMode { kPostOff = 0, kPostSoft, kPostHard };

// Clarity's bands: their Frequency, Width and Range parameters. Clarity has one button (kClarity);
// a band works while Clarity is on and its Range is above 0 dB.
constexpr int kClarityBands = 2;
inline constexpr uint32_t kClarityFreqIds[kClarityBands] = {kClarityFreq, kClarity2Freq};
inline constexpr uint32_t kClarityWidthIds[kClarityBands] = {kClarityWidth, kClarity2Width};
inline constexpr uint32_t kClarityRangeIds[kClarityBands] = {kClarityRange, kClarity2Range};
inline constexpr uint32_t kClarityThresholdIds[kClarityBands] = {kClarityThreshold, kClarity2Threshold};

// Gently's law (Engine.cpp): a band's cut before the curve is 3 dB for every 5 dB its level (the
// band's peak level going into the curve, see Engine.h) is over the threshold, a 2.5 : 1 hard knee,
// up to the band's Range; so it reaches the Range (Range / 0.6) dB over the threshold. Without
// Advanced the threshold is kClarityThresholdDb, which is also where a band's Threshold starts.
constexpr double kClarityThresholdDb = -18.0;
constexpr double kClarityCutPerDb = 0.6;
inline double clarityCutDb (double levelDb, double thresholdDb, double rangeDb)
{
    return std::clamp ((levelDb - thresholdDb) * kClarityCutPerDb, 0.0, std::clamp (rangeDb, 0.0, 24.0));
}

const pk::ParamTable& paramTable ();
inline double toPlain (uint32_t id, double n) { return paramTable ().toPlain (id, n); }
inline double toNormalized (uint32_t id, double p) { return paramTable ().toNormalized (id, p); }
inline double defaultNormalized (uint32_t id) { return paramTable ().defaultNormalized (id); }

// Whether Clarity band k works, from plain values (Clarity on, the band's Range above 0 dB).
inline bool clarityBandOn (double clarityOn, double rangeDb) { return clarityOn >= 0.5 && rangeDb > 0.0; }

// Before one Clarity button, band 2 had an On of its own. For a state from then (normalized values):
// band 2 off -> its Range 0; band 2 on with Clarity off -> Clarity on and band 1's Range 0. Same sound.
inline void clarityToOneButton (double& on1, double& range1, double on2, double& range2)
{
    if (on2 < 0.5)
        range2 = 0.0;
    else if (on1 < 0.5)
    {
        on1 = 1.0;
        range1 = 0.0;
    }
}

// Clarity Frequency covered 20 - 500 Hz (log) at first: a value saved then (normalized), in today's range.
// States from before the change run their Clarity Frequency values through this when they load.
inline double clarityFreqFromNarrowRange (double oldNorm)
{
    return toNormalized (kClarityFreq, 20.0 * std::pow (25.0, std::clamp (oldNorm, 0.0, 1.0)));
}

} // namespace smacheratr
