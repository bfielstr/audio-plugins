#pragma once

#include "../core/Params.h"

namespace smacheratr::help {

// Gentlr's band Slope (Smacheratr's, Gentlr's own, the end saturators' and the rack's)
inline constexpr const char* kGlue =
    "Glue: two of Gentlr's bands held at a shared border. Drag a band's edge onto its neighbour's in the display (it "
    "snaps within a few pixels) and the two glue: the border then moves as one (one band widens as the other narrows; "
    "for the Sub and High bands their Freq is the border) and a band moved drags its neighbour's edge along. The link "
    "icon on the border (lit cinnabar while glued) detaches them, or glues two bands that touch. Automation holds a "
    "glued border too. Off by default.";
inline constexpr const char* kSlope =
    "Gentlr's Slope: the shape of its two bands (not the Sub and High bands), below / above each band. 12 / 12: 12 "
    "dB/oct on both sides, the cut exactly the Range at the band's centre. Signature (the default): 24 dB/oct below and "
    "12 above, a steeper floor under the band. Classic: 12 below and 6 above, the shape before the Slope (older "
    "projects load with it, so they sound as they did). Alt Signature: 36 dB/oct below and 12 above, the steepest "
    "floor.";

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
                   "digital clip), so the output never exceeds the Output level, and nothing leaves above 0 dBFS. Useful with negative Color amounts.";
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
        case kOversampling:
            return "Oversampling: runs the curve at 2x or 4x the sample rate to reduce aliasing (4x: the least, a little more CPU and "
                   "latency). Off: no oversampling and no latency from it.";
        case kDcFilter: return "Removes DC offset from the input before the curve.";
        case kClarity:
            return "Gentlr: a compressor on a band (two if you like), so a hard-pushed drive does not go muddy or harsh: "
                   "when the band hits the curve hard it is turned down before it (up to Range, 8 dB by default, only "
                   "when pushed) and after it by half as much. Its Slope sets the band's shape (12 dB/oct on both sides by "
                   "default); set it with Freq and Width, or drag its handle in the frequency display (edges or Alt-drag: width; "
                   "wheel: width). A band works while its Range is above 0 dB. Advanced gives each band a Threshold "
                   "and can drive the region it cuts.";
        case kClarityFreq: return "Gentlr: the centre of the band it compresses (20 Hz to 20 kHz).";
        case kClarityWidth: return "Gentlr: the band's width in octaves, between its low and high edges (their slopes: Slope).";
        case kClaritySlope: return kSlope;
        case kClarity2Freq: return "Gentlr band 2: the centre of its band.";
        case kClarity2Width: return "Gentlr band 2: the band's width in octaves.";
        case kClarity2Range:
            return "Gentlr band 2 (marked 2 in the display): the most it turns its band down. At 0 dB (the default) the band "
                   "does nothing; give it a range to use it on a second muddy or harsh spot.";
        case kClarityRange:
            return "Gentlr: the most it turns its band down before the curve (after it, half as much). 8 dB by default, "
                   "0 to 24 dB.";
        case kClarityAdvanced:
            return "Gentlr's Advanced mode: each band gets a Threshold (the vertical sliders at the right of the frequency "
                   "display, with the band's level beside them) and the region it cuts can be driven (Drive). Off, Gentlr "
                   "works exactly as before: its bands start cutting at -18 dB.";
        case kClaritySub:
            return "Unused: Gentlr's Sub band works while its Range is above 0 dB (it had a button of its own before).";
        case kClaritySubFreq:
            return "Gentlr Sub: where the band starts to taper off, 20 to 100 Hz (40 Hz by default). Everything below it, "
                   "to the very bottom, is compressed.";
        case kClaritySubRange:
            return "Gentlr's Sub band (marked Sub in the display): compresses the sub region, as a shelf from the very bottom of the "
                   "spectrum up to where its cut starts to let go (Freq). Range is the most it turns that region down before "
                   "the curve (after it, half as much), 0 to 24 dB. At 0 dB (the default) the band does nothing; pull its "
                   "handle down in the display, or turn this up, and it starts cutting.";
        case kClarityHigh:
            return "Unused: Gentlr's High band works while its Range is above 0 dB (it had a button of its own before).";
        case kClarityHighFreq:
            return "Gentlr High: where the band starts to taper off going down, 2 to 16 kHz (7 kHz by default: the cut is half "
                   "as deep around half that, and nearly gone an octave below, so the presence region is left alone). "
                   "Everything above it, to the very top, is compressed.";
        case kClarityHighRange:
            return "Gentlr's High band (marked High in the display): compresses the top of the spectrum (harshness, fizz, sibilance), "
                   "as a shelf from where its cut starts to let go (Freq) up to the very top. Range is the most it turns that "
                   "down before the curve (after it, half as much), 0 to 24 dB. At 0 dB (the default) the band does nothing; "
                   "pull its handle down in the display, or turn this up, and it starts cutting.";
        case kClarityNoOverlap:
            return "Gentlr's bands never cover the same frequencies: dragging or widening a band in the display pushes its "
                   "neighbours' edges along (a neighbour narrows, then moves; the band stops where they cannot move further). "
                   "Switched on, bands that overlap are split at the middle of the overlap; automation that makes them "
                   "overlap is kept apart the same way.";
        case kClarityGlue12:
        case kClarityGlueSub1:
        case kClarityGlueSub2:
        case kClarityGlue1High:
        case kClarityGlue2High: return kGlue;
        case kClarityHighThreshold:
        case kClaritySubThreshold:
        case kClarityThreshold:
        case kClarity2Threshold:
            return "Gentlr (Advanced): the band's level where it starts cutting (-18 dB by default, where it starts without "
                   "Advanced). Over it, 3 dB of cut for every 5 dB, up to the band's Range. The bar beside it is the "
                   "band's level going into the curve, bright where it is over the threshold. Drag up/down, double-click "
                   "to reset. Grabbing it selects its band (lit, with its controls shown); the selected band's slider is lit.";
        case kClarityDrive:
            return "Gentlr (Advanced): drive the region Gentlr works on. Its bands are split out again, put through the "
                   "Analog curve on their own and put back, so the cut region gets density and harmonics while the rest "
                   "stays clean. Level-matched: quiet parts pass as they are.";
        case kClarityDriveAmount:
            return "Gentlr (Advanced): how hard the region Drive pushes the band region into the Analog curve (0 to 36 dB). "
                   "Level-matched: it gets denser, not louder.";
        case kMidSide:
            return "Saturate the mid and the side apart: the side is driven by its own, lower level, so a wide sound "
                   "stays wide when you push the drive (Menu).";
        default: return nullptr;
    }
}

constexpr const char* kShaperDisplay =
    "The Analog curve: input left to right, output bottom to top, with the clipping points at +-1. The bright part "
    "shows where the driven signal sits on the curve; with Pre-Limit on, the dashed lines are the furthest it can go. "
    "Drag up/down to set Drive, double-click to reset it. Shift: fine.";

constexpr const char* kColorDisplay =
    "Two layers, picked with the Color | Gentlr switch above it: the one in front has its handles to drag and its "
    "controls under the display, the other is drawn faint behind. "
    "Color: the colour EQ applied before the curve (it is undone after it). Drag the left handle up/down for Amt Lo; drag "
    "the right handle up/down for Amt Hi or sideways for Freq. Double-click a handle to reset it. Mouse wheel on the right handle (held, or with Shift): Width. "
    "Gentlr (once it is on): its bands; the band selected (its handle grabbed, its button, or its Threshold slider with Advanced) "
    "has its handle and its slider lit. Drag a handle sideways for the frequency and down for the Range, an edge for the width, "
    "or hold Alt (Option) and drag a band sideways for its width (right: wider). The Sub and High bands (named in their readouts) "
    "have no width; like band 2 they sit flat at 0 dB until you pull them down. With No Overlap on, a band pushes its neighbours along. "
    "Drag a band's edge onto its neighbour's (it snaps) and they glue: a link icon sits on the border near the bottom, lit "
    "while glued; click it to detach them, or to glue two bands that touch. Shift: fine.";

} // namespace smacheratr::help
