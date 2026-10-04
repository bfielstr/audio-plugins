#pragma once

#include "Params.h"

namespace dropr::help {

inline const char* forParam (uint32_t id)
{
    if (id >= kXover1 && id < kXover1 + kNumXovers)
        return "Where two neighbouring bands meet (Linkwitz-Riley, 24 dB/oct: the bands add up flat). "
               "Drag its handle in the display.";
    if (id >= kBandGain1 && id < kBandGain1 + kMaxBands)
        return "This band's own gain after its compression (its point in the display), added to Tilt and Makeup.";
    switch (id)
    {
        case kInput:
            return "Gain before the bands. Pushing the input far over the thresholds (+30 dB by default) is what makes "
                   "the compression hit so hard. The dry signal is taken before this gain.";
        case kBands:
            return "How many bands (1 to 6). With fewer, the top band takes everything above the last crossover in use. "
                   "Changing it crossfades over 20 ms.";
        case kMode:
            return "Stereo: each band compresses left and right. Mid-Side: each band compresses mid and side "
                   "(the change dips the output for 10 ms).";
        case kLink:
            return "How far the two channels (left / right, or mid / side) share their compression: 100 %: one gain "
                   "for both, from the louder one; 0 %: each channel on its own.";
        case kAdaptive:
            return "Program-dependent timing: the larger the move the gain has to make, the faster Attack and Release "
                   "run (at 100 % up to 4 times as fast for moves of 24 dB or more, at 50 % 2.5 times). "
                   "Small moves keep the set times. 0 %: always the set times.";
        case kAttack: return "How fast a band's gain goes down when the level rises (the time it takes to get 63 % of the way).";
        case kRelease: return "How fast a band's gain comes back up when the level falls (63 % of the way in this time).";
        case kDownThreshold:
            return "Above this level the bands are turned down (the solid threshold line in the display; drag it).";
        case kDownRatio:
            return "Downward ratio, 1 : 1 (nothing) to 1 : inf (nothing gets over the threshold). "
                   "Turn on Negative for ratios beyond infinity.";
        case kNegative:
            return "Negative ratios: above the threshold, the louder the input the QUIETER the output. "
                   "Down to the floor Range dB below the threshold.";
        case kNegRatio:
            return "Negative ratio 1 : -x: every dB the level rises over the threshold takes the output x dB down "
                   "(1 : -1 mirrors it at the threshold). 1 : -inf drops everything over the threshold straight to the "
                   "floor, so loud and quiet come out at the same level: hits are clamped and their body comes up.";
        case kRange:
            return "Negative mode's floor: how far below the downward threshold the output may be pushed (the dotted line).";
        case kUpThreshold:
            return "Below this level the bands are turned up (the dashed threshold line in the display; drag it).";
        case kUpRatio:
            return "Upward ratio: below the upward threshold quiet sounds are brought up, 1 : 1 (off) to 1 : inf "
                   "(up to the threshold). At most 30 dB.";
        case kKnee: return "Softens the corner at the thresholds over this many dB.";
        case kTilt:
            return "Tilts the bands' gains across the spectrum: dB per octave from 1 kHz (positive: brighter).";
        case kMakeup: return "Gain on every band after its compression (into the saturator).";
        case kMix: return "Blend of the compressed bands (wet) and the input before the Input gain (dry).";
        case kOutput: return "Level after the dry / wet blend, before the saturator.";
        default: return nullptr;
    }
}

constexpr const char* kDisplay =
    "The bands across the spectrum. Drag a crossover handle sideways to move it, a band's point up or down "
    "for its gain, the solid (downward) or dashed (upward) threshold line up or down. Right-click or "
    "double-click one to reset it. Bars per band: its level after the Input gain (dim), after its gain "
    "(lit), and outlined between them the gain reduction from 0 dB.";

} // namespace dropr::help
