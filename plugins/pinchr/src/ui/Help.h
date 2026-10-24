#pragma once

#include "Params.h"

namespace pinchr::help {

inline const char* forParam (uint32_t id)
{
    switch (id)
    {
        case kInput:
            return "The level into the compressor, -21 to +21 dB. There is no makeup gain after it: more Input "
                   "compresses harder and is louder until the compression holds it.";
        case kRatio:
            return "How hard the compressor works, 1 to 11: higher lowers the threshold and steepens the knee, as the "
                   "hardware's Ratio does (a position, not a fixed ratio).";
        case kCurve:
            return "The knee's diodes: R Soft (a resistor, the gentlest and cleanest), Germ Soft (one germanium "
                   "diode), Germ Med (five), Silicon Med (one silicon diode), Silicon Hard (two) and Zener Hard (the "
                   "sharpest and earliest).";
        case kAttack:
            return "How fast the compressor reacts, 0 (Fast, about 10 ms) to 10 (Slow, about 100 ms). Slower settles "
                   "with a little less gain reduction.";
        case kRelease:
            return "How fast the compressor lets go, 0 (Fast, about 30 ms) to 10 (Slow, about 300 ms from deep gain "
                   "reduction). Slower also holds a little more gain reduction.";
        case kScFilter:
            return "Takes the lows out of what the compressor listens to, so bass pumps it less: Off, then 30 to 300 "
                   "Hz (broad shelves; 300 Hz leaves it hearing mostly the mids and highs). The detector always "
                   "ignores the deepest lows.";
        case kDirty:
            return "The FET without its distortion compensation: adds a second harmonic that grows with the level "
                   "and falls as the gain reduction deepens.";
        case kMix:
            return "Blend of the compressed and the uncompressed signal (parallel compression). Both go through the "
                   "output stage, so Drive and Feedback colour either.";
        case kDrive:
            return "The germanium output stage's gain switch, 0 to 10 in steps of about 3 dB. Higher drive also "
                   "raises the low resonance and the top end's lift.";
        case kFeedback:
            return "The output stage's feedback, 0 to 10: up adds gain and a resonant bass bump (about 20 to 90 Hz, "
                   "higher with more Drive) with a steep cut below it. At 0 the stage is quiet and flat.";
        case kOutput: return "Overall output level.";
        case kLink: return "One detector for both channels (the louder sets the gain), so the image stays put. Off: each channel on its own.";
        case kMode: return "Stereo (left and right) or Mid/Side (the compressor and the output stage work on the mid and the side).";
        case kExtSc:
            return "The compressor listens to the sidechain input instead of the sound it compresses (route one to "
                   "pinchr's sidechain in the host; without one, it listens to its input as usual).";
        case kScGain: return "The sidechain input's level into the compressor, -24 to +24 dB (0: as the input would be heard).";
        default: return nullptr;
    }
}

constexpr const char* kPinchView =
    "At the left, the output stage's response (Drive and Feedback) from 10 Hz to 20 kHz, relative to its gain at "
    "1 kHz (shown in the corner). At the right, the compression curve for the Curve and Ratio: the level out "
    "against the level in, with the level reaching the detector now and the gain reduction while audio plays.";

} // namespace pinchr::help
