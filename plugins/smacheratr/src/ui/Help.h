#pragma once

#include "../core/Params.h"

namespace smacheratr::help {

inline const char* forParam (uint32_t id)
{
    switch (id)
    {
        case kDrive: return "Gain into the Analog curve. The display shows how far the driven signal reaches into it.";
        case kPreLimit:
            return "A look-ahead limiter before the drive: the input is held at the threshold, so a transient cannot "
                   "push further into the curve than the rest of the sound. The drive is applied after it.";
        case kPreLimitThreshold: return "Level the pre-limiter holds the input to, before the drive.";
        case kPostClip:
            return "No Clip, or clips the output at 0 dB after the curve (Soft: the Analog curve again, Hard: a "
                   "digital clip), so the output never exceeds the Output level. Useful with negative Color amounts.";
        case kColorOn:
            return "Enables the colour filters: an EQ applied before the curve and undone after it, so it changes "
                   "how much of each frequency range is saturated, not the balance of the output.";
        case kColorLo:
            return "Saturation applied to the low end (a shelf below 100 Hz, +-24 dB at +-100 %): negative values keep "
                   "the bass clean and let its energy through, positive values saturate it more.";
        case kColorHi: return "Saturation applied around Freq: positive values add more, negative values less.";
        case kColorFreq: return "Centre frequency of the second colour filter.";
        case kColorWidth: return "Width of the second colour filter (larger = wider).";
        case kOutput: return "Final output attenuation.";
        case kDryWet: return "Balance between the dry input and the saturated signal. Use 100 % on a return track.";
        case kHiQuality: return "Runs the curve 4x oversampled to reduce aliasing (a little more CPU).";
        case kDcFilter: return "Removes DC offset from the input before the curve.";
        case kClarity:
            return "Keeps a hard-pushed drive from going muddy: when the low mids (around 320 Hz) hit the curve hard "
                   "they are turned down before it (up to 8 dB, only when pushed), and the lows go into the curve "
                   "4 dB down and are lifted back after it, so the bass drives it less but keeps its level.";
        case kMidSide:
            return "Saturate the mid and the side apart: the side is driven by its own, lower level, so a wide sound "
                   "stays wide when you push the drive (Menu).";
        default: return nullptr;
    }
}

constexpr const char* kShaperDisplay =
    "The Analog curve: input left to right, output bottom to top, with the clipping points at +-1. The bright part "
    "shows where the driven signal sits on the curve; with Pre-Limit on, the blue lines are the furthest it can go. "
    "Drag up/down to set Drive, double-click to reset it. Shift: fine.";

constexpr const char* kColorDisplay =
    "The colour EQ applied before the curve (it is undone after it). Drag the left handle up/down for Amt Lo; drag "
    "the right handle up/down for Amt Hi or sideways for Freq. Double-click a handle to reset it. Mouse wheel on the right handle (held, or with Shift): Width. Shift: fine.";

} // namespace smacheratr::help
