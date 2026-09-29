#pragma once

#include "Params.h"

namespace widr::help {

inline const char* forParam (uint32_t id)
{
    switch (id)
    {
        case kWidth:
            return "How much width Widr adds. 0 %: bypass. 100 %: the image fills the speakers. 200 %: past them. "
                   "It only adds side signal, so the mono fold keeps the mid.";
        case kCharacter:
            return "How the width is made. Tight: clean decorrelation, no delay or room. Wide: a Haas pair up front. "
                   "Epic: big reflections, a detuned spread and the strongest contrast. Surround: the reverb and a "
                   "large room lead.";
        case kContrast:
            return "Keeps the centre and the sides apart: the added width ducks under the hits in the mid and blooms "
                   "between them, and gives way where the mid is strong (a voice) while filling where it is thin. "
                   "Each Character has its own amount; this scales it.";
        case kSize: return "The Haas delay and the spacing of the early reflections: small to large.";
        case kSpace: return "A short stereo reverb, added to the sides only: it reads as width, not distance.";
        case kDecay: return "How long the Space reverb rings.";
        case kPreDelay: return "Time before the Space reverb starts.";
        case kDamping: return "Darkens the reflections and the reverb above this frequency.";
        case kAir: return "Lifts the side above about 6 kHz: an open, airy top.";
        case kBeyond:
            return "Lifts the side around 4 kHz, which dips it in the far speaker: images seem to reach past the "
                   "speakers.";
        case kMonoBelow: return "Below this frequency the output is mono, so the low end stays centred and punchy.";
        case kGuard:
            return "Keeps every band from getting too wide: where the side gets close to the mid, the added width "
                   "backs off. 0 %: no limit.";
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
