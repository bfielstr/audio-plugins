// Tapr parameters. IDs are persisted in projects: only ever append.
#pragma once

#include "pluginkit/ParamTable.h"
#include "pluginkit/TailParams.h"

#include <cstdint>

namespace tapr {

enum ParamId : uint32_t
{
    kTapeOn = 0,  // the tape stage (Drive, the leveller, the curve, Speed's shelf) in the path
    kDrive,       // 0 .. 1: -10 .. +50 dB into the curve
    kSpeed,       // 0 .. 1: the tape speed (Speed's high shelf after the curve)
    kCompression, // 0 .. 1: the leveller
    kCompPlace,   // the leveller before the curve (Pre) or after it (Post)
    kCutOn,       // the Cut EQ in the path
    kCutLo,       // dB, -18 .. +18: the 126 Hz bell
    kCutMid,      // dB: the 983 Hz bell
    kCutHi,       // dB: the 9.8 kHz bell
    kCutPlace,    // the Cut EQ before the tape (Pre: into the saturation) or after it (Post)
    kMix,         // 0 .. 1: dry .. wet
    kOutput,      // dB
    // the Smacheratr at the end of the chain (the suite's end saturator, off by default here: the tape
    // stage is the saturator), every block of it
    kTailBase,
    kTailExtBase = kTailBase + pk::kTailFields,
    kTailExt2Base = kTailExtBase + pk::kTailExtFields,
    kTailExt3Base = kTailExt2Base + pk::kTailExt2Fields,
    kTailExt4Base = kTailExt3Base + pk::kTailExt3Fields,
    kNumParams = kTailExt4Base + pk::kTailExt4Fields
};

// pinned: these numbers are in saved projects
static_assert (kTapeOn == 0 && kDrive == 1 && kSpeed == 2 && kCompression == 3 && kCompPlace == 4 && kCutOn == 5 && kCutLo == 6 &&
                   kCutMid == 7 && kCutHi == 8 && kCutPlace == 9 && kMix == 10 && kOutput == 11 && kTailBase == 12,
               "Tapr's parameter IDs are fixed");
static_assert (kTailExtBase == 18 && kTailExt2Base == 35 && kTailExt3Base == 44 && kTailExt4Base == 50 && kNumParams == 55,
               "saved IDs: the end saturator's blocks at 12 .. 54");

enum Place
{
    kPre = 0,
    kPost
};

const pk::ParamTable& paramTable ();
inline double toPlain (uint32_t id, double n) { return paramTable ().toPlain (id, n); }
inline double toNormalized (uint32_t id, double p) { return paramTable ().toNormalized (id, p); }
inline double defaultNormalized (uint32_t id) { return paramTable ().defaultNormalized (id); }

// Menu > Defaults (pluginkit/GentlrDefaults.h): the parameters Gentlr On by Default and Advanced On by
// Default set in a new instance: the end saturator's Saturator and Gentlr switches and Gentlr's Advanced
inline constexpr pk::GentlrIds kGentlrIds = pk::tailGentlrIds (kTailBase, kTailExtBase, kTailExt2Base);

} // namespace tapr
