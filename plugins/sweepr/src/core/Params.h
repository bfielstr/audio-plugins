// Sweepr parameters. IDs are persisted in projects: only ever append.
#pragma once

#include "pluginkit/ParamTable.h"
#include "pluginkit/TailParams.h"

#include <cstdint>

namespace sweepr {

constexpr int kBands = 2; // A and B

// A band's fields: its bell, then its LFO (on the band's Freq). Band b's field f is bandParam (b, f).
enum BandField : uint32_t
{
    kFreq = 0,  // Hz, 20 .. 20k (log)
    kQ,         // 0.1 .. 10 (log)
    kGain,      // dB, -18 .. +18
    kLfoSync,   // on: the rate is a note value locked to the song position; off: free in Hz
    kLfoDiv,    // the synced rate (Division)
    kLfoRate,   // Hz, 0.01 .. 40 (log): the free rate
    kLfoShape,  // Shape
    kLfoDepth,  // octaves, -4 .. +4
    kLfoPhase,  // degrees, 0 .. 360
    kBandFields
};

enum ParamId : uint32_t
{
    kBandA = 0,
    kBandB = kBandA + kBandFields,
    kLink = kBandB + kBandFields, // B's LFO follows A's (A's rate and shape; B's Phase is its offset from A)
    kTapeOn,                      // the tape stage in the path
    kDrive,                       // 0 .. 1
    kSpeed,                       // 0 .. 1: the tape speed (low: darker, more head bump and wow)
    kCompression,                 // 0 .. 1: the squash before the curve
    kMix,                         // 0 .. 1: dry .. wet
    kOutput,                      // dB
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
static_assert (kBandFields == 9 && kBandA == 0 && kBandB == 9 && kLink == 18 && kTapeOn == 19 && kDrive == 20 && kSpeed == 21 &&
                   kCompression == 22 && kMix == 23 && kOutput == 24 && kTailBase == 25,
               "Sweepr's parameter IDs are fixed");
static_assert (kTailExtBase == 31 && kTailExt2Base == 48 && kTailExt3Base == 57 && kTailExt4Base == 63 && kNumParams == 68,
               "saved IDs: the end saturator's blocks at 25 .. 67");

constexpr uint32_t bandParam (int band, uint32_t field) { return (band == 0 ? kBandA : kBandB) + field; }

enum Shape
{
    kSine = 0,
    kTriangle,
    kSawUp,
    kSawDown,
    kSquare,
    kRandom,       // sample and hold: a new random value each cycle
    kSmoothRandom, // glides from one random value to the next over each cycle
    kNumShapes
};

// The synced rates, shortest first (a bar is four beats).
enum Division
{
    kDiv32 = 0,
    kDiv16T,
    kDiv16,
    kDiv16D,
    kDiv8T,
    kDiv8,
    kDiv8D,
    kDiv4T,
    kDiv4,
    kDiv4D,
    kDiv2T,
    kDiv2,
    kDiv2D,
    kDiv1Bar,
    kDiv2Bars,
    kDiv4Bars,
    kNumDivisions
};
// one cycle of the LFO in beats (quarter notes)
double divisionBeats (int division);

const pk::ParamTable& paramTable ();
inline double toPlain (uint32_t id, double n) { return paramTable ().toPlain (id, n); }
inline double toNormalized (uint32_t id, double p) { return paramTable ().toNormalized (id, p); }
inline double defaultNormalized (uint32_t id) { return paramTable ().defaultNormalized (id); }

// Menu > Defaults (pluginkit/GentlrDefaults.h): the parameters Gentlr On by Default and Advanced On by
// Default set in a new instance: the end saturator's Saturator and Gentlr switches and Gentlr's Advanced
inline constexpr pk::GentlrIds kGentlrIds = pk::tailGentlrIds (kTailBase, kTailExtBase, kTailExt2Base);

} // namespace sweepr
