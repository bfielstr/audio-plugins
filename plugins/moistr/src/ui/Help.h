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

// a 0.27 gesture slot's controls (the same for slots 1 .. 4; no longer in the editor, still played in a project that
// uses them, and shown to the host)
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

// the LAB (0.30)
inline const char* forLab (uint32_t id)
{
    if (!isChainParam (id))
        return nullptr;
    switch ((id - kChainBase) % kChainFields)
    {
        case kChainLevel: return "The chain's level after its effects (dB): its band in the mix, however hard it hits them. At the bottom it is off.";
        case kChainMute: return "Silences the chain: it fades out and stops running (its band is gone from the mix until it is on again).";
        case kChainSolo:
            return "Hears only the soloed chains (the Low band too is silent while any chain is soloed), to set one band's "
                   "dirt on its own.";
        case kChainMono: return "Puts the chain's output in the middle: its band mono, whatever its effects do to the width.";
        default: return nullptr;
    }
}

inline const char* forParam (uint32_t id)
{
    if (const char* t = forBell (id))
        return t;
    if (const char* t = forGesture (id))
        return t;
    if (const char* t = forLab (id))
        return t;
    switch (id)
    {
        case kInput:
            return "The level going in (dB), at the very start: turn it down when the SWEEP stage's saturator crunches too hard. "
                   "Its make-up follows the level, so the output stays about as loud while the crunch eases.";
        case kLoopLock:
            return "Retriggers all of the movement (the bells, the bands, the gesture, Wobble, PARA) together, in time with the "
                   "song: the loop region of the window (the slowest cycle of what moves) plays again every Length.";
        case kLoopStart: return "Where the loop region starts in the window (0 %: the window's start, where every movement starts together).";
        case kLoopEnd: return "Where the loop region ends in the window (100 %: the whole slowest cycle).";
        case kLoopLength:
            return "How long a pass of the region takes in the song, 1/16 to 4 bars (the region time-scaled to fit), or Natural: "
                   "its own speed, again from the next 16th of a beat after it ends. Stopped, it keeps going at the last tempo.";
        case kLoopShape:
            return "Wrap: the region plays forward, then glides back to its start over a few milliseconds (no click). Bounce: "
                   "forward over the first half, back over the second.";
        case kParaOn:
            return "Switches PARA on: the sound split into a low-pass and a high-pass path in parallel (as para does), the "
                   "low-pass path moving in and out, the high-pass path moving up and down and in and out. Before Drive and the "
                   "bands, so the movement feeds the grit.";
        case kParaLpFreq: return "The low-pass path's corner (Hz): what is below it moves in and out with LP Move.";
        case kParaHpFreq:
            return "The high-pass path's lowest corner (Hz): it moves up from here by HP Move. Above LP Freq there is a hollow "
                   "between the two paths; at LP Freq they meet flat.";
        case kParaLpMove: return "How far the low-pass path's level moves out (100 %: all the way out, in the middle of each cycle).";
        case kParaHpMove: return "How far the high-pass path's corner moves up and back (octaves), a quarter cycle ahead of the low-pass path.";
        case kParaHpLevelMove: return "How far the high-pass path's level moves out, against the low-pass path: one is in while the other is out.";
        case kParaRate: return "One cycle of PARA's movement in bars or beats, in time with the song (or with Loop Lock's segment).";
        case kParaMix: return "PARA against the dry sound: 100 % only the two paths.";
        case kSubGuard:
            return "Keeps the sub steady while everything above it moves: the lows below Freq take no dips (the bands, the "
                   "gesture's level lanes, Wobble, PARA, the LAB, the Glue and the end saturator pressed by the rest). The "
                   "SWEEP stage's bells still move them (Guard Bells keeps those off too).";
        case kSubGuardFreq: return "Below this (Hz) Sub Guard holds the level steady; above it everything moves as before.";
        case kSubFloor: return "How far the guarded lows may still dip at most (dB): 0 not at all, -12 lets some of the movement through.";
        case kGuardBells:
            return "With Sub Guard: the SWEEP stage's bells (and its saturator's response to them) stay off the lows too, so "
                   "the sub is steady under the whole sound. Off, the bells move the low end on purpose.";
        case kDriftSeed:
            return "Starts and runs every modulator (the bells, the shelf, the bands' pattern, the gesture, Wobble, PARA) a "
                   "little apart, drawn from this seed: the same seed is the same every time. 0: off, everything exactly as set.";
        case kStartDrift: return "How far each modulator's start may move, up to a whole cycle of it (with Drift Seed on).";
        case kSpeedDrift: return "How much faster or slower each modulator may run, up to 10 %, fixed per seed (with Drift Seed on).";
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
            return "Scales every 0.27 gesture slot's Depth at once (an older project's slots): 0 switches them off, 100 % "
                   "plays them as set.";
        case kScene:
            return "The gesture: one timeline of a few beats that moves many targets together (the band levels, Wobble, "
                   "Close, Liquid, Dirt, Bells, the crossovers, Seed Blend, Shift), each on its own lane with its own range, "
                   "all on one clock. The display shows every lane. User plays the file picked with File (from the Gestures "
                   "folder beside your presets), saved with the project. None: no gesture. The Low band is never moved.";
        case kSceneMode:
            return "Loop plays the whole gesture over and over in time with the song. Walk goes back and forth through it "
                   "(forwards, then backwards), at Speed. Every lane moves together either way.";
        case kSceneLength:
            return "How long the gesture lasts, in beats: Own is the length it was made with; the others stretch or squeeze "
                   "it (every lane at once) to 1/2 .. 32 beats.";
        case kSceneSpeed:
            return "How fast Walk goes through the gesture: x1 takes Length per pass, x2 half that. Hold stops it at "
                   "Position (move Position by hand or with automation to scrub every lane at once). Loop does not use it.";
        case kScenePosition:
            return "Loop: where the loop starts, as a share of Length (25 % starts it a quarter later). Walk: where it starts, "
                   "and where it stays at Hold.";
        case kSceneSmooth:
            return "Glides every lane: 0 keeps the steps sharp (they still take 2 ms, so they never click), 100 % turns them "
                   "into glides of a 16th of a beat.";
        case kSceneAmount:
            return "How far the gesture moves its targets, every lane at once: 100 % as the gesture has them, 50 % half way "
                   "from where the controls are, 0 no gesture.";
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

constexpr const char* kLabGrit =
    "How hard the chain's band drives its smacheratr (its Drive, dB). The band's moving gain comes first, so Movement and "
    "the gestures change how much it distorts. Turned on an empty chain, it loads smacheratr there.";
constexpr const char* kLabCurve =
    "What smacheratr does to the peaks after its curve (its Post Clip): No Clip, Soft Clip, or Hard Clip, the hardest edge.";
constexpr const char* kLabOtt =
    "How much the chain's multidyn squeezes its band, OTT style (its Amount, OTT's Depth): quiet detail up, peaks down. "
    "Turned on an empty chain, it loads multidyn there.";
constexpr const char* kPostDepth =
    "POST: an OTT on the sum of the chains (never on the Low band), its Amount (OTT's Depth). It glues the three bands into "
    "one dense sound; Liquid, Close, Wobble and the shifter come after it.";
constexpr const char* kPostTime = "How fast POST's OTT lets go (its Time): lower pumps quicker, higher holds longer.";
constexpr const char* kPostUp =
    "How much POST's OTT brings quiet detail up (upward compression): 100 % is OTT's own, 0 % none. Sets every band's "
    "Below ratio.";
constexpr const char* kPostDown =
    "How much POST's OTT pushes peaks down (downward compression): 100 % is OTT's own, 0 % none. Sets every band's Above "
    "ratio.";

constexpr const char* kSweepView =
    "The SWEEP stage now: each bell that is on (A, C, E, G solid; B, D, F, H dashed) and the High Shelf, each thin, and "
    "the whole stage bold (with Tone), from 20 Hz to 5 kHz. The bars show where each bell sweeps (bottom, A lowest) and "
    "where the shelf's corner goes (top). The box at the right is the shelf's orbit: its corner across (Low to High), its "
    "gain up (Min to Max), the dashed line the most it may boost at each corner (Tilt), the dot where it is now.";

constexpr const char* kGestureView =
    "The gesture: one row per lane, named by its target, its curve across the gesture (up is the top of the lane's "
    "range), the beats it plays over, and while it runs one playhead through every lane (they share one clock) with "
    "each lane's value now.";

constexpr const char* kLoopView =
    "Loop Lock's window: as long as the slowest cycle of everything that moves (named in the title), every curve across "
    "it, and the loop region shaded. Drag its edges to set Start and End, drag inside it to slide it (its length kept), "
    "double-click for the whole window. While Loop Lock runs, a line shows where the movement is.";

constexpr const char* kGestureFile =
    "Picks a gesture file (JSON, a lane per target) from the Gestures folder beside your moistr presets and sets "
    "Gesture to User; it is saved with the project, so the file is not needed again. Open Gestures Folder shows the "
    "folder (made if missing). scripts/als_extract.py --moistr writes these files from Ableton automation.";

constexpr const char* kBandView =
    "The bands against frequency. The Low band is locked (solid, with a lock and where it ends in Hz) unless Push or Dip "
    "lets it move; the other bands fill up to their level now and rise and fall while sound plays. The lines show each "
    "band's Level and how far it can move (dashed). With the shifter on, each upper band shows how far it is shifted. "
    "With Liquid on, the bar at the top shows where its resonance may go and the markers where its two peaks are now.";

} // namespace moistr::help
