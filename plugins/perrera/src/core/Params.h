// Perrera parameters. IDs are persisted in projects: only ever append.
#pragma once

#include "pluginkit/ParamTable.h"

#include <cstdint>

namespace perrera {

enum ParamId : uint32_t
{
    kHpFreq = 0, // Hz, high-pass cutoff (at the root note)
    kHpRes,      // 0 .. 1
    kLpFreq,     // Hz, low-pass cutoff (at the root note)
    kLpRes,      // 0 .. 1
    kSlope,      // 12 / 24 dB per octave
    kSplit,      // semitones: > 0 pushes the filters apart, < 0 brings them together
    kEnvAmount,  // semitones of Split added by the envelope at its peak
    kEnvAttack,  // ms
    kEnvDecay,   // ms
    kKey,        // 0 .. 1: how much both cutoffs follow the played note
    kTranspose,  // semitones
    kPbRange,    // semitones
    kRoot,       // MIDI note where the cutoffs sit at their set frequencies
    kDryWet,
    kOutput,     // dB

    kNumParams
};

enum Slope { kSlope12 = 0, kSlope24 };

constexpr int kDefaultRoot = 60; // C3

// Hidden parameter that receives MIDI pitch bend through IMidiMapping.
enum MidiParamId : uint32_t { kMidiPitchBend = 1000 };

const pk::ParamTable& paramTable ();
inline double toPlain (uint32_t id, double n) { return paramTable ().toPlain (id, n); }
inline double toNormalized (uint32_t id, double p) { return paramTable ().toNormalized (id, p); }
inline double defaultNormalized (uint32_t id) { return paramTable ().defaultNormalized (id); }

} // namespace perrera
