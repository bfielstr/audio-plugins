#pragma once

#include "../core/Params.h"

namespace para::help {

inline const char* forParam (uint32_t id)
{
    switch (id)
    {
        case kHpFreq: return "High-pass cutoff at the root note. With key tracking it follows the played note from here.";
        case kHpRes: return "High-pass resonance. At 0 the two filters can meet without a bump.";
        case kLpFreq: return "Low-pass cutoff at the root note.";
        case kLpRes: return "Low-pass resonance.";
        case kSlope: return "12, 18 or 24 dB per octave for both filters.";
        case kSplit:
            return "Moves the two filters apart (positive: high-pass up, low-pass down) or together (negative), in "
                   "semitones around their set frequencies. Automate it, or let the envelope drive it.";
        case kEnvAmount: return "Semitones of Split the envelope adds at its peak, on every note. Negative pulls them together.";
        case kEnvAttack: return "Time the envelope takes to reach its peak after a note.";
        case kEnvDecay: return "Time the envelope takes to fall back.";
        case kKey:
            return "How much both cutoffs follow the played note: 100 % keeps the filters on the same harmonics of "
                   "every note. Transpose and pitch bend count, and the root note is where the cutoffs sit as set.";
        case kTranspose: return "Added to every played note before tracking.";
        case kPbRange: return "Pitch bend range in semitones, applied to the tracked note.";
        case kRoot: return "The MIDI note at which the cutoffs sit at their set frequencies (60 = C3).";
        case kDryWet: return "Balance between the dry input and the filtered signal.";
        case kOutput: return "Output level.";
        case kMovement:
            return "Free: the filters move independently. Vocal: the filter you moved last leads; when it "
                   "crosses the other, the other is pushed along and fades out (-inf an octave past), so one "
                   "filter sweeps alone. A low-pass swept up takes the high-pass with it, a resonant high-pass "
                   "swept down fades the low-pass out.";
        case kResLink: return "Link the resonances: the low-pass uses the high-pass resonance, so one control sets both.";
        case kDragGain:
            return "On: dragging a handle in the display up or down moves its gain along with its resonance. Off: "
                   "only the resonance (Alt-drag moves the gain alone).";
        case kHpGain: return "Level of the high-pass filter's output, down to -inf (only the low-pass is heard).";
        case kLpGain: return "Level of the low-pass filter's output, down to -inf (only the high-pass is heard).";
        default: return nullptr;
    }
}

constexpr const char* kDisplay =
    "Orange: the high-pass, blue: the low-pass, white: what you hear (their sum). Behind them the live spectrum of "
    "the input (grey) and the output (light). Drag a handle sideways for its cutoff and up/down for its resonance "
    "(with Drag Gain on, the gain moves too); Alt-drag for the gain alone (to the bottom: -inf); double-click resets "
    "it. Shift: fine. A handle sits as high as its resonant peak; the handles follow the tracked note and glow with "
    "the envelope.";

} // namespace para::help
