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
// Output controls at 0 dB you hear the preset (Input +5.2 dB, band Outputs +24 / +9.1 / +11.3 /
// +11.7 dB, Output -7 dB), and the controls trim around it.
inline constexpr double kBakedInputDb = 5.2;
inline constexpr double kBakedOutputDb[4] = {24.0, 9.1, 11.3, 11.7};
inline constexpr double kBakedMasterDb = -7.0;

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
