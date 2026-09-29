#pragma once

#include "../core/Params.h"

namespace widr::help {

inline const char* forParam (uint32_t id)
{
    switch (id)
    {
        case kWidth:
            return "How loud the left and right voices are next to the dry centre. 0 %: bypass. 100 %: a clear left, "
                   "centre and right. 200 %: voices as loud as the centre.";
        case kCharacter:
            return "How the voices are made. Tight: decorrelated, right beside the centre. Wide: a second take 12 and "
                   "16 ms late on each side. Epic: later, detuned, wandering takes, big reflections, the strongest "
                   "contrast. Surround: the room and the reverb lead.";
        case kDryLevel: return "Level of the input, in parallel with what Widr adds (to the bottom: -inf).";
        case kWetLevel: return "Level of what Widr adds (the voices and the reverb), in parallel with the input.";
        case kContrast:
            return "Keeps the centre and the sides apart: the added width ducks under the hits in the mid and blooms "
                   "between them, and gives way where the mid is strong (a voice) while filling where it is thin. "
                   "Each Character has its own amount; this scales it.";
        case kSize: return "The Haas delay and the spacing of the early reflections: small to large.";
        case kSpace: return "A short, dark stereo reverb with its own left and right, going to the voices.";
        case kDecay: return "How long the Space reverb rings.";
        case kPreDelay: return "Time before the Space reverb starts.";
        case kDamping: return "Darkens the voices, the reflections and the reverb above this frequency.";
        case kAir: return "Lifts the side above about 6 kHz: an open, airy top.";
        case kBeyond:
            return "Lifts the side around 4 kHz, which dips it in the far speaker: images seem to reach past the "
                   "speakers.";
        case kMonoBelow: return "Below this frequency the output is mono, so the low end stays centred and punchy.";
        case kGuard:
            return "Keeps every band safe: the mono fold gains at most about 1.2 dB (at 100 %) and the side stays "
                   "below the mid; the voices back off where needed. 0 %: no limit.";
        case kRole:
            return "This Widr's place in the mix, for the other Widrs in its group. Anchor: narrow, it keeps the centre "
                   "and never gives way. Support, Wide, Ambient: wider, and each gives way to the roles before it.";
        case kAware:
            return "How much this Widr gives way where Widrs with a higher role already widen. 0 %: ignores the "
                   "others. Also how much the role pulls the width.";
        case kGroup: return "Widrs listen to the others in the same group (1 to 8) only.";
        case kMonoCheck: return "Listen in mono (L + R), to check the fold.";
        case kOutput: return "Overall output level.";
        default: return nullptr;
    }
}

constexpr const char* kStage =
    "The stage from above: you at the bottom, the speakers at the sides. Orange: this Widr (the angle is its width, "
    "the distance its Space); grey: the other Widrs in the group. Bottom strip: the width kept per band (blue = given "
    "to the group). Drag an end of the orange arc for Width, drag up/down for Space, double-click to reset. Shift: "
    "fine.";

constexpr const char* kGonio =
    "Goniometer of the output: mono is a vertical line, wide material a cloud. The meter shows the correlation: "
    "+1 mono, 0 unrelated, below 0 it cancels in mono.";

} // namespace widr::help
