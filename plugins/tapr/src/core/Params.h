// Tapr parameters. IDs are persisted in projects: only ever append.
#pragma once

#include "pluginkit/ParamTable.h"
#include "pluginkit/TailParams.h"

#include <cstdint>

namespace tapr {

enum ParamId : uint32_t
{
    kTapeOn = 0,  // the tape stage (Drive, the leveller, the curve, Speed's shelf) in the path
    kDrive,       // 0 .. 2 (0 .. 200 %): -10 .. +110 dB into the curve (0 .. 1 before state version 2)
    kSpeed,       // 0 .. 1: the tape speed (Speed's high shelf after the curve)
    kCompression, // 0 .. 1: the leveller
    kCompPlace,   // the leveller before the curve (Pre) or after it (Post)
    kCutOn,       // the Cut EQ in the path
    kCutLo,       // dB, -18 .. +18: the low bell (126 Hz by default: kCutLoFreq)
    kCutMid,      // dB: the middle bell (983 Hz)
    kCutHi,       // dB: the high bell (9.8 kHz)
    kCutPlace,    // the Cut EQ before the tape (Pre: into the saturation) or after it (Post)
    kMix,         // 0 .. 1: dry .. wet
    kOutput,      // dB
    // the old end saturator's blocks (smacheratr at the end of the chain), retired: kept so saved IDs stay put
    kTailBase,
    kTailExtBase = kTailBase + pk::kTailFields,
    kTailExt2Base = kTailExtBase + pk::kTailExtFields,
    kTailExt3Base = kTailExt2Base + pk::kTailExt2Fields,
    kTailExt4Base = kTailExt3Base + pk::kTailExt3Fields,
    // the Cut bells' centres, Hz (log); Scream's fixed ones by default
    kCutLoFreq = kTailExt4Base + pk::kTailExt4Fields,
    kCutMidFreq,
    kCutHiFreq,
    kSoftClip, // the end of the chain: Soft Clip, then Hard Clip (pluginkit/EndClip.h)
    kHardClip,
    kNumParams
};

// pinned: these numbers are in saved projects
static_assert (kTapeOn == 0 && kDrive == 1 && kSpeed == 2 && kCompression == 3 && kCompPlace == 4 && kCutOn == 5 && kCutLo == 6 &&
                   kCutMid == 7 && kCutHi == 8 && kCutPlace == 9 && kMix == 10 && kOutput == 11 && kTailBase == 12,
               "Tapr's parameter IDs are fixed");
static_assert (kTailExtBase == 18 && kTailExt2Base == 35 && kTailExt3Base == 44 && kTailExt4Base == 50 && kCutLoFreq == 55 &&
                   kCutMidFreq == 56 && kCutHiFreq == 57 && kSoftClip == 58 && kHardClip == 59 && kNumParams == 60,
               "saved IDs: the old end saturator's blocks at 12 .. 54 (retired), the Cut frequencies at 55 .. 57, the clips 58 and 59");

// a Cut band's gain and frequency
constexpr uint32_t cutGainParam (int band) { return kCutLo + (uint32_t)band; }
constexpr uint32_t cutFreqParam (int band) { return kCutLoFreq + (uint32_t)band; }

enum Place
{
    kPre = 0,
    kPost
};

// Drive's top: 200 % (+110 dB into the curve, the same 0.6 dB a percent); 100 % before state version 2
constexpr double kDriveMax = 2.0;

const pk::ParamTable& paramTable ();
inline double toPlain (uint32_t id, double n) { return paramTable ().toPlain (id, n); }
inline double toNormalized (uint32_t id, double p) { return paramTable ().toNormalized (id, p); }
inline double defaultNormalized (uint32_t id) { return paramTable ().defaultNormalized (id); }

// Menu > Defaults (pluginkit/GentlrDefaults.h): none (the end saturator it set is retired)
inline constexpr pk::GentlrIds kGentlrIds {};

} // namespace tapr
