#pragma once

#include "../core/Params.h"

namespace para::help {

inline const char* forParam (uint32_t id)
{
    switch (id)
    {
        case kHpFreq: return "High-pass cutoff.";
        case kHpRes: return "High-pass resonance. At 0 the two filters can meet without a bump.";
        case kLpFreq: return "Low-pass cutoff.";
        case kLpRes: return "Low-pass resonance.";
        case kHpSlope:
            return "Steepness of the high-pass: 6 to 96 dB per octave, or Brickwall (80 dB down just past the cutoff). "
                   "With both slopes the same, the two filters meeting at one frequency sum flat. From 36 dB on the "
                   "resonance peaks as high as at 24 dB; 6 dB and Brickwall get a resonant bell at the cutoff. Click "
                   "for the list, or scroll over it.";
        case kLpSlope:
            return "Steepness of the low-pass: 6 to 96 dB per octave, or Brickwall (80 dB down just past the cutoff). "
                   "With both slopes the same, the two filters meeting at one frequency sum flat. Click for the "
                   "list, or scroll over it.";
        case kHpGainLock:
            return "Locks the high-pass gain at 0 dB at most: the knob and the display's handle stop at 0 dB, and "
                   "switching it on brings a higher gain down to 0 dB. On by default.";
        case kLpGainLock:
            return "Locks the low-pass gain at 0 dB at most: the knob and the display's handle stop at 0 dB, and "
                   "switching it on brings a higher gain down to 0 dB.";
        case kSplit:
            return "Moves the two filters apart (positive: high-pass up, low-pass down) or together (negative), in "
                   "semitones around their set frequencies. Automate it, or let the envelope drive it.";
        case kEnvAmount: return "Semitones of Split the envelope adds at its peak, on every note. Negative pulls them together.";
        case kEnvAttack: return "Time the envelope takes to reach its peak after a note.";
        case kEnvDecay: return "Time the envelope takes to fall back.";
        case kDryWet: return "Balance between the dry input and the filtered signal.";
        case kOutput: return "Output level.";
        case kMovement:
            return "Free: the filters move independently. Vocal: the filter you moved last leads. A low-pass swept up "
                   "past Dip takes the high-pass up with it and fades it out (evenly in dB, to -inf Fade semitones past Dip), so the "
                   "low-pass ends up sweeping alone; a high-pass swept down past the low-pass fades the low-pass out. "
                   "The filter you move also overshoots the way it moves and flows back when it stops: liquid, techy "
                   "Reese movement.";
        case kDipStart:
            return "Vocal: where the dip starts. Once the low-pass rises past this, the high-pass rises with it and "
                   "fades out over Fade.";
        case kFade:
            return "Vocal: how far past Dip the pushed filter takes to fade out, in semitones (30 by default, up to 60). "
                   "It fades evenly in dB, 0 to -36 dB over the Fade (-18 dB half way), and is silent at its end. "
                   "Shorter dives faster.";
        case kLpFloor:
            return "The low-pass never goes below this, whatever Split, the envelope or the movement do, so the sub "
                   "stays.";
        case kResLink: return "Link the resonances: the low-pass uses the high-pass resonance, so one control sets both.";
        case kDragGain:
            return "On: dragging a handle in the display up or down moves its gain along with its resonance. Off: "
                   "only the resonance (Alt-drag moves the gain alone).";
        case kHpGain:
            return "Level of the high-pass filter's output, down to -inf (only the low-pass is heard), up to +12 dB "
                   "(0 dB with its Lock on).";
        case kLpGain:
            return "Level of the low-pass filter's output, down to -inf (only the high-pass is heard), up to +12 dB "
                   "(0 dB with its Lock on).";
        case kHpDriveOn:
            return "High-pass drive: Smacheratr's Analog curve (4x oversampled) in the high-pass filter's branch only, "
                   "apart from the saturator at the end. Off, that branch passes untouched.";
        case kHpDrive:
            return "How hard the high-pass drive pushes into the curve (0 dB: only peaks above -6 dBFS bend).";
        case kLpDriveOn:
            return "Low-pass drive: Smacheratr's Analog curve (4x oversampled) in the low-pass filter's branch only. "
                   "With both drives on, each band saturates on its own (the lows do not bend the highs).";
        case kLpDrive:
            return "How hard the low-pass drive pushes into the curve (0 dB: only peaks above -6 dBFS bend).";
        case kDrivePos:
            return "For both drives. Pre: each drive goes in before its filter, so the filter shapes the harmonics it "
                   "makes (the low-pass takes the top ones away). Post: after its filter (before its gain), so its "
                   "harmonics stay. The dry part of Dry/Wet is never driven. Switching fades the sound out and back in "
                   "for a moment.";
        default: return nullptr;
    }
}

constexpr const char* kDisplay =
    "Solid copper line: the high-pass (HP), dashed: the low-pass (LP), the bright line: what you hear (their sum). "
    "Behind them the live spectrum of the input (dim) and the output (copper). Drag a handle sideways for its cutoff and up/down for its resonance "
    "(with Drag Gain on, the gain moves too); Alt-drag for the gain alone (to the bottom: -inf; a locked gain stops "
    "at 0 dB); double-click resets "
    "it. Mouse wheel: resonance (while holding a handle, or with Shift over it). Shift: fine. A handle sits as high "
    "as its resonant peak; the handles follow the tracked note and glow with "
    "the envelope.";

} // namespace para::help
