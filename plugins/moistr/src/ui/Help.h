#pragma once

#include "Params.h"

namespace moistr::help {

inline const char* forParam (uint32_t id)
{
    switch (id)
    {
        case kDrive: return "Light saturation before the bands split. 0 leaves the input untouched.";
        case kLowFreq: return "Where the Low band's low-pass filter starts to cut (Hz).";
        case kMidFreq: return "The centre of the Mid band's band-pass filter, the low mids (Hz).";
        case kHighFreq: return "Where the High band's high-pass filter starts to let sound through (Hz).";
        case kLowRes:
        case kMidRes:
        case kHighRes: return "The band filter's resonance: higher gives a sharper peak at its frequency.";
        case kLowLevel:
        case kMidLevel:
        case kHighLevel: return "The band's level in the mix of the three (dB). All the way down switches the band off.";
        case kGap:
            return "Widens the hollow between Mid and High: to the right Mid moves down and High moves up (up to an "
                   "octave each), to the left they move towards each other.";
        case kSlope: return "How steeply the band filters cut: 12 or 24 dB per octave.";
        case kMovement:
            return "How far every band's frequency and level drift. 0 keeps the filters still; each band's Move sets its "
                   "share.";
        case kRate: return "How fast the bands drift (Hz). Each band moves at its own multiple of it.";
        case kSync: return "Takes the speed of the movement from the song tempo (Sync Rate) instead of Rate.";
        case kSyncRate: return "One cycle of the movement in bars or beats, while Sync is on.";
        case kLowMove: return "How much of the movement the Low band gets.";
        case kMidMove: return "How much of the movement the Mid band gets.";
        case kHighMove: return "How much of the movement the High band gets (the most by default).";
        case kLevelMove: return "How far a band's level moves up and down at full movement (dB).";
        case kSeed:
            return "Picks the pattern of the movement: each band's rates, phases and shapes. The same number always moves "
                   "the same way.";
        case kGlue: return "Compresses the three bands back together: more Glue lowers the threshold and raises the ratio.";
        case kGrit: return "Soft clipping after the compressor, for a little dirt.";
        case kPasses:
            return "2 runs the result through the bands, Glue and Grit a second time, with a movement of its own, like "
                   "bouncing it and filtering it again.";
        case kMix: return "Blend of the effect and the untouched signal.";
        case kOutput: return "Overall output level.";
        default: return nullptr;
    }
}

constexpr const char* kBandView =
    "The three bands' filters against frequency: their set places in dim copper (the hollow between Mid and High "
    "shaded), and while sound plays, where the movement has them now. With 2 Passes the second pass is dashed.";

} // namespace moistr::help
