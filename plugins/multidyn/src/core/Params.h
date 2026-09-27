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
    kLowOn,
    kHighOn,
    kLowFreq,
    kHighFreq,
    kScOn,
    kScGain,
    kScMix,
    kScListen,
    kBandBase, // 3 bands x kBandBlock, see bandParam()

    kNumParams = kBandBase + 3 * 10
};

enum Band { kLow = 0, kMid, kHigh };
constexpr int kNumBands = 3;
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

const pk::ParamTable& paramTable ();
inline double toPlain (uint32_t id, double n) { return paramTable ().toPlain (id, n); }
inline double toNormalized (uint32_t id, double p) { return paramTable ().toNormalized (id, p); }
inline double defaultNormalized (uint32_t id) { return paramTable ().defaultNormalized (id); }
inline std::string toText (uint32_t id, double p) { return paramTable ().toText (id, p); }

} // namespace multidyn
