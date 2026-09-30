#pragma once

#include "../core/Params.h"

namespace levlr::help {

inline const char* forParam (uint32_t id)
{
    switch (id)
    {
        case kSlope:
            return "How steeply the bands part at each crossover (Linkwitz-Riley). Each slope turns the phase its own way "
                   "around the crossovers: 12 gently, 96 the most. At any slope, bands at 0 dB add back to a flat level. "
                   "The mouse wheel on a crossover in the display steps it too.";
        case kOutput: return "Output level, before the Smacheratr at the end (so it also sets how hard the bands drive it).";
        case kXover:
        case kXover + 1:
        case kXover + 2:
            return "Where two bands meet (drag the line in the display sideways). A crossover stays at least 1/6 octave "
                   "from its neighbours; one set past them pushes the ones above it up. With fewer bands, the crossovers "
                   "past the last band aren't used.";
        case kBandCount:
            return "How many bands are in use (1 to 4): N bands use the first N-1 crossovers, the last band reaches to the "
                   "top. 1: no split at all. The bands past it (their level, mute, solo and drive) are left out, and a "
                   "change crossfades over 20 ms.";
        default: break;
    }
    if (id >= kDriveBase && id < kNumParams)
        switch ((id - kDriveBase) % kDriveBlock)
        {
            case kDriveDb:
                return "Saturates the band (after its level): how hard it drives the curve, 0 to 36 dB. 0 dB is off: the "
                       "band stays clean. The level is kept about the same (auto gain), so more Drive adds colour, not "
                       "volume; a louder band (its level up) is pushed harder. 4x oversampled; the latency is the same "
                       "whether a drive is on or off.";
            case kDriveType:
                return "The band's drive curve. Analog: Smacheratr's (clean up to a soft knee). Tape: soft all the way "
                       "(tanh). Tube: leans to one side, adds even harmonics. Hard Clip: a flat top, the most edge. "
                       "Fold: folds back over past the top, hollow and metallic as it is pushed. A change crossfades.";
            default: break;
        }
    if (id >= kBandBase && id < kTailExtBase)
        switch ((id - kBandBase) % kBandBlock)
        {
            case kGain: return "The band's level (drag the band up or down in the display; Shift: fine).";
            case kMute: return "Silences the band (M in the display).";
            case kSolo: return "Hear only the soloed bands (S in the display). A soloed band is heard even when muted.";
            default: break;
        }
    return nullptr;
}

constexpr const char* kDisplay =
    "The bands in use side by side, each at its level (its drive, when on, tagged at the foot), with the whole "
    "response (white) and the output's spectrum behind (tilted 4.5 dB/oct, so a mix looks level). Drag a band up or down for its level (Shift: fine); drag a "
    "line between two bands sideways to move that crossover. Double-click or right-click resets a band's level or a "
    "crossover's frequency. M and S at the top of a band mute and solo it. The mouse wheel: a band's level, or the "
    "slope on a crossover.";

} // namespace levlr::help
