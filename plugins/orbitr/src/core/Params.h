// Orbitr parameters. IDs are persisted in projects: only ever append.
#pragma once

#include "pluginkit/ParamTable.h"
#include "pluginkit/TailParams.h"

#include <cstdint>

namespace orbitr {

enum ParamId : uint32_t
{
    kOrbs = 0,  // 1 .. 16 moving sources
    kPattern,   // Orbit / Swarm
    kSpeed,     // m/s
    kDistance,  // m: the centre of the motion, ahead of the listener
    kRadius,    // m: how far from the centre the orbs move
    kSpread,    // 0 .. 1: centred .. full width (and the ears' spacing)
    kRandom,    // 0 .. 1: how much the orbs differ
    kFloor,     // each orb's reflection off the floor
    kMix,       // 0 .. 1: the orbs against the input (inside the effect, as Detonatr's Motion Mix)
    kDryWet,    // 0 .. 1: the whole effect against the input
    kOutput,    // dB
    kTailBase,  // the Smacheratr at the end of the chain: pk::kTailFields entries
    kTailExtBase = kTailBase + pk::kTailFields,        // the rest of it: pk::kTailExtFields entries
    kTailExt2Base = kTailExtBase + pk::kTailExtFields, // Gently's Advanced mode and Sub band: pk::kTailExt2Fields (the last block)
    kNumParams = kTailExt2Base + pk::kTailExt2Fields
};

// pinned: these numbers are in saved projects
static_assert (kPattern == 1 && kSpeed == 2 && kDistance == 3 && kRadius == 4 && kSpread == 5 && kRandom == 6 && kFloor == 7 &&
                   kMix == 8 && kDryWet == 9 && kOutput == 10 && kTailBase == 11,
               "Orbitr's parameter IDs are fixed");

enum Pattern { kPatternOrbit = 0, kPatternSwarm };

// The defaults: Detonatr's Motion stage as it shipped, Tonsturm SpinTracer's "Liquid Debris"-like
// setting (an estimate: the user never sent that preset's numbers).
struct Defaults
{
    int orbs, pattern;
    double speed, distance, radius, spread, randomness;
    bool floor;
    double mix;
};
constexpr Defaults kLiquidDebris {.orbs = 6, .pattern = kPatternSwarm, .speed = 18.0, .distance = 3.0, .radius = 2.0, .spread = 0.8,
                                  .randomness = 0.6, .floor = true, .mix = 0.5};

const pk::ParamTable& paramTable ();
inline double toPlain (uint32_t id, double n) { return paramTable ().toPlain (id, n); }
inline double toNormalized (uint32_t id, double p) { return paramTable ().toNormalized (id, p); }
inline double defaultNormalized (uint32_t id) { return paramTable ().defaultNormalized (id); }

} // namespace orbitr
