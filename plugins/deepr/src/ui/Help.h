#pragma once

#include "Params.h"

namespace deepr::help {

inline const char* forParam (uint32_t id)
{
    switch (id)
    {
        case kDepth:
            return "The most the low mids are turned down while the sub plays (reached 12 dB over the Threshold). "
                   "More depth: a deeper, heavier-sounding bass by contrast, without making the track louder.";
        case kDipFreq: return "The centre of the band that is dipped: where the bass sounds boxy or muddy (often 150 - 400 Hz).";
        case kDipWidth: return "How wide the dipped band is, in octaves. Narrow: a surgical notch. Wide: the whole low-mid area.";
        case kThreshold:
            return "The sub level where the dip starts. It deepens as the sub gets louder and reaches full Depth 12 dB above this.";
        case kAttack: return "How fast the dip comes in when a sub note starts. Short: tight; longer lets the note's attack through.";
        case kRelease: return "How fast the low mids come back after the sub stops. Longer: smoother, shorter: more pumping.";
        case kSplit: return "Everything below this is the sub: it drives the dip and is shaped by Mono Sub and Sub Gain.";
        case kMonoSub:
            return "Folds the sub's stereo width into the middle, so detuned or wide layers stop cancelling each other down low.";
        case kSubGain: return "Level of the sub band.";
        case kListen: return "Off: the result. Sub: hear only the sub band (what drives the dip). Cut: hear only what is taken out.";
        case kMix: return "Blend of the processed and the untouched signal.";
        case kOutput: return "Overall output level.";
        default: return nullptr;
    }
}

constexpr const char* kDisplay =
    "Shaded copper: the sub band (drag its edge to set Split). Dashed: the dip at full Depth; lit (cinnabar): the dip it is making now. "
    "Drag the handle sideways for Dip Freq, up/down for Depth; the mouse wheel while you hold it (or with Shift over it), or Alt-drag sideways, for Dip Width. "
    "Right: the sub's level with the Threshold (drag it) and the range where the dip deepens. "
    "Double-click or right-click resets. Shift: fine.";

} // namespace deepr::help
