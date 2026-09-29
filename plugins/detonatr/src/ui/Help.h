#pragma once

#include "../core/Params.h"

namespace detonatr::help {

inline constexpr const char* kStageStrip =
    "The chain, left to right. Click a stage to show its controls; click its light to turn it on or off (off, it only "
    "delays, so nothing moves in time); drag it sideways to move it in the chain.";
inline constexpr const char* kHitView =
    "The last second: grey is the input (lined up with the output), orange the output, in dB. Click to change how much "
    "time it shows.";
inline constexpr const char* kRecording =
    "A recording of a household item (a pot, a glass, a pipe...). Drop an audio file here or click to pick one; it loops "
    "and the input's bands play it (a vocoder), so its tone takes the input's shape. The project keeps its audio "
    "(up to 10 seconds).";
inline constexpr const char* kClearRecording = "Empties this slot.";
inline constexpr const char* kTransientCurve =
    "The level after each hit: full for the Spike, down to the Drop over the Fall, and held there until the next hit.";

inline const char* forParam (uint32_t id)
{
    if (id >= kOrderBase && id < kOrderBase + kNumStages)
        return "Which stage runs at this place in the chain (drag the stages in the strip).";
    if (id >= kCarrierLevel1 && id <= kCarrierLevel4)
        return "How loud this recording is in the Tone stage.";
    switch (id)
    {
        case kOutput: return "The level at the very end.";
        case kDryWet: return "How much of the processed sound you hear against the input (lined up in time).";
        case kCleanOn: return "The Clean stage: a denoiser and dereverber (off: it only delays).";
        case kDenoise:
            return "Turns down the steady noise floor (hiss, rumble) it learns from the quiet parts, keeping what stands "
                   "out of it: the tones.";
        case kDereverb: return "Turns down the room: the part of each frequency that is only the decaying tail of what came before.";
        case kToneOn: return "The Tone stage: tuned resonators and vocoded recordings make the sound tonal.";
        case kRoot: return "The resonators' fundamental. Designed impacts often sit around 50 to 85 Hz (E1 is 82.4 Hz).";
        case kMaterial: return "The resonators' material: which overtones ring, how loud and for how long.";
        case kDecay: return "How long the resonators ring (the material makes some overtones shorter).";
        case kResonators: return "How loud the tuned resonators are.";
        case kCarriers: return "How loud the vocoded recordings are (the slots below).";
        case kToneDry: return "How much of the input passes through the Tone stage as it is.";
        case kDisperse:
            return "Smears the phase (a chain of all-pass filters): the chirpy, laser-like disperser sound. The level "
                   "stays the same.";
        case kDisperseFreq: return "Where the dispersion is centred.";
        case kMultibandOn: return "The Multiband stage: Multidyn, squashing each band (off: it only delays).";
        case kTransientOn: return "The Transient stage: a short spike at each hit, the rest turned down (off: it only delays).";
        case kSpike: return "How long the start of each hit stays at full level.";
        case kDrop: return "How far the rest of the hit is turned down. The Saturator after it raises it back up, dense and loud.";
        case kFall: return "How quickly the level goes from the spike down to the drop.";
        case kSensitivity: return "How far the level must jump to count as a new hit (higher: only clear hits).";
        default: return nullptr;
    }
}

} // namespace detonatr::help
