#pragma once

#include "../core/Params.h"

namespace detonatr::help {

inline constexpr const char* kStageStrip =
    "The chain, left to right. Click a stage to show its controls; click its light to turn it on or off (off, it only "
    "delays, so nothing moves in time); drag it sideways to move it in the chain. The Smacheratr at the end stays there.";
inline constexpr const char* kHitView =
    "The last second: grey is the input (lined up with the output), orange the output, in dB. Click to change how much "
    "time it shows.";
inline constexpr const char* kVocoderView = "Each band's level now: the vocoder multiplies each band by this, so the loud ones stand out.";
inline constexpr const char* kSpikeView =
    "How far each band is boosted (or cut) on its transients right now, with the peaks held for a moment.";
inline constexpr const char* kOrbView =
    "The orbs seen from above: you at the bottom, facing up, the swarm's ball ahead at the Distance. Rings every metre "
    "(every 5 m when far).";
inline constexpr const char* kTransientView = "The gain over the last seconds: up where the attacks are raised, down where the sustain is lowered.";
inline constexpr const char* kLimiterView = "The gain reduction over the last seconds.";
inline constexpr const char* kTtmView =
    "Each band's level (grey), the target it is pulled towards (line) and the gain doing it (bar: up raises, down lowers).";
inline constexpr const char* kTapeView = "The two bands' curves: how the level going in (across) comes out (up), at their Drive, Mix and Level.";

namespace detail {
inline const char* transient (uint32_t f)
{
    switch (f)
    {
        case kTrOn: return "This transient modulator (like Oxford TransMod). Off: it only delays.";
        case kTrGain: return "The level going into the process (the signal and the detector). TransMod's Gain.";
        case kTrThreshold: return "Levels under this do not count: a hit only counts for how far it rises above it.";
        case kTrDeadband: return "Differences between the peak and the average smaller than this are left alone.";
        case kTrRatio:
            return "How much the transients are changed: the gain is Ratio times how far the peak is over the average (dB). "
                   "+1: a peak 10 dB over the average comes out 20 dB over it; negative values soften the attacks.";
        case kTrOvershoot: return "How long the gain change lasts after a peak (short: only the leading edges).";
        case kTrRise: return "How fast the peak detector rises (longer leaves out short, initial transients).";
        case kTrRecovery: return "How fast the longer-term average follows the level (the reference the peaks are measured against).";
        case kTrOverdrive: return "Saturation mixed in where the attack is raised: snap and weight.";
        case kTrOutput: return "The level coming out.";
        case kTrMix: return "The processed signal against the input.";
        default: return nullptr;
    }
}
inline const char* limiter (uint32_t f)
{
    switch (f)
    {
        case kLimOn: return "This limiter (like Pro-L2). Off: it only delays.";
        case kLimGain: return "Drive into the limiter: louder, limited to the ceiling.";
        case kLimCeiling: return "The most the output may reach (dBTP with True Peak on).";
        case kLimLookahead: return "How far ahead the limiter looks: the gain glides into each peak over this time (the latency stays the same).";
        case kLimAttack: return "How fast the slow part of the limiter (the body) takes the gain down.";
        case kLimRelease: return "How fast the gain comes back up.";
        case kLimLink: return "0 %: each channel limited on its own; 100 %: both by the louder one.";
        case kLimTruePeak: return "Also holds the peaks between the samples (found at 4x) under the ceiling.";
        default: return nullptr;
    }
}
inline const char* comp (uint32_t f)
{
    switch (f)
    {
        case kCompOn: return "This multiband compressor in TTM style (like Pro-C 3's). Off: it only delays.";
        case kCompThreshold: return "The target each band is pulled towards: louder gets turned down, quieter up (with Auto off).";
        case kCompAutoThreshold: return "The target follows each band's own long-term level instead of the Threshold.";
        case kCompRatio: return "How hard the bands are pulled towards the target.";
        case kCompAttack: return "How fast a cut or a boost comes in. Long: hits pass, and the body after them swells (the pumping).";
        case kCompRelease: return "How fast a cut or a boost lets go.";
        case kCompAutoRelease: return "Program-dependent release: slower on held material, faster on hits (a boost lets go of a hit at once).";
        case kCompKnee:
            return "Blends the two stages: near the target the correction fades out over this many dB (0: every dB is corrected, the "
                   "most pumping).";
        case kCompRange: return "The most a band is turned up or down.";
        case kCompHold: return "How long a cut holds before it lets go.";
        case kCompAutoGain: return "Keeps the stage about as loud as its input.";
        case kCompDry: return "The bands, unprocessed, mixed back in at this level.";
        case kCompXoverLow: return "Where the low band ends and the mid band starts.";
        case kCompXoverHigh: return "Where the mid band ends and the high band starts.";
        case kCompOutput: return "The stage's output level.";
        default: return nullptr;
    }
}
} // namespace detail

inline const char* forParam (uint32_t id)
{
    if (id >= kOrderBase && id < kOrderBase + kNumStages)
        return "Which stage runs at this place in the chain (drag the stages in the strip).";
    for (int i = 0; i < 2; ++i)
    {
        if (id >= kTransientBase[i] && id < kTransientBase[i] + kTrFields)
            return detail::transient (id - kTransientBase[i]);
        if (id >= kLimiterBase[i] && id < kLimiterBase[i] + kLimFields)
            return detail::limiter (id - kLimiterBase[i]);
        if (id >= kCompBase[i] && id < kCompBase[i] + kCompFields)
            return detail::comp (id - kCompBase[i]);
    }
    switch (id)
    {
        case kOutput: return "The level at the end (before the Smacheratr).";
        case kDryWet: return "How much of the processed sound you hear against the input (lined up in time).";

        case kVocOn: return "The vocoder (like MVocoder with the sound on its own side-chain): the sound vocodes itself. Off: it only delays.";
        case kVocBands: return "How many bands the vocoder splits the sound into.";
        case kVocLow: return "The lowest band's frequency.";
        case kVocHigh: return "The highest band's frequency.";
        case kVocOrder: return "How steep the band filters are: one, two or three filter sections each.";
        case kVocAttack: return "How fast each band's level follower rises (short: sharper attacks).";
        case kVocRelease: return "How fast each band's level follower falls.";
        case kVocRatio: return "The input (0 %) against the vocoded sound (100 %), where each band is multiplied by its own level.";

        case kSpkOn: return "A spectral transient processor (like Spiff): finds the transients in each band. Off: it only delays.";
        case kSpkMode: return "Boost the transients, or cut them.";
        case kSpkDepth: return "How far the transients are boosted or cut, at most (1.8 dB per step: 10 is 18 dB).";
        case kSpkSensitivity: return "How readily a rise counts as a transient (higher: smaller ones too).";
        case kSpkDecay: return "How long the boost or cut lasts after each transient.";
        case kSpkSharpness: return "Higher: only the sharpest onsets count.";
        case kSpkDecayTilt: return "Tilts the Decay across the spectrum: positive makes the highs last longer and the lows shorter.";
        case kSpkLink: return "0 %: each channel finds its own transients; 100 %: both follow the louder one.";
        case kSpkLow: return "The lowest band's frequency.";
        case kSpkHigh: return "The highest band's frequency.";
        case kSpkMix: return "The processed signal against the input.";
        case kSpkTrim: return "The stage's output level.";

        case kMotOn: return "A Doppler swarm (like SpinTracer): virtual sources moving round you play the sound. Off: it only delays.";
        case kMotOrbs: return "How many moving sources play the sound.";
        case kMotPattern: return "Orbit: the orbs circle round the centre. Swarm: each wanders on its own smooth path.";
        case kMotSpeed: return "How fast the orbs move (m/s): the faster, the bigger the Doppler pitch shifts.";
        case kMotDistance: return "How far ahead of you the swarm's centre is (m).";
        case kMotRadius: return "How far from the centre the orbs move (m).";
        case kMotSpread: return "How wide the orbs spread across the stereo field.";
        case kMotRandom: return "How much the orbs differ: their speeds, sizes and tilts (orbit), or their paths' rates (swarm).";
        case kMotFloor: return "Adds each orb's reflection off the floor.";
        case kMotMix: return "The swarm against the input.";

        case kTapeOn: return "Two-band tape saturation (like Saturn 2's Warm Tape). Off: it only delays.";
        case kTapeSplit: return "Where the two bands are split (a linear-phase split: no phase turn).";
        case kTapeLowDrive:
        case kTapeHighDrive: return "How hard this band is driven into the tape curve (the level stays about the same).";
        case kTapeLowMix:
        case kTapeHighMix: return "This band saturated against the band clean.";
        case kTapeLowDyn:
        case kTapeHighDyn: return "Positive: louder parts are driven harder (expands); negative: quieter parts are (compresses).";
        case kTapeLowLevel:
        case kTapeHighLevel: return "This band's output level.";
        default: return nullptr;
    }
}

} // namespace detonatr::help
