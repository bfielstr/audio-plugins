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
        case kSlope: return "12, 18 or 24 dB per octave for both filters.";
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
                   "past Dip takes the high-pass up with it and fades it out (to -inf Fade semitones past Dip), so the "
                   "low-pass ends up sweeping alone; a high-pass swept down past the low-pass fades the low-pass out. "
                   "The filter you move also overshoots the way it moves and flows back when it stops: liquid, techy "
                   "Reese movement.";
        case kDipStart:
            return "Vocal: where the dip starts. Once the low-pass rises past this, the high-pass rises with it and "
                   "fades out over Fade.";
        case kFade:
            return "Vocal: how far past Dip the high-pass takes to fade to -inf (an octave by default, -3 dB half way). "
                   "Shorter dives faster.";
        case kLpFloor:
            return "The low-pass never goes below this, whatever Split, the envelope or the movement do, so the sub "
                   "stays.";
        case kResLink: return "Link the resonances: the low-pass uses the high-pass resonance, so one control sets both.";
        case kDragGain:
            return "On: dragging a handle in the display up or down moves its gain along with its resonance. Off: "
                   "only the resonance (Alt-drag moves the gain alone).";
        case kHpGain: return "Level of the high-pass filter's output, down to -inf (only the low-pass is heard).";
        case kLpGain: return "Level of the low-pass filter's output, down to -inf (only the high-pass is heard).";
        case kDriveOn:
            return "Drive: Smacheratr's Analog curve (4x oversampled) in Para's own path, apart from the saturator "
                   "at the end. Off, the sound passes untouched.";
        case kDrive: return "How hard the drive pushes into the curve (0 dB: only peaks above -6 dBFS bend).";
        case kDrivePos:
            return "Pre: the drive goes in before the filters, so they shape the harmonics it makes (a low-pass takes the "
                   "top ones away). Post: after the filters (and Dry/Wet, before Output), so its harmonics stay. Switching "
                   "fades the sound out and back in for a moment.";
        default: return nullptr;
    }
}

constexpr const char* kDisplay =
    "Orange: the high-pass, blue: the low-pass, white: what you hear (their sum). Behind them the live spectrum of "
    "the input (grey) and the output (light). Drag a handle sideways for its cutoff and up/down for its resonance "
    "(with Drag Gain on, the gain moves too); Alt-drag for the gain alone (to the bottom: -inf); double-click resets "
    "it. Mouse wheel: resonance (while holding a handle, or with Shift over it). Shift: fine. A handle sits as high "
    "as its resonant peak; the handles follow the tracked note and glow with "
    "the envelope.";

} // namespace para::help
