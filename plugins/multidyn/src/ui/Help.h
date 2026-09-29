// Hover help for Multidyn (toggle with the "?" button).
#pragma once

#include "../core/Params.h"

namespace multidyn::help {

inline const char* forParam (uint32_t id)
{
    switch (id)
    {
        case kOutput: return "Overall output level.";
        case kAmount: return "Scales all compression and expansion. At 0% every ratio behaves like 1:1 (no effect).";
        case kTime: return "Scales every Attack and Release time together, keeping their proportions.";
        case kPreLimit:
            return "A 1 ms look-ahead limiter on each band's driven input, with its ceiling relative to the band's "
                   "Above threshold: a transient pushed hard into the thresholds is held where the compressor will "
                   "settle instead of passing through at full level until the attack catches up, so it reaches "
                   "whatever follows (a saturator) at the same level as the body.";
        case kPreLimitCeiling: return "Pre-limiter ceiling relative to each band's Above threshold (0 dB = right at it).";
        case kSatOn: return "The built-in Smacheratr after the Output gain: the usual chain in one device.";
        case kSatPreLimit:
            return "A look-ahead limiter before the saturator's drive: the signal is held at the threshold, so a "
                   "transient cannot push further into the curve than the rest of the sound.";
        case kSatPreLimitThreshold: return "Level the saturator's pre-limiter holds the signal to, before the drive.";
        case kSatDrive: return "Gain into the built-in saturator's Analog curve (after its pre-limiter).";
        case kSatPostClip: return "Clip the saturator's output at 0 dB (Soft: Analog Clip curve, Hard: digital).";
        case kSatMix: return "Dry/wet of the built-in saturator.";
        case kSoftKnee: return "Soft Knee: processing starts gradually as the level approaches a threshold.";
        case kDetector: return "Peak reacts to short peaks. RMS reacts to average level and ignores very short transients.";
        case kRmsWindow:
            return "RMS detector: how long a stretch of audio the level is averaged over. Short follows the audio closely, "
                   "long is smoother and lets transients through. 50 ms by default.";
        case kSoften:
            return "Keeps a squashed top band from sounding noisy and grainy. The closer the top band's Below threshold is "
                   "to its Above threshold, the more the hiss and air that upward compression lifts is softened (a low-pass "
                   "that reaches down to 3.5 kHz, and up to 6 dB less of it, on the lifted part only), and the gain "
                   "changes are rounded off. Fully at work 6 dB apart or closer "
                   "(the defaults), nothing happens 18 dB or more apart.";
        case kBands: return "Number of frequency bands (1 = a single full-range processor).";
        case kXover1: return "Crossover between bands 1 and 2.";
        case kXover2: return "Crossover between bands 2 and 3.";
        case kXover3: return "Crossover between bands 3 and 4.";
        case kScOn: return "Use the side-chain input (route another track to this plug-in's inputs 3/4) to trigger the processing.";
        case kScGain: return "Level of the side-chain signal feeding the detectors (it is never heard).";
        case kScMix: return "Detector blend: 0% = the processed signal itself, 100% = only the side-chain.";
        case kScListen: return "Listen to the side-chain signal instead of the output while setting it up.";
        default: break;
    }
    if (id >= kBandBase && id < kNumParams)
        switch ((id - kBandBase) % kBandBlock)
        {
            case kBandActive: return "Band on/off. Off bypasses this band's dynamics and gain controls.";
            case kBandSolo: return "Solo: hear only the soloed band(s).";
            case kBandInput: return "Band level before the dynamics (changes how hard it hits the thresholds).";
            case kBandOutput: return "Band level after the dynamics.";
            case kAboveThresh: return "Above threshold: levels above it are compressed (1:x with x > 1) or expanded upward (x < 1).";
            case kAboveRatio: return "Above ratio 1:x. x > 1: downward compression (loud gets quieter); 1:inf limits. x < 1: upward expansion (loud gets louder).";
            case kBelowThresh: return "Below threshold: levels under it are compressed upward (1:x with x > 1) or expanded downward (x < 1).";
            case kBelowRatio: return "Below ratio 1:x. x > 1: upward compression (quiet gets louder, up to +36 dB). x < 1: downward expansion (quiet gets quieter).";
            case kAttack: return "How fast the level envelopes follow the signal into a block (rising for Above, falling for Below).";
            case kRelease: return "How fast the level envelopes follow the signal out of a block.";
            default: break;
        }
    return nullptr;
}

constexpr const char* kDisplay =
    "Drag a block edge left/right to move a threshold. Drag inside a block up (louder) or down (quieter) to set its "
    "ratio. Cmd/Ctrl: all bands. Alt/Option: above and below together. Shift: fine. Double-click a block: 1:1. "
    "Thin bars show input level, thick bars output level; the number in a block is the gain it applies at its "
    "extreme (silence for Below, 0 dB for Above). The value fields beside the lanes can be dragged up/down.";

} // namespace multidyn::help
