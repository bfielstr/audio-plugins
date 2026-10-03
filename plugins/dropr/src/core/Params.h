// Dropr parameters. IDs are persisted in projects: only ever append.
#pragma once

#include "pluginkit/ParamTable.h"
#include "pluginkit/TailParams.h"

#include <cstdint>

namespace dropr {

constexpr int kMaxPoints = 8;
enum PointField : uint32_t { kPtX = 0, kPtY, kPtCurve };

enum ParamId : uint32_t
{
    kSensitivity = 0, // dB a hit must jump over the recent level to start the shape
    kRetrigger,       // ms: the shortest time between two hits
    kLength,          // ms the shape takes
    kDepth,           // dB at the bottom of the shape (its top is 0 dB)
    kPre,             // ms the shape starts before the hit (0 .. kLookaheadMs)
    kMix,             // dry / wet
    kOutput,          // dB
    kPointCount,      // 2 .. kMaxPoints
    kPoints,          // kMaxPoints x (time 0..1, level 0..1, curve -1..1 of the segment after it)
    kTailBase = kPoints + kMaxPoints * 3, // the Smacheratr at the end of the chain: pk::kTailFields entries
    kTailExtBase = kTailBase + pk::kTailFields,        // the rest of it: pk::kTailExtFields entries
    kTailExt2Base = kTailExtBase + pk::kTailExtFields, // Gently's Advanced mode and Sub band: pk::kTailExt2Fields (the last block)
    kNumParams = kTailExt2Base + pk::kTailExt2Fields
};

constexpr uint32_t pointParam (int point, uint32_t field) { return kPoints + (uint32_t)point * 3 + field; }

// The look-ahead every signal goes through (the latency, with the end saturator's): Pre can start the
// shape up to this long before the hit.
constexpr double kLookaheadMs = 5.0;

const pk::ParamTable& paramTable ();
inline double toPlain (uint32_t id, double n) { return paramTable ().toPlain (id, n); }
inline double toNormalized (uint32_t id, double p) { return paramTable ().toNormalized (id, p); }
inline double defaultNormalized (uint32_t id) { return paramTable ().defaultNormalized (id); }

} // namespace dropr
