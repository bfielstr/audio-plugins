// Multidyn parameters. IDs are persisted in projects: only ever append.
#pragma once

#include "pluginkit/ParamTable.h"
#include "pluginkit/TailParams.h"

#include <cstdint>
#include <string>

namespace multidyn {

enum ParamId : uint32_t
{
    kOutput = 0,
    kAmount,
    kTime,
    kSoftKnee,
    kDetector, // Peak / RMS
    kBands,    // 1..4 (choice index 0..3)
    kXover1,   // crossover frequencies, used in order: N bands use the first N-1
    kXover2,
    kXover3,
    kScOn,
    kScGain,
    kScMix,
    kScListen,
    kBandBase, // kMaxBands x kBandBlock, see bandParam(); band 0 is the lowest

    kMode = kBandBase + 4 * 10, // unused: Multidyn always works as the former Character mode (IDs are persisted)
    kPreLimit,                  // look-ahead limiter on each band's driven input
    kPreLimitCeiling,           // dB relative to the band's Above threshold
    kSatOn,                     // the built-in Smacheratr after Output
    kSatPreLimit,               // its pre-limiter before the drive
    kSatDrive,                  // dB
    kSatPostClip,               // No / Soft / Hard Clip
    kSatMix,                    // dry/wet
    kSatPreLimitThreshold,      // dB
    kRmsWindow,                 // ms, the RMS detector's window (Character: 2.5 times as long)
    kSoften,                    // the top band: softens what upward compression lifts as its thresholds close in
    kSatExtBase,                // the rest of the built-in Smacheratr: pk::kTailExtFields entries
    kSatExt2Base = kSatExtBase + pk::kTailExtFields, // Gently's Advanced mode in it: pk::kTailExt2Fields entries
    kXoverSlope = kSatExt2Base + pk::kTailExt2Fields, // the crossovers' slope (an XoverSlope, Crossover.h)
    kSoftenColor,                                     // Soften's Color: Smacheratr's high colour after Output (see Engine.h)
    kNumParams
};

constexpr int kMaxBands = 4;
constexpr int kNumBands = kMaxBands;
constexpr int kBandBlock = 10;
enum BandField
{
    kBandActive = 0,
    kBandSolo,
    kBandInput,
    kBandOutput,
    kAboveThresh,
    kAboveRatio,
    kBelowThresh,
    kBelowRatio,
    kAttack,
    kRelease
};
constexpr uint32_t bandParam (int band, int field) { return (uint32_t)(kBandBase + band * kBandBlock + field); }

// The preset's gain staging is baked into the processing: with the band Input, band Output and
// Output controls at 0 dB you hear the preset (Live's OTT: band Outputs +10.3 / +5.7 / +10.3 dB, and
// the fourth band, when there is one, like the top band of three: +10.3 dB), and the controls trim
// around it.
inline constexpr double kBakedInputDb = 0.0;
inline constexpr double kBakedOutputDb[4] = {10.3, 5.7, 10.3, 10.3};
inline constexpr double kBakedMasterDb = 0.0;

// Before the OTT defaults (state version 3 and older; Smemplr before 12, Detonatr before 2) the baked
// gains were an "OTT pushed further": Input +5.2 dB, band Outputs +24 / +9.1 / +11.3 / +11.7 dB,
// Output -7 dB. An old project keeps its sound by moving the difference into its controls.
inline constexpr double kOldBakedInputDb = 5.2;
inline constexpr double kOldBakedOutputDb[4] = {24.0, 9.1, 11.3, 11.7};
inline constexpr double kOldBakedMasterDb = -7.0;
// How much an old state's plain value of this parameter moves (dB; 0 for every other parameter).
double oldBakedShiftDb (uint32_t id);
// An old state's normalized value of parameter id as it is now: moved by oldBakedShiftDb and held
// in the parameter's range (a trim past the range's end, e.g. a low band Output above +10.3 dB on top
// of the old +24, stops at +24 dB: about the only way an old project sounds different).
double migrateOldBakedNorm (uint32_t id, double norm);

enum DetectorMode { kPeak = 0, kRms };
enum Mode { kBase = 0, kCharacter };

// Ratios are written Live-style as "1 : r": r > 1 compresses (Above: loud gets quieter, Below:
// quiet gets louder), r < 1 expands, and the maximum is treated as infinity (limiting).
constexpr double kRatioMin = 0.25;
constexpr double kRatioInf = 1000.0;

const pk::ParamTable& paramTable ();
inline double toPlain (uint32_t id, double n) { return paramTable ().toPlain (id, n); }
inline double toNormalized (uint32_t id, double p) { return paramTable ().toNormalized (id, p); }
inline double defaultNormalized (uint32_t id) { return paramTable ().defaultNormalized (id); }
inline std::string toText (uint32_t id, double p) { return paramTable ().toText (id, p); }

} // namespace multidyn
