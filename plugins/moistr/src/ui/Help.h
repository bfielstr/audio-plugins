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
            return "The Low band's level (dB). Seed picks where the Low band ends (100 to 500 Hz) and that never moves; "
                   "with Push and Dip at 0 its level never moves either. All the way down switches it off.";
        case kMidLevel:
        case kHighLevel:
            return "The band's level when it has risen all the way (dB). All the way down switches the band off.";
        case kAirLevel:
            return "The Air band's level when it has risen all the way (dB), with 4 Bands. All the way down switches it "
                   "off.";
        case kRise: return "How quickly the moving bands rise: below x1 faster than Seed picked, above x1 slower.";
        case kFall: return "How quickly the moving bands fall: below x1 faster than Seed picked, above x1 slower.";
        case kDepth:
            return "How far a moving band falls below its Level (dB) with Movement and its Move all the way up. With Drop "
                   "Out on, the deepest falls go to silence.";
        case kMovement: return "How much the bands rise and fall overall. 0 holds every band still at its Level.";
        case kRate: return "How often the bands rise and fall (Hz), while Sync is off.";
        case kSync: return "Takes the speed of the movement from the song tempo (Sync Rate) instead of Rate.";
        case kSyncRate: return "One cycle of the movement in bars or beats, while Sync is on.";
        case kMidMove: return "How much the Mid band rises and falls.";
        case kHighMove: return "How much the High band rises and falls.";
        case kAirMove: return "How much the Air band rises and falls (with 4 Bands).";
        case kSeed:
            return "Picks the pattern: when each band rises and falls, how quickly, and where the Low band ends (100 to "
                   "500 Hz). The same number always moves the same way. Changing it while playing crossfades.";
        case kSeedB:
            return "A second pattern to blend with Seed's (Blend). The Low band still ends where Seed puts it.";
        case kSeedBlend:
            return "Crossfades the two patterns: 0 is Seed alone, 100 % Seed B alone. In between both play and overlap, "
                   "so the bands are up more of the time: fuller and louder (at 50 % a band is up whenever either pattern has "
                   "it up).";
        case kDensity:
            return "How many rises and falls: x1 is Seed's pattern, x8 eight times as many in the same time, x0.25 a quarter "
                   "as many.";
        case kSpeed:
            return "Divides every rise and fall time (after Rise and Fall), down to 1 ms: x16 turns slow swells into fast "
                   "chops. The ramps stay smooth.";
        case kDropOut:
            return "Lets the deepest falls go all the way to silence: as a band's fall (Depth x Movement x its Move) goes "
                   "past 30 dB its floor curves down, to nothing at 48 dB. Shallower falls are unchanged.";
        case kLowPush:
            return "Lets the Low band come forward on its own seeded moments: up to this many dB above its Level (x "
                   "Movement). With Push and Dip at 0 the Low band is locked. Its crossover never moves.";
        case kLowDip:
            return "Lets the Low band dip back a little on its own seeded moments: at most this many dB under its Level "
                   "(6 dB at most, x Movement).";
        case kLink:
            return "How much the moving bands (Mid, High and Air) rise and fall together. 0: each on its own events. 100 %: "
                   "all of them open and close at once, following the Mid band, each still at its own Level and Move. "
                   "With Liquid on, Link also lifts the resonance as the bands open. The Low band is never linked.";
        case kLiquid:
            return "A moving resonance on the bands above Low: two peaks, like a vowel, that glide between Liquid's Low and "
                   "High on the movement's clock (Rate or Sync, x Density), now and then jumping quickly. 0 switches it off. "
                   "The Low band (the sub) never goes through it.";
        case kLiquidRes: return "How sharp Liquid's peaks are: low is a broad wash, high a narrow, whistling vowel.";
        case kLiquidLow: return "The lowest Liquid's first peak goes (Hz).";
        case kLiquidHigh: return "The highest Liquid's first peak goes (Hz). The second peak sits above it, as in a vowel.";
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
    "The bands against frequency. The Low band is locked (solid, with a lock and where it ends in Hz) unless Push or Dip "
    "lets it move; the other bands fill up to their level now and rise and fall while sound plays. The lines show each "
    "band's Level and how far it can move (dashed). With the shifter on, each upper band shows how far it is shifted. "
    "With Liquid on, the bar at the top shows where its resonance may go and the markers where its two peaks are now.";

} // namespace moistr::help
