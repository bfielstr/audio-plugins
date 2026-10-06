// Ciphr parameters. IDs are persisted in projects: only ever append.
#pragma once

#include "pluginkit/ParamTable.h"
#include "pluginkit/TailParams.h"

#include <cstdint>

namespace ciphr {

enum ParamId : uint32_t
{
    // GENERATOR
    kTimbre = 0, // 0 .. 1: scans each oscillator's list of (wave, pitch) entries, crossfading neighbours
    kCross,      // -1 .. 1: left FM (phase modulation) between neighbouring oscillators, centre clean, right ring
    kCharacter,  // 0 .. 1: the cluster's detune spread, the processor's tap count and its loop's brightness
    kVariant,    // 1 .. 128: the seed of the oscillators' lists and the processor's tap pattern
    kDrift,      // 0 .. 1: slow glides between random states (0: static)
    kTune,       // semitones
    // INPUT
    kInput,     // 0 .. 1: the stereo input bus's level
    kInputPath, // Direct / Voices
    // FILTER
    kCutoff,     // Hz
    kResonance,  // 0 .. 1
    kFilterType, // 0 .. 1: low-pass, band-pass (0.5), high-pass (1), morphing between them
    kKeyTrack,   // 0 .. 1: how far the cutoff follows the note (1: an octave per octave, from C3)
    kEnvAmount,  // -1 .. 1: the filter envelope's reach (+-5 octaves at the ends)
    // AMP ENVELOPE
    kAttack,  // ms
    kDecay,   // ms
    kSustain, // 0 .. 1
    kRelease, // ms
    // FILTER ENVELOPE
    kFilterAttack,
    kFilterDecay,
    kFilterSustain,
    kFilterRelease,
    kVelocity, // 0 .. 1: how much the velocity sets the level
    // PROCESSOR
    kSpace,    // 0 .. 1: taps only (delay) .. heavy diffusion (reverb)
    kLength,   // ms: the longest tap (every delay time scales with it)
    kMovement, // 0 .. 1: slow modulation of the delay times
    kRegen,    // -1 .. 1: feedback; right of centre shifted only, left shifted plus unshifted
    kShift,    // Hz: the frequency shifter in the feedback path
    // OUTPUT
    kBlend,  // 0 .. 1: dry .. wet
    kOutput, // dB
    // the Smacheratr at the end of the chain (the suite's end saturator), every block of it
    kTailBase,
    kTailExtBase = kTailBase + pk::kTailFields,
    kTailExt2Base = kTailExtBase + pk::kTailExtFields,
    kTailExt3Base = kTailExt2Base + pk::kTailExt2Fields,
    kTailExt4Base = kTailExt3Base + pk::kTailExt3Fields,
    kNumParams = kTailExt4Base + pk::kTailExt4Fields
};

// pinned: these numbers are in saved projects
static_assert (kTimbre == 0 && kCross == 1 && kCharacter == 2 && kVariant == 3 && kDrift == 4 && kTune == 5 && kInput == 6 &&
                   kInputPath == 7 && kCutoff == 8 && kResonance == 9 && kFilterType == 10 && kKeyTrack == 11 &&
                   kEnvAmount == 12 && kAttack == 13 && kDecay == 14 && kSustain == 15 && kRelease == 16,
               "Ciphr's parameter IDs are fixed");
static_assert (kFilterAttack == 17 && kFilterDecay == 18 && kFilterSustain == 19 && kFilterRelease == 20 && kVelocity == 21 &&
                   kSpace == 22 && kLength == 23 && kMovement == 24 && kRegen == 25 && kShift == 26 && kBlend == 27 &&
                   kOutput == 28 && kTailBase == 29,
               "Ciphr's parameter IDs are fixed");
static_assert (kTailExtBase == 35 && kTailExt2Base == 52 && kTailExt3Base == 61 && kTailExt4Base == 67 && kNumParams == 72,
               "saved IDs: the end saturator's blocks at 29 .. 71");

enum InputPath { kPathDirect = 0, kPathVoices };

constexpr int kMinVariant = 1, kMaxVariant = 128;

const pk::ParamTable& paramTable ();
inline double toPlain (uint32_t id, double n) { return paramTable ().toPlain (id, n); }
inline double toNormalized (uint32_t id, double p) { return paramTable ().toNormalized (id, p); }
inline double defaultNormalized (uint32_t id) { return paramTable ().defaultNormalized (id); }

} // namespace ciphr
