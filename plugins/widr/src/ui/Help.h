#pragma once

#include "../core/Params.h"

#include <string>

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
        case kCinema:
            return "The cinema stage. widr hears the mix as five lanes (Voice, Bass, Hits, Tones, Ambience) and places "
                   "each by its Position: the Centre ones stay dry and mono in the middle, the Wide and Beyond ones are "
                   "widened. Also scales Depth and Theatre. 0 %: off, widr as before (no added latency); above 0 it "
                   "adds 21 ms of latency.";
        case kDepth:
            return "A deep, tight low end from the Bass lane: a sub an octave down and a slow low shelf that backs off "
                   "when the lows are already loud. Mono. Scaled by Cinema.";
        case kTheatre:
            return "A large, dark hall with a theatre's early reflections, fed only by the Wide and Beyond lanes (never "
                   "the Centre ones, so a voice in the Centre stays dry). Amount and size. Scaled by Cinema.";
        default:
            break;
    }
    if (id >= kLaneBase && id < kNumPluginParams)
    {
        static const char* what[kNumLanes] = {
            "Voice: speech and singing in the centre (harmonic, moving at a syllable rate).",
            "Bass: everything below about 120 Hz.",
            "Hits: drums and other onsets.",
            "Tones: held notes, pads, leads, chords.",
            "Ambience: room, reverb, noise, anything diffuse."};
        static std::string texts[2 * kNumLanes];
        const int lane = (int)(id - kLaneBase) / 2;
        std::string& t = texts[id - kLaneBase];
        if (t.empty ())
            t = std::string (what[lane]) +
                ((id - kLaneBase) % 2 == 0
                     ? " Where it goes: Centre (dry, mono in the middle, no voices or hall), Wide (widr's voices), Beyond "
                       "(stronger voices and cues that reach past the speakers)."
                     : " Width: Centre: how much of its own stereo it keeps (0 %: mono). Wide and Beyond: how much width "
                       "is added (0 %: as it is).");
        return t.c_str ();
    }
    return nullptr;
}

constexpr const char* kStage =
    "The stage from above: you at the bottom, the speakers at the sides. Lit (cinnabar): this Widr (the angle is its width, "
    "the distance its Space); dashed copper: the other Widrs in the group. With Cinema on, five thin arcs are the "
    "lanes (Voice, Bass, Hits, Tones, Ambience, inside out) where their Position puts them, lit by how much each holds. Bottom "
    "strip: the width kept per band (outlined = given to the group). Drag an end of the lit arc for Width, drag up/down for Space, double-click to reset. Shift: "
    "fine.";

constexpr const char* kGonio =
    "Goniometer of the output: mono is a vertical line, wide material a cloud. The meter shows the correlation: "
    "+1 mono, 0 unrelated, below 0 it cancels in mono.";

} // namespace widr::help
