// Para parameters. IDs are persisted in projects: only ever append.
#pragma once

#include "pluginkit/ParamTable.h"
#include "pluginkit/TailParams.h"

#include <cmath>
#include <cstdint>

namespace para {

enum ParamId : uint32_t
{
    kHpFreq = 0, // Hz, high-pass cutoff (at the root note)
    kHpRes,      // 0 .. 1
    kLpFreq,     // Hz, low-pass cutoff (at the root note)
    kLpRes,      // 0 .. 1
    kSlope,      // 12 / 18 / 24 dB per octave
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
    kHpGain,     // dB, level of the high-pass (minimum = -inf)
    kLpGain,     // dB, level of the low-pass (minimum = -inf)
    kResLink,    // the low-pass uses the high-pass resonance
    kMovement,   // Free / Vocal (see Engine.h)
    kTailBase,   // the Smacheratr at the end of the chain: pk::kTailFields entries
    kDragGain = kTailBase + pk::kTailFields, // editor: dragging a handle up/down moves its gain with the resonance
    kLiquid,     // Vocal movement, plus Split swinging with the leader's sweep (see Engine.h)

    kNumParams
};

// The IDs a plug-in hosting Para (Smempler) reserves for it; the ones after are mapped one by one.
constexpr uint32_t kHostedParams = kTailBase + pk::kTailFields;

enum Slope { kSlope12 = 0, kSlope18, kSlope24 };
enum Movement { kFree = 0, kVocal };

constexpr double kGainMinDb = -70.0; // the bottom of the filter gains is -inf
inline double filterGain (double db) { return db <= kGainMinDb + 0.01 ? 0.0 : std::pow (10.0, db / 20.0); }

constexpr int kDefaultRoot = 60; // C3

// Hidden parameter that receives MIDI pitch bend through IMidiMapping.
enum MidiParamId : uint32_t { kMidiPitchBend = 1000 };

const pk::ParamTable& paramTable ();
inline double toPlain (uint32_t id, double n) { return paramTable ().toPlain (id, n); }
inline double toNormalized (uint32_t id, double p) { return paramTable ().toNormalized (id, p); }
inline double defaultNormalized (uint32_t id) { return paramTable ().defaultNormalized (id); }

} // namespace para
