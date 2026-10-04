// Dropr parameters. IDs are persisted in projects: only ever append. (Dropr's first table, the drawn
// transient shape, never shipped in a release; state version 2 replaced it, see plugin/State.cpp.)
#pragma once

#include "pluginkit/ParamTable.h"
#include "pluginkit/TailParams.h"

#include <cstdint>
#include <string>

namespace dropr {

constexpr int kMaxBands = 6;
constexpr int kNumXovers = kMaxBands - 1;

enum ParamId : uint32_t
{
    kInput = 0,                    // dB of gain before the bands
    kBands,                        // 1 .. 6
    kXover1,                       // Hz, kNumXovers of them, ascending (band k lies between kXover1 + k - 1 and kXover1 + k)
    kBandGain1 = kXover1 + kNumXovers, // dB, each band's own gain (the points of the display), kMaxBands of them
    kMode = kBandGain1 + kMaxBands, // Stereo / Mid-Side
    kLink,                         // channel link 0 .. 1
    kAdaptive,                     // Adaptive Time 0 .. 1
    kAttack,                       // ms
    kRelease,                      // ms
    kDownThreshold,                // dB
    kDownRatio,                    // 1 .. 100 (100: 1 : inf), when Negative is off
    kNegative,                     // the downward ratio goes negative
    kNegRatio,                     // 0.1 .. 100 (100: -inf): the output falls this many dB per dB over the threshold
    kRange,                        // dB: Negative mode's floor, this far below the downward threshold
    kUpThreshold,                  // dB
    kUpRatio,                      // 1 .. 100 (100: 1 : inf)
    kKnee,                         // dB
    kTilt,                         // dB per octave across the bands' gains, pivot at 1 kHz
    kMakeup,                       // dB on every band
    kMix,                          // dry / wet
    kOutput,                       // dB
    kTailBase,                     // the Smacheratr at the end of the chain: pk::kTailFields entries
    kTailExtBase = kTailBase + pk::kTailFields,        // the rest of it: pk::kTailExtFields entries
    kTailExt2Base = kTailExtBase + pk::kTailExtFields, // Gentlr's Advanced mode and Sub band: pk::kTailExt2Fields
    kTailExt3Base = kTailExt2Base + pk::kTailExt2Fields, // Gentlr's High band and No Overlap in it: pk::kTailExt3Fields entries (the last block)
    kNumParams = kTailExt3Base + pk::kTailExt3Fields
};

// pinned: these numbers are in saved projects
static_assert (kXover1 == 2 && kBandGain1 == 7 && kMode == 13 && kLink == 14 && kAdaptive == 15 && kAttack == 16 &&
                   kRelease == 17 && kDownThreshold == 18 && kDownRatio == 19 && kNegative == 20 && kNegRatio == 21 &&
                   kRange == 22 && kUpThreshold == 23 && kUpRatio == 24 && kKnee == 25 && kTilt == 26 && kMakeup == 27 &&
                   kMix == 28 && kOutput == 29 && kTailBase == 30,
               "Dropr's parameter IDs are fixed");

enum Mode { kModeStereo = 0, kModeMidSide, kNumModes };

constexpr double kRatioInf = 100.0;   // a ratio parameter at its maximum reads (and works as) infinity
constexpr double kMinXoverHz = 20.0, kMaxXoverHz = 20000.0;
constexpr double kXoverDefaults[kNumXovers] = {70.0, 800.0, 2150.0, 4500.0, 11000.0};

const pk::ParamTable& paramTable ();
inline double toPlain (uint32_t id, double n) { return paramTable ().toPlain (id, n); }
inline double toNormalized (uint32_t id, double p) { return paramTable ().toNormalized (id, p); }
inline double defaultNormalized (uint32_t id) { return paramTable ().defaultNormalized (id); }

// the Negative Ratio's text: "1 : -x", "1 : -inf" at the maximum
std::string negRatioText (double x);

} // namespace dropr
