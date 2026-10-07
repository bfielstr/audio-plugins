#pragma once

#include "Params.h"

namespace moistr::help {

inline const char* forParam (uint32_t id)
{
    switch (id)
    {
        case kDrive: return "Light saturation before the sound is split into bands. 0 leaves the input untouched.";
        case kBandCount:
            return "Splits the sound into 3 bands (Low, Mid, High) or 4 (Low, Mid, High, Air). The Low band stays put; "
                   "the others rise and fall.";
        case kXoverMid: return "Where the Mid band ends and the High band starts (Hz).";
        case kXoverHigh: return "Where the High band ends and the Air band starts (Hz). Only used with 4 Bands.";
        case kLowLevel:
            return "The Low band's level (dB). The Low band is locked: it never moves, and Seed picks where it ends "
                   "(100 to 500 Hz). All the way down switches it off.";
        case kMidLevel:
        case kHighLevel:
            return "The band's level when it has risen all the way (dB). All the way down switches the band off.";
        case kAirLevel:
            return "The Air band's level when it has risen all the way (dB), with 4 Bands. All the way down switches it "
                   "off.";
        case kRise: return "How quickly the moving bands rise: below x1 faster than Seed picked, above x1 slower.";
        case kFall: return "How quickly the moving bands fall: below x1 faster than Seed picked, above x1 slower.";
        case kDepth: return "How far a moving band falls below its Level (dB) with Movement and its Move all the way up.";
        case kMovement: return "How much the bands rise and fall overall. 0 holds every band still at its Level.";
        case kRate: return "How often the bands rise and fall (Hz), while Sync is off.";
        case kSync: return "Takes the speed of the movement from the song tempo (Sync Rate) instead of Rate.";
        case kSyncRate: return "One cycle of the movement in bars or beats, while Sync is on.";
        case kMidMove: return "How much the Mid band rises and falls.";
        case kHighMove: return "How much the High band rises and falls.";
        case kAirMove: return "How much the Air band rises and falls (with 4 Bands).";
        case kSeed:
            return "Picks the pattern: when each band rises and falls, how quickly, and where the Low band ends (100 to "
                   "500 Hz). The same number always moves the same way.";
        case kGlue: return "Compresses the bands back together: more Glue lowers the threshold and raises the ratio.";
        case kGrit: return "Soft clipping after the compressor, for a little dirt.";
        case kPasses:
            return "2 runs the result through the bands, Glue and Grit a second time, with a movement of its own, like "
                   "bouncing it and splitting it again.";
        case kShiftOn:
            return "Switches on the frequency shifter on the bands above Low. The Low band (the sub) is never shifted.";
        case kShift:
            return "How far the shifter moves every frequency of the bands above Low, up or down (Hz). Shifting by Hz, not "
                   "by a ratio, makes the harmonics clash for a metallic sound. The Low band is never shifted.";
        case kShiftMix:
            return "The shifted bands against the unshifted ones. Below 100 % both play, which beats and swirls. The Low "
                   "band is never shifted.";
        case kMix: return "Blend of the effect and the untouched signal.";
        case kOutput: return "Overall output level.";
        default: return nullptr;
    }
}

constexpr const char* kBandView =
    "The bands against frequency. The Low band is locked (solid, with a lock and where it ends in Hz); the other bands "
    "fill up to their level now and rise and fall while sound plays. The lines show each band's Level and how far it "
    "can fall (dashed). With the shifter on, each upper band shows how far it is shifted.";

} // namespace moistr::help
