#pragma once

#include "Params.h"

namespace moistr::help {

// a bell's controls (the same for A .. H)
inline const char* forBell (uint32_t id)
{
    for (int b = 0; b < kNumBells; ++b)
    {
        if (id == bellOnId (b))
            return "Switches the bell on or off. Off, it fades out and is not run. The letters pick which bell's controls "
                   "BELLS shows; this row switches each one.";
        if (id == bellId (b, kBellRate))
            return "How fast the bell sweeps from Low to High and back (Hz), while its Sync is off.";
        if (id == bellId (b, kBellSync))
            return "Takes the bell's sweep speed from the song tempo (the choice beside it) instead of Rate.";
        if (id == bellId (b, kBellSyncRate))
            return "One sweep of the bell, Low to High and back, in bars or beats, while its Sync is on.";
        if (id == bellId (b, kBellLow))
            return "The lowest the bell's centre goes (Hz). With Phase at 0 the sweep starts here.";
        if (id == bellId (b, kBellHigh))
            return "The highest the bell's centre goes (Hz). The sweep moves on a log scale, so it spends as long per octave.";
        if (id == bellId (b, kBellGain))
            return "How far the bell boosts (above 0) or cuts (below 0) around its centre (dB).";
        if (id == bellId (b, kBellWidth))
            return "The bell's Q: low is a broad bump (0.5 is broad and smooth), high a narrow, ringing peak.";
        if (id == bellId (b, kBellPhase))
            return "Where in its sweep the bell starts (degrees): 0 at Low, 180 at High. Sets the bells against each other.";
    }
    return nullptr;
}

// a gesture slot's controls (the same for slots 1 .. 4)
inline const char* forGesture (uint32_t id)
{
    if (id < kG1Gesture || id >= kIntensity)
        return nullptr;
    switch ((GestureField)((id - kG1Gesture) % kGestureFields))
    {
        case kGestureChoice:
            return "The slot's gesture: a curve over a few beats (gates and stutters on straight and triplet grids, fades, "
                   "swells, plucks, ramps). User plays the file picked with File (from the Gestures folder beside your "
                   "presets), saved with the project.";
        case kGestureTarget:
            return "What the slot moves. Mid, High and Air Level fade the band (down to silence); Wobble Rate and Amount "
                   "drive Wobble; Close sweeps a resonant low-pass down from Tone's corner; Liquid Pos moves Liquid's "
                   "resonance; Dirt goes from the saturated sound to the clean one, Bells from the bells to none (both "
                   "level matched); Mid X, High X, Seed Blend and Shift move those controls. Off: the slot does nothing. "
                   "The Low band is never moved.";
        case kGestureMode:
            return "Loop plays the gesture over and over in time with the song. Walk goes back and forth through it (forwards, "
                   "then backwards), at Speed.";
        case kGestureLength:
            return "How long the gesture lasts, in beats: Own is the length it was made with; the others stretch or squeeze "
                   "it to 1/2 .. 32 beats.";
        case kGestureSpeed:
            return "How fast Walk goes through the gesture: x1 takes Length per pass, x2 half that. Hold stops it at "
                   "Position (move Position by hand or with automation to scrub). Loop does not use it.";
        case kGesturePosition:
            return "Loop: where the loop starts, as a share of Length (25 % starts it a quarter later). Walk: where it starts, "
                   "and where it stays at Hold.";
        case kGestureSmooth:
            return "Glides the gesture: 0 keeps its steps sharp (they still take 2 ms, so they never click), 100 % turns them "
                   "into glides of a 16th of a beat.";
        case kGestureDepth:
            return "How far the slot pulls its target towards the gesture: 100 % all the way, 50 % half way. Below 0 the "
                   "gesture is turned upside down (a fade out becomes a fade in).";
        default: return nullptr;
    }
}

inline const char* forParam (uint32_t id)
{
    if (const char* t = forBell (id))
        return t;
    if (const char* t = forGesture (id))
        return t;
    switch (id)
    {
        case kDrive: return "Light saturation before the sound is split into bands (after SWEEP). 0 leaves the input untouched.";
        case kSweep:
            return "Switches the SWEEP stage on: eight sweeping bell EQs, a High Shelf going round in a slow orbit, then a "
                   "saturator and Tone, all before the bands. This is moistr's main sound. Off, the sound goes straight to the "
                   "bands.";
        case kSweepDrive:
            return "How hard the SWEEP stage's saturator is driven after the bells and the shelf (dB). Its level is matched "
                   "to the input's automatically, so more Drive is more grit, not more volume. 12 to 24 dB is the sweet spot.";
        case kSweepCurve:
            return "The saturator's curve: Hard bends sooner and crunches more; Soft is a gentler knee (the same Drive about "
                   "3 dB softer).";
        case kToneOn: return "Switches Tone on: a gentle low-pass after the saturator that rounds off its brightest fizz.";
        case kTone: return "Where Tone's low-pass sits (Hz): lower is darker and smoother. 7 kHz takes the edge off the crunch.";
        case kCleanSub:
            return "Splits off the lows before the saturator and sends them around it, so the sub stays clean while the rest "
                   "crunches. Split, Level and Drive set where, how loud and how much crunch the lows get after all.";
        case kSplitFreq: return "Where Clean Sub splits (Hz): everything below goes around the saturator.";
        case kSplitLevel:
            return "The clean lows against the saturated rest (dB). 0 dB is the level they would have through the saturator "
                   "if it did not compress; the saturated rest is quieter than that, so the lows usually sit below 0.";
        case kSplitDrive: return "Lets the split-off lows crunch too, with a saturator of their own (dB). 0 keeps them clean.";
        case kSubBoost:
            return "Adds the lows back after the saturator: the whole sound still crunches, with a clean sub under it for "
                   "weight. Freq and Level set how low and how much.";
        case kSubFreq: return "How high Sub Boost's lows reach (Hz): a steep low-pass on the saturator's input.";
        case kSubLevel:
            return "How much of the lows Sub Boost adds: 100 % as loud as the saturated sound (matched slowly, on the music's "
                   "level), 50 % half that.";
        case kShelf:
            return "Switches the High Shelf on: it lifts or cuts everything above its corner, and the corner and the gain move "
                   "together in a slow orbit. Off in a new instance.";
        case kShelfRate: return "How fast the High Shelf goes round its orbit (Hz).";
        case kShelfLow: return "The lowest the High Shelf's corner goes (Hz).";
        case kShelfHigh:
            return "The highest the High Shelf's corner goes (Hz), up to 5 kHz. With Tilt, the higher the corner, the less "
                   "it may boost.";
        case kShelfMin: return "The High Shelf's lowest gain (dB): how far it cuts the highs at the bottom of its orbit.";
        case kShelfMax: return "The High Shelf's highest gain (dB) at the top of its orbit (lowered at high corners by Tilt).";
        case kShelfQ:
            return "The High Shelf's resonance: low is a gentle slope, high (18 and up) adds a bump just above the corner and "
                   "a dip just under it, which is what makes it talk.";
        case kShelfWander:
            return "The shape of the High Shelf's orbit: 0 is a perfect circle (corner and gain round and round); higher "
                   "drifts it smoothly and randomly (Seed picks how), so it never quite repeats. It never jumps.";
        case kShelfTilt:
            return "Lowers how far the High Shelf may boost as its corner rises above 1 kHz, so high corners never get nasal. "
                   "0 gives the same gain range at every corner.";
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
        case kIntensity:
            return "Scales every gesture slot's Depth at once: 0 switches all the gestures off, 100 % plays them as set.";
        case kWobbleRate:
            return "Wobble's speed in cycles per beat (in time with the song): 2 is 8th notes, 4 16ths, 3 8th-note "
                   "triplets; the top end buzzes. A gesture on Wobble Rate sweeps it smoothly, without jumps.";
        case kWobbleAmount:
            return "How deep Wobble, the tremolo on the bands above Low, goes: 100 % to silence on every cycle. 0 switches "
                   "it off unless a gesture drives Wobble Amount. The Low band never wobbles.";
        case kMix: return "Blend of the effect and the untouched signal.";
        case kOutput: return "Overall output level.";
        default: return nullptr;
    }
}

constexpr const char* kSweepView =
    "The SWEEP stage now: each bell that is on (A, C, E, G solid; B, D, F, H dashed) and the High Shelf, each thin, and "
    "the whole stage bold (with Tone), from 20 Hz to 5 kHz. The bars show where each bell sweeps (bottom, A lowest) and "
    "where the shelf's corner goes (top). The box at the right is the shelf's orbit: its corner across (Low to High), its "
    "gain up (Min to Max), the dashed line the most it may boost at each corner (Tilt), the dot where it is now.";

constexpr const char* kGestureView =
    "The picked gesture slot: its curve across the gesture (up is where it pulls its target to), the beats it plays "
    "over, and while it runs the playhead and the value now. Upside down when Depth is below 0.";

constexpr const char* kGestureSlots =
    "Shows that gesture slot's controls here (four slots, each moving one target; the display shows the picked one).";

constexpr const char* kGestureFile =
    "Picks a gesture file (JSON) from the Gestures folder beside your moistr presets for this slot and sets its Gesture "
    "to User. The curve is saved with the project, so the file is not needed again.";

constexpr const char* kBandView =
    "The bands against frequency. The Low band is locked (solid, with a lock and where it ends in Hz) unless Push or Dip "
    "lets it move; the other bands fill up to their level now and rise and fall while sound plays. The lines show each "
    "band's Level and how far it can move (dashed). With the shifter on, each upper band shows how far it is shifted. "
    "With Liquid on, the bar at the top shows where its resonance may go and the markers where its two peaks are now.";

} // namespace moistr::help
