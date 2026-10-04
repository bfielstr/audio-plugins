#pragma once

#include "Params.h"

namespace locus::help {

inline const char* forParam (uint32_t id)
{
    switch (id)
    {
        case kContrast:
            return "Positive: quieter low-end events and the mud between harmonics fade back so the dominant ones "
                   "come into focus (clarity). Negative: everything moves closer in level (weight, density). The "
                   "level of the range is kept steady; use Gain to change it.";
        case kMode: return "Punchy: fast time constants that favour transients and impact. Smooth: slow ones that favour sustain and weight.";
        case kGain: return "Level of the focus range.";
        case kLowFreq: return "Bottom of the focus range.";
        case kHighFreq: return "Top of the focus range.";
        case kSolo: return "Hear only the focus range.";
        case kOutput: return "Overall output level.";
        default: return nullptr;
    }
}

constexpr const char* kSpectrum =
    "Faint copper: input spectrum. Bright line: output. Bottom strip: gain applied per band (down = cut, up = boost). Drag the "
    "range edges to set Low/High, drag inside the range sideways to move it or up/down to set Contrast, "
    "double-click inside to reset Contrast. Shift: fine.";

} // namespace locus::help
