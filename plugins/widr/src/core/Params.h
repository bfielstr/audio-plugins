// Widr parameters. IDs are persisted in projects: only ever append.
#pragma once

#include "pluginkit/ParamTable.h"
#include "pluginkit/TailParams.h"

#include <cstdint>

namespace widr {

enum ParamId : uint32_t
{
    kWidth = 0,  // 0 .. 2 (0 = bypass, 1 = 100 %)
    kCharacter,  // Tight / Wide / Epic / Surround
    kSize,       // 0 .. 1: the Haas delay and the early-reflection pattern
    kSpace,      // 0 .. 1: the short stereo reverb
    kDecay,      // ms
    kPreDelay,   // ms
    kDamping,    // Hz
    kAir,        // dB, high shelf on the side
    kBeyond,     // 0 .. 1: side lift around 4 kHz (images beyond the speakers)
    kMonoBelow,  // Hz: mono below this
    kGuard,      // 0 .. 1: mono guard
    kRole,       // Anchor / Support / Wide / Ambient
    kAware,      // 0 .. 1: how much this instance yields to the others in its group
    kGroup,      // 1 .. 8
    kMonoCheck,  // listen in mono
    kOutput,     // dB
    kTailBase,   // the Smacheratr at the end of the chain: pk::kTailFields entries

    kNumParams = kTailBase + pk::kTailFields
};

enum Character { kTight = 0, kWide, kEpic, kSurround, kNumCharacters };
enum Role { kAnchor = 0, kSupport, kWideRole, kAmbient, kNumRoles };

const pk::ParamTable& paramTable ();
inline double toPlain (uint32_t id, double n) { return paramTable ().toPlain (id, n); }
inline double toNormalized (uint32_t id, double p) { return paramTable ().toNormalized (id, p); }
inline double defaultNormalized (uint32_t id) { return paramTable ().defaultNormalized (id); }

} // namespace widr
