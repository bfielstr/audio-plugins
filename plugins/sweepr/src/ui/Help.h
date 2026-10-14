#pragma once

#include "Params.h"

namespace sweepr::help {

inline const char* forParam (uint32_t id)
{
    if (id < kLink)
    {
        const bool b = id >= kBandB;
        switch (id - (b ? kBandB : kBandA))
        {
            case kFreq:
                return b ? "Band B's centre frequency, where its LFO sweeps from (drag the B handle sideways in the display)."
                         : "Band A's centre frequency, where its LFO sweeps from (drag the A handle sideways in the display).";
            case kQ:
                return "How narrow the bell is: low Q is broad and gentle, high Q a narrow peak or notch. A boost keeps its width "
                       "at any Gain; a cut widens the deeper it goes (the mouse wheel over a handle, or Alt and drag up and down, "
                       "changes it too).";
            case kGain:
                return "How far the bell boosts (up to +17.9 dB) or cuts (down to -18.1 dB) around its frequency (drag the "
                       "handle up and down).";
            case kLfoSync:
                return "On: the LFO's rate is a note value locked to the song position, so it sounds the same from any point of "
                       "the song. Off: it runs free at Rate.";
            case kLfoDiv:
                return "The synced LFO's length: one cycle per note value, from 1/32 to 4 bars (T: triplet, D: dotted; a bar is "
                       "four beats).";
            case kLfoRate: return "The free LFO's rate, 0.01 to 40 cycles a second (with Sync off).";
            case kLfoShape:
                return "The LFO's shape: Sine, Triangle, Saw Up, Saw Down, Square, Random (a new value each cycle) or Smooth "
                       "Random (gliding from one random value to the next).";
            case kLfoDepth:
                return "How far the LFO sweeps the band's frequency, in octaves up and down from Freq (negative: the other way "
                       "round). 0 leaves the band still.";
            case kLfoPhase:
                return b ? "Where in its cycle B's LFO starts. With Link on, B's offset from A's LFO (180: B moves against A)."
                         : "Where in its cycle A's LFO starts (synced: its offset from the beat).";
            default: return nullptr;
        }
    }
    switch (id)
    {
        case kLfoOnA:
        case kLfoOnB:
            return "The band's LFO on or off. Off, the band stays at its Freq (its LFO settings are kept for when it comes "
                   "back on).";
        case kLink:
            return "B's LFO follows A's: A's Sync, rate and Shape, with B's Phase as the offset from A (180: against A) and B's "
                   "own Depth.";
        case kTapeOn: return "The tape stage after the two bands: drive into a fast leveller and a clipper, then softer highs.";
        case kDrive:
            return "The gain into the tape, -10 to +50 dB: clean and quieter at 0, saturated and held at a steady level from "
                   "about a third up (no makeup: more drive is louder until it saturates).";
        case kSpeed:
            return "The tape speed: a high shelf after the curve. 100 %: almost flat. Lower: darker, down to -48 dB above "
                   "2 kHz at 0.";
        case kCompression:
            return "A fast leveller before the curve: higher holds the level lower, so the tape is softer, quieter and less "
                   "distorted. 0: nothing holds it, and a high Drive clips to a square wave.";
        case kMix: return "Blend of the effect and the untouched signal (kept in time with each other, so no comb filtering).";
        case kOutput: return "Overall output level.";
        default: return nullptr;
    }
}

constexpr const char* kCurveView =
    "The two bands' curve, +18 to -18 dB from 20 Hz to 20 kHz: the bright line is the curve now, with each band where its "
    "LFO has swept it (the dots); the dashed line is the curve at the knobs' settings and the bars under the handles show "
    "how far each LFO sweeps. Drag the A or B handle sideways for Freq, up and down for Gain; the mouse wheel over a "
    "handle (or Alt and drag) changes Q; a double-click resets the band.";

} // namespace sweepr::help
