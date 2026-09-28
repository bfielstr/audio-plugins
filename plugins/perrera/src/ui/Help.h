#pragma once

#include "../core/Params.h"

namespace perrera::help {

inline const char* forParam (uint32_t id)
{
    switch (id)
    {
        case kHpFreq: return "High-pass cutoff at the root note. With key tracking it follows the played note from here.";
        case kHpRes: return "High-pass resonance. At 0 the two filters can meet without a bump.";
        case kLpFreq: return "Low-pass cutoff at the root note.";
        case kLpRes: return "Low-pass resonance.";
        case kSlope: return "12 or 24 dB per octave for both filters.";
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
        default: return nullptr;
    }
}

constexpr const char* kDisplay =
    "Red: the high-pass, green: the low-pass, white: what you hear (their sum). Drag a handle sideways to set its "
    "cutoff, up/down for resonance; double-click resets it. Shift: fine. When a note is tracked the curves show "
    "where the filters actually are.";

} // namespace perrera::help
