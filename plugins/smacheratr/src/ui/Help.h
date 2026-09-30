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
            return "Gently: a compressor on a band (two if you like), so a hard-pushed drive does not go muddy or harsh: "
                   "when the band hits the curve hard it is turned down before it (up to Range, 8 dB by default, only "
                   "when pushed) and after it by half as much. The band slopes 12 dB/oct below and 6 dB/oct above; set "
                   "it with Freq and Width, or drag its handle in the frequency display (edges or Alt-drag: width; "
                   "wheel: width). A band works while its Range is above 0 dB. Advanced gives each band a Threshold "
                   "and can drive the region it cuts.";
        case kClarityFreq: return "Gently: the centre of the band it compresses (20 Hz to 20 kHz).";
        case kClarityWidth: return "Gently: the band's width in octaves, between its 12 dB/oct low edge and 6 dB/oct high edge.";
        case kClarity2Freq: return "Gently band 2: the centre of its band.";
        case kClarity2Width: return "Gently band 2: the band's width in octaves.";
        case kClarity2Range:
            return "Gently band 2 (blue in the display): the most it turns its band down. At 0 dB (the default) the band "
                   "does nothing; give it a range to use it on a second muddy or harsh spot.";
        case kClarityRange:
            return "Gently: the most it turns its band down before the curve (after it, half as much). 8 dB by default, "
                   "0 to 24 dB.";
        case kClarityAdvanced:
            return "Gently's Advanced mode: each band gets a Threshold (the vertical sliders at the right of the frequency "
                   "display, with the band's level beside them) and the region it cuts can be driven (Drive). Off, Gently "
                   "works exactly as before: its bands start cutting at -18 dB.";
        case kClarityThreshold:
        case kClarity2Threshold:
            return "Gently (Advanced): the band's level where it starts cutting (-18 dB by default, where it starts without "
                   "Advanced). Over it, 3 dB of cut for every 5 dB, up to the band's Range. The bar beside it is the "
                   "band's level going into the curve, bright where it is over the threshold. Drag up/down, double-click "
                   "to reset.";
        case kClarityDrive:
            return "Gently (Advanced): drive the region Gently works on. Its bands are split out again, put through the "
                   "Analog curve on their own and put back, so the cut region gets density and harmonics while the rest "
                   "stays clean. Level-matched: quiet parts pass as they are.";
        case kClarityDriveAmount:
            return "Gently (Advanced): how hard the region Drive pushes the band region into the Analog curve (0 to 36 dB). "
                   "Level-matched: it gets denser, not louder.";
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
    "the right handle up/down for Amt Hi or sideways for Freq. Double-click a handle to reset it. Mouse wheel on the right handle (held, or with Shift): Width. "
    "With Gently on, its bands: drag a handle sideways for the frequency and down for the Range, an edge for the width, "
    "or hold Alt (Option) and drag a band sideways for its width (right: wider). Shift: fine.";

} // namespace smacheratr::help
