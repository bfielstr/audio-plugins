// Para parameters. IDs are persisted in projects: only ever append (after the gain locks now; the
// end saturator's blocks keep the room they have).
#pragma once

#include "pluginkit/ParamTable.h"
#include "pluginkit/TailParams.h"

#include <cmath>
#include <cstdint>
#include <functional>

namespace para {

enum ParamId : uint32_t
{
    kHpFreq = 0, // Hz, high-pass cutoff (at the root note)
    kHpRes,      // 0 .. 1
    kLpFreq,     // Hz, low-pass cutoff (at the root note)
    kLpRes,      // 0 .. 1
    kHpSlope,    // 6 .. 96 dB per octave or Brickwall, the high-pass's (the one Slope of both filters before
                 // state 5; 12 / 18 / 24 dB before state 4, see slopeFromThreeChoices)
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
    kHpDriveOn = kTailExtBase + 17, // the high-pass branch's drive (Smacheratr's Analog curve, see Engine.h); the one
                                    // drive for both filters until the drive was per band
    kHpDrive,                       // dB into the curve
    kDrivePos,                      // Pre (before each filter) / Post (after it), for both drives
    kTailExt2Base, // Gently's Advanced mode in the end Smacheratr: pk::kTailExt2Fields entries
    // --- after Gently's Advanced block and its Sub band (its room is fixed at 9 from here on) ---
    kLpDriveOn = kTailExt2Base + 9, // the low-pass branch's drive
    kLpDrive,                       // dB into the curve
    // --- after the drives ---
    kLpSlope,    // the low-pass's slope (the same choices as kHpSlope; both filters had kHpSlope's before)
    kHpGainLock, // the high-pass gain never goes above 0 dB (on by default)
    kLpGainLock, // the low-pass gain never goes above 0 dB (off by default)
    // --- after the gain locks ---
    kTailExt3Base, // Gently's High band and No Overlap in the end Smacheratr: pk::kTailExt3Fields entries (the last block)
    kNumParams = kTailExt3Base + pk::kTailExt3Fields
};
static_assert (pk::kTailExtFields <= kHpDriveOn - kTailExtBase, "the end saturator's block grew into the drive's IDs");
static_assert (pk::kTailExt2Fields == kLpDriveOn - kTailExt2Base, "Gently's Advanced block must fill its room: IDs are persisted");
static_assert (kHpDriveOn == 48 && kHpDrive == 49 && kDrivePos == 50 && kTailExt2Base == 51 && kLpDriveOn == 60 && kLpDrive == 61 &&
                   kLpSlope == 62 && kHpGainLock == 63 && kLpGainLock == 64 && kTailExt3Base == 65 && kNumParams == 70,
               "Para's IDs are persisted in projects");

// The IDs a plug-in hosting Para (Smemplr) reserves for it; the ones after are mapped one by one.
constexpr uint32_t kHostedParams = kTailBase + pk::kTailFields;

// The filters' slopes (Slopes.h), in dB per octave. Before 0.7 there were three (12, 18, 24 dB).
enum Slope
{
    kSlope6 = 0,
    kSlope12,
    kSlope18,
    kSlope24,
    kSlope36,
    kSlope48,
    kSlope60,
    kSlope72,
    kSlope84,
    kSlope96,
    kSlopeBrickwall,
    kNumSlopes
};
enum Movement { kFree = 0, kVocal };
enum DrivePos { kDrivePre = 0, kDrivePost };

constexpr double kGainMinDb = -70.0; // the bottom of the filter gains is -inf
inline double filterGain (double db) { return db <= kGainMinDb + 0.01 ? 0.0 : std::pow (10.0, db / 20.0); }
// A filter's gain with its Gain Lock (kHpGainLock / kLpGainLock) on or off: locked, never above 0 dB.
inline double lockedGainDb (double db, bool locked) { return locked ? std::fmin (db, 0.0) : db; }
// The lock of a gain (kHpGain / kLpGain), the gain of a lock; 0 for anything else.
inline uint32_t gainLockOf (uint32_t gainId) { return gainId == kHpGain ? kHpGainLock : gainId == kLpGain ? kLpGainLock : 0; }
inline uint32_t gainOfLock (uint32_t lockId) { return lockId == kHpGainLock ? kHpGain : lockId == kLpGainLock ? kLpGain : 0; }
// What a normalized value of a gain (kHpGain / kLpGain) becomes with its lock on (at most 0 dB's) or off.
double lockedGainNormalized (uint32_t gainId, double norm, bool locked);

// Vocal's fade of the pushed filter (Engine.h vocalPush), as a gain: over = semitones past where the fade
// starts, fadeSemis = Fade. Even in dB: 0 dB to -36 dB in a straight line (in dB) over the Fade (-18 dB half
// way), the last 10 % of it also under a raised-cosine half window from 1 to 0, so it ends silent (-32.4 dB
// at 90 %, -inf at 100 % and past it).
constexpr double kFadeTaperDb = 36.0, kFadeTailFrom = 0.9;
inline double vocalFadeGain (double over, double fadeSemis)
{
    const double x = std::fmin (1.0, std::fmax (0.0, over / std::fmax (0.5, fadeSemis)));
    if (x >= 1.0)
        return 0.0;
    double g = std::pow (10.0, -kFadeTaperDb * x / 20.0);
    if (x > kFadeTailFrom)
        g *= 0.5 * (1.0 + std::cos (M_PI * (x - kFadeTailFrom) / (1.0 - kFadeTailFrom)));
    return g;
}
// Fade's range was 1 .. 36 semitones (log) before state 5 of Para (15 of Smemplr): a normalized value of
// then, as a normalized value now (the same semitones). Its default was 12 semitones (30 now).
constexpr double kFadeOldMax = 36.0, kFadeOldDefault = 12.0;
double fadeFromOldRange (double oldNorm);

constexpr int kDefaultRoot = 60; // C3

// Hidden parameter that receives MIDI pitch bend through IMidiMapping.
enum MidiParamId : uint32_t { kMidiPitchBend = 1000 };

const pk::ParamTable& paramTable ();
inline double toPlain (uint32_t id, double n) { return paramTable ().toPlain (id, n); }
inline double toNormalized (uint32_t id, double p) { return paramTable ().toNormalized (id, p); }
inline double defaultNormalized (uint32_t id) { return paramTable ().defaultNormalized (id); }

// A slope saved over the three choices of before (12 / 18 / 24 dB: normalized 0 / 0.5 / 1), normalized
// over the slopes now (the same slope).
double slopeFromThreeChoices (double oldNorm);
// Settings saved before the per-band drive and the slopes 6 .. 96 dB and Brickwall (Para's state before
// version 4, a Para slot of Smemplr's rack before version 13), made to mean the same: the slope moves to
// its place on the longer list, and the low-pass branch gets the drive the high-pass branch has (it was
// one drive for both: Pre, that is the same sound). get (id, v): the saved normalized value, false when
// there is none; set (id, v) stores one.
void upgradeToPerBandDrive (const std::function<bool (uint32_t, double&)>& get, const std::function<void (uint32_t, double)>& set);
// Settings saved before the separate slopes and the gain locks (Para's state before version 5, a Para slot
// of Smemplr's rack before version 15; after upgradeToPerBandDrive where that applies), made to mean the
// same: the low-pass gets the slope both filters had (the default where none was saved), the high-pass's
// Gain Lock is on unless the saved high-pass gain is above 0 dB (so nothing gets quieter), the low-pass's
// is off, and Fade (1 .. 36 semitones then) keeps its semitones (12, the default then, where none was saved).
void upgradeToSeparateSlopes (const std::function<bool (uint32_t, double&)>& get, const std::function<void (uint32_t, double)>& set);

} // namespace para
