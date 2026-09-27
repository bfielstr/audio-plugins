// Multidyn parameters. IDs are persisted in projects: only ever append.
#pragma once

#include "pluginkit/ParamTable.h"

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

    kMode = kBandBase + 4 * 10, // Base / Character (appended after the bands: IDs are persisted)
    kPreLimit,                  // look-ahead limiter on each band's driven input
    kPreLimitCeiling,           // dB

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
