#pragma once

#include "Params.h"

namespace smatcheratr::help {

inline const char* forParam (uint32_t id)
{
    switch (id)
    {
        case kDrive: return "Gain into the shaper. The display shows how far the driven signal reaches into the curve.";
        case kCurve:
            return "Analog Clip and Digital Clip stay linear below the clipping point (smooth / immediate clipping "
                   "above it). Soft Sine, Medium Curve and Hard Curve saturate progressively. Bass Shaper is an Analog "
                   "Clip with an adjustable threshold and a smoother spectrum on low-end material. Sinoid Fold folds "
                   "the signal back over itself. Waveshaper is shaped by its own controls on the right.";
        case kBassThreshold:
            return "Bass Shaper only: the curve is linear below this level and clips smoothly above it. Low values "
                   "give soft clipping, 0 dB is a hard clip.";
        case kPostClip:
            return "No Clip, or clips the output at 0 dB after the shaper (Soft: the Analog Clip curve, Hard: a digital clip), so "
                   "the output never exceeds the Output level. Useful with negative Color amounts, which can add level.";
        case kColorOn:
            return "Enables the colour filters: an EQ applied before the shaper and undone after it, so it changes "
                   "how much of each frequency range is saturated, not the balance of the output.";
        case kColorLo:
            return "Saturation applied to the low end (a shelf below 100 Hz, +-24 dB at +-100 %): negative values keep "
                   "the bass clean and let its energy through, positive values saturate it more.";
        case kColorHi: return "Saturation applied around Freq: positive values add more, negative values less.";
        case kColorFreq: return "Centre frequency of the second colour filter.";
        case kColorWidth: return "Width of the second colour filter (larger = wider).";
        case kOutput: return "Final output attenuation.";
        case kDryWet: return "Balance between the dry input and the saturated signal. Use 100 % on a return track.";
        case kWsDrive:
            return "How much the Waveshaper controls shape the curve: at 0 % the curve is a plain clip, at 100 % they "
                   "shape it fully.";
        case kWsCurve: return "Adds mostly third-order harmonics.";
        case kWsDepth: return "Amplitude of a sine wave superimposed on the curve.";
        case kWsLinear: return "Size of the linear region of the curve (works with Curve and Depth).";
        case kWsDamp: return "Flattens the curve around zero, like an ultra-fast noise gate.";
        case kWsPeriod: return "Density of the ripples of the superimposed sine wave (with Depth).";
        case kHiQuality: return "Runs the shaper 4x oversampled to reduce aliasing (a little more CPU).";
        case kDcFilter: return "Removes DC offset from the input before the shaper.";
        default: return nullptr;
    }
}

constexpr const char* kShaperDisplay =
    "The shaping curve: input left to right, output bottom to top, with the clipping points at +-1. The bright "
    "part shows where the driven signal currently sits on the curve. Drag up/down to set Drive, double-click to "
    "reset it. Shift: fine.";

constexpr const char* kColorDisplay =
    "The colour EQ applied before the shaper (it is undone after it). Drag the left handle up/down for Amt Lo; drag "
    "the right handle up/down for Amt Hi or sideways for Freq. Double-click a handle to reset it. Shift: fine.";

} // namespace smatcheratr::help
