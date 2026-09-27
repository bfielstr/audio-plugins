// Hover help for Multidyn (toggle with the "?" button).
#pragma once

#include "Params.h"

namespace multidyn::help {

inline const char* forParam (uint32_t id)
{
    switch (id)
    {
        case kOutput: return "Overall output level.";
        case kAmount: return "Scales all compression and expansion. At 0% every ratio behaves like 1:1 (no effect).";
        case kTime: return "Scales every Attack and Release time together, keeping their proportions.";
        case kSoftKnee: return "Soft Knee: processing starts gradually as the level approaches a threshold.";
        case kDetector: return "Peak reacts to short peaks. RMS reacts to average level and ignores very short transients.";
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
