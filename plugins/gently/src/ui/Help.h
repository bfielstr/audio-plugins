#pragma once

#include "../core/Params.h"

namespace gently::help {

inline const char* forParam (uint32_t id)
{
    switch (id)
    {
        case kAdvanced:
            return "Advanced: each band gets its own Threshold (the sliders at the right of the display) and the region "
                   "Drive works. Off: every band starts cutting at -18 dB (its peak level), as Smacheratr's Gently does.";
        case kDrive:
            return "Advanced: saturate the band region Gently is cutting, through Smacheratr's Analog curve on its own, "
                   "level matched: the region gets denser, not louder (oversampled 4x).";
        case kDriveAmount: return "How hard the region Drive pushes the cut bands into the curve.";
        case kAttack:
            return "How fast Gently follows a band getting louder (the level it measures: the band's peak). 15 ms, as "
                   "in Smacheratr; shorter catches the front of a note too.";
        case kRelease: return "How fast a band's cut lets go once the band is quieter again (150 ms, as in Smacheratr).";
        case kStereo:
            return "Stereo: left and right share one detector (the image stays put). Mid/Side: the mid and the side are "
                   "worked on apart, each with its own detector. Mid or Side: only that one (the other passes).";
        case kMix: return "Dry / wet: the input, delayed to line up, against Gently's output.";
        case kOutput: return "Output level, before the Smacheratr at the end.";
        case kSubOn: return "Unused: the Sub band works while its Range is above 0 dB (it had a button of its own before).";
        case kSubFreq:
            return "The Sub band: where it starts to taper off, 20 to 100 Hz (40 Hz by default; drag its handle "
                   "sideways). Everything below it, to the very bottom, is compressed.";
        case kSubRange:
            return "The Sub band compresses the sub region, from the bottom of the spectrum, a shelf, up to where its cut "
                   "starts to let go (Freq). Range: the most it turns that region down (drag its handle down). 3 dB for "
                   "every 5 the band is over its threshold, up to this. At 0 dB (the default) the band does nothing.";
        case kHighOn: return "Unused: the High band works while its Range is above 0 dB (it had a button of its own before).";
        case kHighFreq:
            return "The High band: where it starts to taper off going down, 2 to 16 kHz (7 kHz by default; drag its "
                   "handle sideways). Everything above it, to the very top, is compressed; half as much around half the "
                   "frequency, nearly nothing an octave below that.";
        case kHighRange:
            return "The High band compresses the top of the spectrum (harshness, fizz, sibilance), a shelf from where its cut "
                   "starts to let go (Freq) up to the very top. Range: the most it turns that down (drag its handle "
                   "down). 3 dB for every 5 the band is over its threshold, up to this. At 0 dB (the default) the band does "
                   "nothing.";
        case kHighThreshold:
            return "Advanced: where the High band starts cutting (its peak level, dB). The slider shows the band's level: "
                   "brighter above the threshold, where it is being cut.";
        case kNoOverlap:
            return "No Overlap: the bands never cover the same frequencies. Dragging or widening a band in the display "
                   "pushes its neighbours' edges along (a neighbour narrows, then moves; the band stops where they cannot "
                   "move further). Switched on, bands that overlap are split at the middle of the overlap; automation that "
                   "makes them overlap is kept apart the same way.";
        case kSubThreshold:
            return "Advanced: where the Sub band starts cutting (its peak level, dB). The slider shows the band's level: "
                   "brighter above the threshold, where it is being cut.";
        default: break;
    }
    if (isBandParam (id) && !isSubParam (id) && !isHighParam (id))
        switch ((id - kBandBase) % kBandBlock)
        {
            case kOn: return "Switches the band on or off (or click its name at the top of the display).";
            case kFreq: return "The band's centre (drag its handle in the display sideways).";
            case kWidth:
                return "How wide the band is, in octaves between its edges (drag an edge in the display, or Alt-drag the "
                       "band). 12 dB/oct below it, 6 dB/oct above.";
            case kRange:
                return "The most the band is turned down (drag its handle down). It cuts 3 dB for every 5 the band is over "
                       "its threshold, up to this. 0 dB: the band does nothing.";
            case kThreshold:
                return "Advanced: where the band starts cutting (its peak level, dB). The slider shows the band's level: "
                       "brighter above the threshold, where it is being cut.";
            default: break;
        }
    return nullptr;
}

constexpr const char* kDisplay =
    "Gently's bands (each named in its readout at the top): each band's region shaded, the most it can cut dashed, the cut it is "
    "making now lit (cinnabar), the whole response as the bright line. Behind: the output's spectrum filled, the input's dotted "
    "(tilted 4.5 dB/oct). Drag a handle sideways for the band's frequency, down for its Range; drag an edge, or "
    "Alt-drag the band, for its width (the Sub and High bands have none); the wheel on a handle (Shift) too. Double-click "
    "or right-click a handle resets the band. Click band 1's or band 2's readout at the top to switch it on or off. The Sub "
    "and High bands have no switch: they sit flat at 0 dB until you pull their handle down. With No Overlap on, a band "
    "pushes its neighbours along.";

constexpr const char* kSubThresholdSlider =
    "Advanced: the Sub band's Threshold (drag; Shift: fine). The band's level rises beside it, bright where it is over "
    "the threshold and being cut. Double-click or right-click: -18 dB.";

constexpr const char* kHighThresholdSlider =
    "Advanced: the High band's Threshold (drag; Shift: fine). The band's level rises beside it, bright where it is over "
    "the threshold and being cut. Double-click or right-click: -18 dB.";

constexpr const char* kThresholdSlider =
    "Advanced: the band's Threshold (drag; Shift: fine). The band's level rises beside it, bright where it is over the "
    "threshold and being cut. Double-click or right-click: -18 dB.";

} // namespace gently::help
