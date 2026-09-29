// Para parameters. IDs are persisted in projects: only ever append. The end saturator's extended
// block (kTailExtBase) was the last; a new Para parameter goes in the block after it (and from then on
// the extended block stays as it is).
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
    kKey,        // unused since 0.6 (Para no longer tracks notes)
    kTranspose,  // unused
    kPbRange,    // unused
    kRoot,       // unused
    kDryWet,
    kOutput,     // dB
    kHpGain,     // dB, level of the high-pass (minimum = -inf)
    kLpGain,     // dB, level of the low-pass (minimum = -inf)
    kResLink,    // the low-pass uses the high-pass resonance
    kMovement,   // Free / Vocal (see Engine.h)
    kTailBase,   // the Smacheratr at the end of the chain: pk::kTailFields entries
    kDragGain = kTailBase + pk::kTailFields, // editor: dragging a handle up/down moves its gain with the resonance
    kLiquid,     // unused: Vocal movement is what Liquid was (see Engine.h)
    kFade,       // semitones: how far past where the dip starts Vocal takes the follower to -inf
    kNotch,      // unused (Liquid's notch is gone)
    kDipStart,   // Hz: Vocal, the low-pass leading - where the high-pass starts to rise and fade
    kLpFloor,    // Hz: the low-pass never goes below this (keeps the sub)
    kTailExtBase, // the rest of the end-of-chain Smacheratr: pk::kTailExtFields entries
    // --- after the end saturator's block (which stays as it is from here on) ---
    kDriveOn = kTailExtBase + 17, // the drive in Para's own path (Smacheratr's Analog curve, see Engine.h)
    kDrive,                       // dB into the curve
    kDrivePos,                    // Pre (before the filters) / Post (after them)
    kNumParams
};
static_assert (pk::kTailExtFields <= kDriveOn - kTailExtBase, "the end saturator's block grew into the drive's IDs");

// The IDs a plug-in hosting Para (Smemplr) reserves for it; the ones after are mapped one by one.
constexpr uint32_t kHostedParams = kTailBase + pk::kTailFields;

enum Slope { kSlope12 = 0, kSlope18, kSlope24 };
enum Movement { kFree = 0, kVocal };
enum DrivePos { kDrivePre = 0, kDrivePost };

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
