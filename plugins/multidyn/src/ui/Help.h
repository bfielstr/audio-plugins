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
        case kLowOn: return "Switch the low band on or off. With Low and High off, Multidyn is a single-band processor (Mid).";
        case kHighOn: return "Switch the high band on or off.";
        case kLowFreq: return "Crossover between the low and mid bands.";
        case kHighFreq: return "Crossover between the mid and high bands.";
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
            case kAboveThresh: return "Above threshold: levels above it are compressed (ratio > 1) or expanded upward (ratio < 1).";
            case kAboveRatio: return "Above ratio. > 1: downward compression (loud gets quieter). < 1: upward expansion (loud gets louder).";
            case kBelowThresh: return "Below threshold: levels under it are expanded downward (ratio > 1) or compressed upward (ratio < 1).";
            case kBelowRatio: return "Below ratio. > 1: downward expansion (quiet gets quieter). < 1: upward compression (quiet gets louder).";
            case kAttack: return "How fast the processing reacts when the level crosses into a block.";
            case kRelease: return "How fast the processing recovers when the level leaves a block.";
            default: break;
        }
    return nullptr;
}

constexpr const char* kDisplay =
    "Drag a block edge left/right to move a threshold. Drag inside a block up (louder) or down (quieter) to set its "
    "ratio. Cmd/Ctrl: all bands. Alt/Option: above and below together. Shift: fine. Double-click a block: 1:1. "
    "Thin bars show input level, thick bars output level.";
constexpr const char* kTabs = "Right column shows: T = attack/release, B = below threshold/ratio, A = above threshold/ratio.";

} // namespace multidyn::help
