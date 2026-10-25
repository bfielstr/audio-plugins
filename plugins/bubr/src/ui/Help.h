#pragma once

#include "Params.h"

#include "pluginkit/EndClip.h"

namespace bubr::help {

inline const char* forParam (uint32_t id)
{
    switch (id)
    {
        case kBoost:
            return "How much of the effect is added, 0 to 110 %: 0.3 dB a percent, part into the clipper and part after it. "
                   "The part into the clipper is taken back as the band gets loud, so Boost lifts the quieter passages most.";
        case kFreq:
            return "Where the effect sits, 20 to 125 Hz: the centre of the band the effect is made from. Tune it to the "
                   "sub or low end of the source.";
        case kIntensity:
            return "How hard the band drives the clipper, from subtle (0 %: hardly clipping, about unity gain) to heavily "
                   "processed (100 %: some 36 dB more). The effect's ceiling stays where it is.";
        case kQFactor:
            return "The band's focus: low values spread the effect wide around Freq, high values narrow it onto Freq (and "
                   "drive the clipper harder there).";
        case kDrive:
            return "The gain after the clipper, 0 to 100 % (about 5.7 dB): a louder effect at the same amount of clipping.";
        case kDriveX2: return "Doubles Drive's stage: about 9.6 dB at 100 % instead of 5.7, a little more distortion near the knee.";
        case kGate:
            return "The gate's threshold, -60 to 0 dB, on the input's peak (both channels together): below it the effect "
                   "is turned down, so it only comes in on the louder parts.";
        case kAttack: return "How fast the gate opens once the input passes the threshold, 0 (at once) to 300 ms.";
        case kRelease: return "How fast the gate closes when the input falls below the threshold, 20 to 500 ms.";
        case kRatio:
            return "How strongly the gate turns the effect down below the threshold: Ratio - 1 dB a dB (4: 3 dB a dB). "
                   "The middle of the knob is 10; from about 50 the gate shuts.";
        case kLowCut:
            return "A fourth-order high-pass on the effect, 20 to 125 Hz (20 Hz: the lowest it goes; it is always in). "
                   "While Dry/Wet mixes the dry signal in (0 % and up) it filters the dry signal too; below 0 % it fades "
                   "out of the dry.";
        case kDryWet:
            return "The balance: -100 % the dry signal only, 0 % both at full level (the default), +100 % the effect only.";
        case kOversample: return "Runs the clipper at 2x or 4x the rate (less aliasing when it is driven hard), at the cost of some latency and CPU.";
        case kLookahead:
            return "The output limiter (0 dBFS) ramps its gain down 1 ms early (that latency): cleaner when it limits. "
                   "Off: no latency, it acts at once.";
        case kOutput: return "The output level, -60 dB (silence) to 0 dB, after the output limiter.";
        case kSoftClip: return pk::kSoftClipHelp;
        case kHardClip: return pk::kHardClipHelp;
        default: return nullptr;
    }
}

constexpr const char* kBubrView =
    "At the left, the effect's response (the band at Freq and Q-Factor with its gains and Low Cut, before it clips) from "
    "10 Hz to 1 kHz, Freq and Low Cut marked. At the right, the gate's curve: the effect's level against the input's "
    "peak, with the input now, the gate's gain and the output limiter's gain reduction while audio plays.";

} // namespace bubr::help
