// Stretchr parameters. IDs are persisted in projects: only ever append.
#pragma once

#include "pluginkit/ParamTable.h"

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

    kNumParams
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
    kNumAlgorithms
};

enum Transients { kCrisp = 0, kMixed, kSmooth };
enum Outside { kThru = 0, kMute };

const pk::ParamTable& paramTable ();
inline double toPlain (uint32_t id, double n) { return paramTable ().toPlain (id, n); }
inline double toNormalized (uint32_t id, double p) { return paramTable ().toNormalized (id, p); }
inline double defaultNormalized (uint32_t id) { return paramTable ().defaultNormalized (id); }

} // namespace stretchr
