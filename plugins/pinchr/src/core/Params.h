// Pinchr parameters. IDs are persisted in projects: only ever append.
#pragma once

#include "pluginkit/ParamTable.h"
#include "pluginkit/TailParams.h"

#include <cstdint>

namespace pinchr {

enum ParamId : uint32_t
{
    kInput = 0, // dB, -21 .. +21: the gain into the compressor (and its dry path); no makeup after it
    kRatio,     // 1 .. 11: the detector's gain and the knee together (a position, as on the hardware)
    kCurve,     // the knee's diodes: R Soft .. Zener Hard
    kAttack,    // 0 (Fast) .. 10 (Slow)
    kRelease,   // 0 (Fast) .. 10 (Slow)
    kScFilter,  // the sidechain's high-pass: Off, 30, 60, 90, 150, 300 Hz
    kDirty,     // the FET without its distortion compensation (second harmonic)
    kMix,       // 0 .. 1: uncompressed .. compressed (both through the output stage)
    kDrive,     // 0 .. 10 steps: the germanium output stage's gain switch
    kFeedback,  // 0 .. 10: the output stage's feedback (gain and the low resonance)
    kOutput,    // dB
    kLink,      // one detector for both channels (the louder sets the gain)
    kMode,      // Stereo or Mid/Side (the channels are the mid and the side)
    kExtSc,     // the detector hears the sidechain input instead (when the host routes one)
    kScGain,    // dB, -24 .. +24: the sidechain input's level into the detector
    kSaturation, // 0 .. 1: the germanium transistor's saturation in the output stage (0: off, the measured linear stage)
    // the old end saturator's blocks (smacheratr at the end of the chain), retired: kept so saved IDs stay put
    kTailBase,
    kTailExtBase = kTailBase + pk::kTailFields,
    kTailExt2Base = kTailExtBase + pk::kTailExtFields,
    kTailExt3Base = kTailExt2Base + pk::kTailExt2Fields,
    kTailExt4Base = kTailExt3Base + pk::kTailExt3Fields,
    kSoftClip = kTailExt4Base + pk::kTailExt4Fields, // the end of the chain: Soft Clip, then Hard Clip (pluginkit/EndClip.h)
    kHardClip,
    kNumParams
};

// pinned: these numbers are in saved projects
static_assert (kInput == 0 && kRatio == 1 && kCurve == 2 && kAttack == 3 && kRelease == 4 && kScFilter == 5 && kDirty == 6 && kMix == 7 &&
                   kDrive == 8 && kFeedback == 9 && kOutput == 10 && kLink == 11 && kMode == 12 && kExtSc == 13 && kScGain == 14 &&
                   kSaturation == 15 && kTailBase == 16,
               "Pinchr's parameter IDs are fixed");
static_assert (kTailExtBase == 22 && kTailExt2Base == 39 && kTailExt3Base == 48 && kTailExt4Base == 54 && kSoftClip == 59 &&
                   kHardClip == 60 && kNumParams == 61,
               "saved IDs: the old end saturator's blocks at 16 .. 58 (retired), the clips 59 and 60");

enum Knee
{
    kRSoft = 0,
    kGermSoft,
    kGermMed,
    kSiliconMed,
    kSiliconHard,
    kZenerHard,
    kNumCurves
};

enum Mode
{
    kStereo = 0,
    kMidSide
};

constexpr int kNumScFilters = 6; // Off, then the five high-passes

const pk::ParamTable& paramTable ();
inline double toPlain (uint32_t id, double n) { return paramTable ().toPlain (id, n); }
inline double toNormalized (uint32_t id, double p) { return paramTable ().toNormalized (id, p); }
inline double defaultNormalized (uint32_t id) { return paramTable ().defaultNormalized (id); }

// Menu > Defaults (pluginkit/GentlrDefaults.h): none (the end saturator it set is retired)
inline constexpr pk::GentlrIds kGentlrIds {};

} // namespace pinchr
