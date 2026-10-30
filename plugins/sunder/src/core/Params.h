// Sunder parameters (pinchr's, which projects saved with pinchr load into, then the bass enhancer's). IDs are
// persisted in projects: only ever append.
#pragma once

#include "pluginkit/ParamTable.h"
#include "pluginkit/TailParams.h"

#include <cstdint>

namespace sunder {

enum ParamId : uint32_t
{
    kInput = 0, // dB, -48 .. +48 (Curve::Centre; -21 .. +21 linear before state version 3): the gain into the compressor (and its dry path); no makeup after it
    kRatio,     // 1 .. 10 steps: the detector's gain and the knee together (a position, as on the hardware; 1 .. 11 real in pinchr)
    kCurve,     // the knee's diodes: R Soft .. Zener Hard
    kAttack,    // 0 (Fast) .. 10 (Slow)
    kRelease,   // 0 (Fast) .. 10 (Slow)
    kScFilter,  // the sidechain's high-pass: Off, 30, 60, 90, 150, 300 Hz
    kDirty,     // retired (pinchr's Dirty switch): the FET's second harmonic is always in
    kMix,       // 0 .. 1: uncompressed .. compressed (both through the output stage)
    kDrive,     // 1 .. 10 steps: the germanium output stage's gain switch (stagePosition: 1 .. 15 on pinchr's 0 .. 10 scale)
    kFeedback,  // 1 .. 10 steps: the output stage's feedback, gain and the low resonance (the same positions)
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
    // --- sunder (state version 2): the bass enhancer ahead of the compressor (BassEngine.h) ---
    kBass,           // 0 .. 1: the enhancer blended in (0: off, nothing runs)
    kBassBoost,      // 0 .. 1.1: the effect's level
    kBassFreq,       // Hz, 20 .. 125: the band's centre
    kBassIntensity,  // 0 .. 1: the gain into its clipper
    kBassQ,          // 0 .. 1: the band's focus (Q-Factor)
    kBassDrive,      // 0 .. 1: the gain after its clipper
    kBassDriveX2,    // Drive's stage doubled
    kBassGate,       // dB, -60 .. 0: the gate's threshold
    kBassAttack,     // ms, 0 .. 300
    kBassRelease,    // ms, 20 .. 500
    kBassRatio,      // 1 .. 100: the gate's expansion
    kBassLowCut,     // Hz, 20 .. 125
    kBassOversample, // Off, 2x, 4x: its clipper oversampled (the latency grows)
    // --- sunder (state version 3) ---
    kBassPosition, // Pre (ahead of the compressor, as before) or Post (after the output stage, ahead of Output)
    kNumParams
};

// pinned: these numbers are in saved projects (pinchr's and sunder's)
static_assert (kInput == 0 && kRatio == 1 && kCurve == 2 && kAttack == 3 && kRelease == 4 && kScFilter == 5 && kDirty == 6 && kMix == 7 &&
                   kDrive == 8 && kFeedback == 9 && kOutput == 10 && kLink == 11 && kMode == 12 && kExtSc == 13 && kScGain == 14 &&
                   kSaturation == 15 && kTailBase == 16,
               "Sunder's parameter IDs are fixed");
static_assert (kTailExtBase == 22 && kTailExt2Base == 39 && kTailExt3Base == 48 && kTailExt4Base == 54 && kSoftClip == 59 &&
                   kHardClip == 60,
               "saved IDs: the old end saturator's blocks at 16 .. 58 (retired), the clips 59 and 60");
static_assert (kBass == 61 && kBassBoost == 62 && kBassFreq == 63 && kBassIntensity == 64 && kBassQ == 65 && kBassDrive == 66 &&
                   kBassDriveX2 == 67 && kBassGate == 68 && kBassAttack == 69 && kBassRelease == 70 && kBassRatio == 71 &&
                   kBassLowCut == 72 && kBassOversample == 73,
               "saved IDs: the bass enhancer at 61 .. 73");
static_assert (kBassPosition == 74 && kNumParams == 75, "saved IDs: Bass Position at 74");

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

enum BassPosition
{
    kBassPre = 0, // ahead of the compressor (and Input)
    kBassPost     // after the compressor and the output stage (Mid/Side decoded), ahead of Output and the clips
};

// Input's range: -48 .. +48 dB on Curve::Centre; state version 2 and pinchr's saved it on -21 .. +21 dB, linear
constexpr double kInputMaxDb = 48.0, kOldInputMaxDb = 21.0;

enum BassOversample
{
    kBassOsOff = 0,
    kBassOs2x,
    kBassOs4x
};

constexpr int kNumScFilters = 6; // Off, then the five high-passes
constexpr int kSteps = 10;       // Drive, Feedback and Ratio: 1 .. 10
// a new instance's Drive and Feedback steps (the nearest to pinchr's 5 and 5 in level and tone)
constexpr double kDefaultDrive = 4.0, kDefaultFeedback = 3.0;

// Drive and Feedback: step n (1 .. 10) is the position 1 + (n - 1) * 14 / 9 of pinchr's 0 .. 10 scale (the
// measured stage, extrapolated above 10: StageShape.h), so 1 is pinchr's 1 and 10 what 15 would be. The step
// nearest a position (pinchr's states, held to 1 .. 10).
constexpr double stagePosition (double step) { return 1.0 + (step - 1.0) * 14.0 / 9.0; }
int stepNearest (double position);

const pk::ParamTable& paramTable ();
inline double toPlain (uint32_t id, double n) { return paramTable ().toPlain (id, n); }
inline double toNormalized (uint32_t id, double p) { return paramTable ().toNormalized (id, p); }
inline double defaultNormalized (uint32_t id) { return paramTable ().defaultNormalized (id); }

// Menu > Defaults (pluginkit/GentlrDefaults.h): none (the end saturator it set is retired)
inline constexpr pk::GentlrIds kGentlrIds {};

} // namespace sunder
