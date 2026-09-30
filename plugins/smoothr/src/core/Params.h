// Smoothr parameters. IDs are persisted in projects: only ever append. The saturator's blocks come
// after Smoothr's own, Gently's Advanced block last; a new Smoothr parameter goes in a block after it
// (and from then on that block stays as it is).
#pragma once

#include "pluginkit/ParamTable.h"
#include "pluginkit/TailParams.h"

#include <cstdint>

namespace smoothr {

static_assert (pk::kTailFields == 6, "Smoothr's saturator block is six fields long");

enum ParamId : uint32_t
{
    kInput = 0,   // dB, gain into the chain (how hard it is pushed into the limiter)
    kCeiling,     // dB, the most the output ever reaches (the output level: there is no gain after it)
    kRelease,     // ms, how fast the limiter lets go (the lows let go slower, see Limiter.h)
    kAutoRelease, // program-dependent release: sustained limiting lets go slower than a single peak
    kSmooth,      // 0..1, how much the lows are kept out of the limiting (the highs take the transients)
    kCharacter,   // 0..1, the dynamic dip in the low mids before the limiter (0: none; Character.h)
    kTailBase,                                  // the Smacheratr before the limiter: pk::kTailFields entries
    kTailExtBase = kTailBase + pk::kTailFields, // the rest of that Smacheratr
    kTailExt2Base = kTailExtBase + pk::kTailExtFields, // its Gently's Advanced mode: pk::kTailExt2Fields entries (the last block)
    kNumParams = kTailExt2Base + pk::kTailExt2Fields
};

constexpr bool isTailParam (uint32_t id)
{
    return (id >= kTailBase && id < kTailBase + pk::kTailFields) || (id >= kTailExtBase && id < kTailExtBase + pk::kTailExtFields) ||
           (id >= kTailExt2Base && id < kTailExt2Base + pk::kTailExt2Fields);
}
constexpr uint32_t tailField (uint32_t id)
{
    return id >= kTailExt2Base  ? pk::kTailFields + pk::kTailExtFields + (id - kTailExt2Base)
           : id >= kTailExtBase ? pk::kTailFields + (id - kTailExtBase)
                                : id - kTailBase;
}

const pk::ParamTable& paramTable ();
inline double toPlain (uint32_t id, double n) { return paramTable ().toPlain (id, n); }
inline double toNormalized (uint32_t id, double p) { return paramTable ().toNormalized (id, p); }
inline double defaultNormalized (uint32_t id) { return paramTable ().defaultNormalized (id); }

} // namespace smoothr
