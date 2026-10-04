// Stretchr parameters. IDs are persisted in projects: only ever append.
#pragma once

#include "pluginkit/ParamTable.h"
#include "pluginkit/TailParams.h"

#include <cstdint>

namespace stretchr {

enum ParamId : uint32_t
{
    kAlgorithm = 0,
    kPitch,            // semitones
    kFine,             // cents
    kFormant,          // semitones the spectral envelope moves by
    kPreserveFormants, // keep the envelope in place while the pitch moves
    kSpeed,            // playback rate: 0.5 = twice as long
    kFollowTempo,      // speed = host tempo / source tempo
    kSourceBpm,
    kWindow,           // ms, Simple Windowed / Balanced grain size
    kTransients,       // Polyphonic frame size: Crisp / Mixed / Smooth
    kSmear,            // s, Extreme analysis window
    kGain,             // dB
    kOutside,          // what the input does outside the clip: Thru / Mute
    kTailBase,         // the Smacheratr at the end of the chain: pk::kTailFields entries (not render settings)
    kTrigger = kTailBase + pk::kTailFields, // On Play: the clip starts when the host starts; Timeline: where it is
    kStereo,           // Extreme and Alien: Wide (left and right apart) / Same (one channel, duplicated)
    kTailExtBase,      // the rest of the end-of-chain Smacheratr: pk::kTailExtFields entries (not render settings)
    kTailExt2Base = kTailExtBase + pk::kTailExtFields, // Gentlr's Advanced mode in the end Smacheratr: pk::kTailExt2Fields entries (not render settings)
    kTailExt3Base = kTailExt2Base + pk::kTailExt2Fields, // Gentlr's High band and No Overlap in it: pk::kTailExt3Fields entries (the last block)
    kNumParams = kTailExt3Base + pk::kTailExt3Fields
};

enum Algorithm
{
    kWindowed = 0, // plain overlap-add
    kBalanced,     // waveform-similarity overlap-add (WSOLA)
    kPolyphonic,   // phase-locked vocoder, optional formant correction
    kSoloist,      // pitch-synchronous overlap-add for monophonic material
    kBeats,        // transient-bounded segments
    kExtreme,      // spectral smearing for very long stretches
    kTape,         // varispeed: pitch follows speed
    kAlien,        // a granular cloud: scattered, sometimes reversed, slightly detuned grains
    kNumAlgorithms
};
constexpr int kAlgorithmsBefore06 = 7; // older projects stored the choice over 7 entries

enum Trigger { kOnPlay = 0, kTimeline };
enum StereoMode { kStereoWide = 0, kStereoSame };

enum Transients { kCrisp = 0, kMixed, kSmooth };
enum Outside { kThru = 0, kMute };

// The end-of-chain saturator's parameters (both blocks): the processor handles them, not the session.
constexpr bool isTailParam (uint32_t id)
{
    return (id >= kTailBase && id < kTailBase + pk::kTailFields) || (id >= kTailExtBase && id < kTailExtBase + pk::kTailExtFields) ||
           (id >= kTailExt2Base && id < kTailExt2Base + pk::kTailExt2Fields) ||
           (id >= kTailExt3Base && id < kTailExt3Base + pk::kTailExt3Fields);
}
// Its field in smacheratr::Tail.
constexpr uint32_t tailField (uint32_t id)
{
    return id >= kTailExt3Base  ? pk::kTailFields + pk::kTailExtFields + pk::kTailExt2Fields + (id - kTailExt3Base)
           : id >= kTailExt2Base ? pk::kTailFields + pk::kTailExtFields + (id - kTailExt2Base)
           : id >= kTailExtBase ? pk::kTailFields + (id - kTailExtBase)
                                : id - kTailBase;
}

const pk::ParamTable& paramTable ();
inline double toPlain (uint32_t id, double n) { return paramTable ().toPlain (id, n); }
inline double toNormalized (uint32_t id, double p) { return paramTable ().toNormalized (id, p); }
inline double defaultNormalized (uint32_t id) { return paramTable ().defaultNormalized (id); }

} // namespace stretchr
