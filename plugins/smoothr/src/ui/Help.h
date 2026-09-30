#pragma once

#include "../core/Params.h"

namespace smoothr::help {

inline const char* forParam (uint32_t id)
{
    switch (id)
    {
        case kInput:
            return "Gain into the chain: how hard the signal is pushed into the saturator and the limiter (the output "
                   "stays at the ceiling, so this is how much louder it gets).";
        case kCeiling:
            return "The most the output ever reaches, between the samples too (true peak, checked 8x). -1 dB is safe for "
                   "streaming and lossy encoding. There is no gain after it: the ceiling is the output level.";
        case kRelease:
            return "How fast the highs let go after a peak. The lows let go slower on their own: never faster than 60 ms "
                   "(three periods of a 50 Hz bass), and at least twice this.";
        case kAutoRelease:
            return "Program-dependent release: what has been limited for a while lets go slowly (no pumping on dense "
                   "material), a lone peak quickly.";
        case kSmooth:
            return "How much the low end is kept out of the limiting. The lows (under about 200 Hz) always move slowly; "
                   "Smooth sets how much they give way for the highs. 0 %: the lows follow the whole signal (slowly). "
                   "50 %: a kick click or a snare is left to the highs (up to 12 dB more than the lows), and when the "
                   "highs are held down for a while the lows come down three quarters as far, so the balance holds. "
                   "100 %: the lows are left alone (louder, smoother, and the highs take all of the limiting).";
        case kCharacter:
            return "A dip in the low mids (somewhere in 80 - 250 Hz), before the limiter, that only opens when the low "
                   "mids get loud: the boxy build-up stops eating headroom, so the lows get pulled down less. It is "
                   "Smacheratr's Gently (Clarity), tuned: turning it up deepens the dip (up to 6 dB), widens it and "
                   "slides it down. 0: off.";
        default: break;
    }
    return nullptr;
}

constexpr const char* kDisplay =
    "The last five seconds: the output (dark), what the limiter took off its input (lighter, above it), and the gain "
    "reduction hanging from the top, the lows' (orange) apart from the highs' (red). Smoothr keeps the orange line "
    "smooth and lets the red one take the fast peaks. The dashed line is the ceiling. At the right: the input and "
    "output meters, and the reduction now; click them to clear the holds.";

} // namespace smoothr::help
