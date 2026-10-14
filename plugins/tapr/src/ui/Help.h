#pragma once

#include "Params.h"

namespace tapr::help {

inline const char* forParam (uint32_t id)
{
    switch (id)
    {
        case kTapeOn: return "The tape stage: Drive into the leveller and the curve, then Speed's high shelf. Off, only the Cut EQ shapes the sound.";
        case kDrive:
            return "The gain into the tape, -10 to +50 dB: clean and quieter at 0, saturated and held at a steady level from "
                   "about a third up (no makeup: more drive is louder until it saturates).";
        case kSpeed:
            return "The tape speed: a high shelf after the curve. 100 %: almost flat. Lower: darker, down to -48 dB above "
                   "2 kHz at 0.";
        case kCompression:
            return "A fast leveller (2 ms attack, 21 ms release): higher holds the level lower. Pre, before the curve, it makes "
                   "the tape softer, quieter and less distorted (0: a high Drive clips to a square wave). Post, after the "
                   "curve, it rides the saturated sound down and keeps the drive as hard.";
        case kCompPlace:
            return "Where the leveller sits: Pre, before the curve (the classic tape: it sets how hard the curve is driven), or "
                   "Post, after it (it only holds the level of what the curve made).";
        case kCutOn: return "The Cut EQ: three bells (Lo 126 Hz, Mid 983 Hz, Hi 9.8 kHz). At 0 dB each does nothing.";
        case kCutLo: return "The Cut EQ's low bell, at 126 Hz (a broad Q of 1): -18 to +18 dB. Drag LO on the display too.";
        case kCutMid: return "The Cut EQ's middle bell, at 983 Hz (a broad Q of 1): -18 to +18 dB. Drag MID on the display too.";
        case kCutHi: return "The Cut EQ's high bell, at 9.8 kHz (a broad Q of 1): -18 to +18 dB. Drag HI on the display too.";
        case kCutPlace:
            return "Where the Cut EQ sits: Post, after the tape (the classic place: it shapes the saturated sound), or Pre, "
                   "before it (it shapes what goes into the saturation: a boost saturates harder, a cut cleaner).";
        case kMix: return "Blend of the effect and the untouched signal (kept in time with each other, so no comb filtering).";
        case kOutput: return "Overall output level.";
        default: return nullptr;
    }
}

constexpr const char* kTapeView =
    "At the left, the response of Speed's shelf and the Cut EQ, +18 to -18 dB from 20 Hz to 20 kHz: drag LO, MID or HI up "
    "and down for its gain, double-click it for 0 dB. At the right, the tape's curve, with the level reaching it now (the "
    "markers) and the leveller's gain while audio plays.";

} // namespace tapr::help
