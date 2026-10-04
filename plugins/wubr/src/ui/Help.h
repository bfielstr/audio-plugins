#pragma once

#include "../core/Params.h"

namespace wubr::help {

inline const char* forField (uint32_t field)
{
    switch (field)
    {
        case kBandOn: return "Turns this band on or off.";
        case kTarget:
            return "What the drawn shape moves. Gain: the band's level (Gain + Depth x the shape). Frequency: the band's "
                   "centre, over Sweep octaves. Both: both at once.";
        case kFreq: return "The band's centre (drag its handle in the band display sideways).";
        case kWidth: return "The band's width in octaves (the wheel on its handle).";
        case kGain: return "The band's level where the shape is at its middle line (drag its handle up or down).";
        case kDepth: return "How far the shape raises the band at its top and lowers it at its bottom, in dB (negative turns it over).";
        case kSweep: return "Frequency and Both: how many octaves the shape moves the centre over (half up at the top, half down at the bottom).";
        case kRateMode: return "Sync: the shape runs in note lengths of the host's tempo, locked to the song while it plays. Free: in Hz.";
        case kSync: return "The length of one cycle of the shape, in notes of the host's tempo.";
        case kRateHz: return "Cycles of the shape a second (Free).";
        case kPhase: return "Where in the shape the cycle starts (LFO).";
        case kPointCount: return "How many points the shape has (double-click in the shape to add or remove one).";
        case kHold:
            return "Envelope: the point the shape stops at. A MIDI note holds it there until you let go (then it runs to "
                   "the end); a transient holds it until the next one. The last point: the shape runs through once. "
                   "Alt-click a point to pick it.";
        default: return nullptr;
    }
}

inline const char* forParam (uint32_t id)
{
    switch (id)
    {
        case kMode:
            return "LFO: each band's shape runs over and over. Envelope: a MIDI note or a transient starts the shapes from "
                   "the beginning; they run to their hold points.";
        case kTrigger: return "Envelope: what starts the shapes: MIDI notes, or transients (hits) in the audio.";
        case kSensitivity: return "Transient: how far a hit must jump over the recent level to start the shapes (lower: more hits count).";
        case kLinkRate:
            return "Both bands run at band 1's rate (Sync or Free, and its note length or Hz); each keeps its own phase. "
                   "Off: each band has its own rate.";
        case kDryWet: return "Balance between the input and the banded signal.";
        case kOutput: return "Output level (before the Smacheratr at the end).";
        default: break;
    }
    if (id >= kBandBase && id < kTailExtBase)
    {
        const uint32_t field = (id - kBandBase) % kBandBlock;
        if (field < kPoints)
            return forField (field);
        return "A point of the drawn shape (drag it in the shape display).";
    }
    return nullptr;
}

constexpr const char* kBandDisplay =
    "The two bands (numbered at their handles; the selected one brighter) as they are now, moving with their shapes; dashed: the range the shape covers. "
    "Drag a band's handle sideways for its frequency, up or down for its gain; the wheel, its edges, or Alt + dragging the band sideways set its width. Click a band "
    "to show its controls; double-click or right-click resets it.";
constexpr const char* kShapeDisplay =
    "The band's shape, one cycle from left to right. Drag a point to move it; drag a line up or down to bend it; "
    "double-click to add a point or remove one; Alt-click a point to make it the hold point (Envelope); right-click a "
    "line to straighten it. The dot shows where the band is now.";

} // namespace wubr::help
