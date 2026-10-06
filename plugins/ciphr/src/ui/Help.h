#pragma once

#include "Params.h"

namespace ciphr::help {

inline const char* forParam (uint32_t id)
{
    switch (id)
    {
        case kTimbre:
            return "Scans each oscillator's list of waves and pitches: 0 plays every list's first entry, 100 % its last, with "
                   "smooth crossfades between neighbours.";
        case kCross:
            return "How the oscillators work on each other. Centre: clean. Left: FM, each oscillator bends the next one's "
                   "phase. Right: ring modulation, each one multiplied by the next.";
        case kCharacter:
            return "Widens the cluster's detune, adds taps to the processor (2 at 0, 8 at 100 %) and brightens its "
                   "feedback.";
        case kVariant:
            return "Picks a set of waves and pitches for every oscillator and the processor's tap pattern. The same number "
                   "always gives the same sound.";
        case kDrift:
            return "Lets the oscillators' list positions, their tuning and the taps wander slowly between random states. 0 "
                   "keeps everything still.";
        case kTune: return "Transposes every voice (semitones).";
        case kInput: return "The level of the stereo input (the plug-in's side-chain input) mixed into the sound.";
        case kInputPath:
            return "Direct: the input joins the voices straight into the processor. Voices: each playing note gets the "
                   "input before its filter, so you hear it only while keys are held.";
        case kCutoff: return "The filter's cutoff (or centre) frequency.";
        case kResonance: return "Emphasis at the cutoff frequency.";
        case kFilterType: return "Morphs the filter: low-pass at 0, band-pass at 50 %, high-pass at 100 %.";
        case kKeyTrack: return "How far the cutoff follows the note: at 100 % it moves an octave per octave (from C3).";
        case kEnvAmount:
            return "How far the filter envelope moves the cutoff: up to 5 octaves up (right) or down (left).";
        case kAttack: return "The amp envelope's rise time.";
        case kDecay: return "The amp envelope's fall time to the Sustain level.";
        case kSustain: return "The amp envelope's level while the key is held.";
        case kRelease: return "The amp envelope's fade time after the key is let go.";
        case kFilterAttack: return "The filter envelope's rise time.";
        case kFilterDecay: return "The filter envelope's fall time to its Sustain level.";
        case kFilterSustain: return "The filter envelope's level while the key is held.";
        case kFilterRelease: return "The filter envelope's fall time after the key is let go.";
        case kVelocity: return "How much the key velocity sets the level: 0 plays every note equally loud.";
        case kSpace:
            return "From discrete echoes (0: the taps of a multi-tap delay) to a dense, diffuse wash like a reverb (100 %).";
        case kLength: return "The longest echo's time: every tap and diffuser scales with it.";
        case kMovement: return "Slowly wobbles the echoes' times for a moving, chorused sound.";
        case kRegen:
            return "Feedback through the frequency shifter. Right: only the shifted sound comes back, so each echo climbs "
                   "or falls further. Left: shifted and unshifted together, for moving notches.";
        case kShift: return "How far the frequency shifter moves the echoes' frequencies each time round (Hz).";
        case kBlend: return "Dry voices against the processor's output.";
        case kOutput: return "Overall output level.";
        case kStretch:
            return "Pulls the oscillators off their pitches to uneven ratios, like a struck bar or plate: from clean (0) "
                   "to clanging metal (100 %). Strongest with the Metal waves and Cross.";
        case kWaveSet:
            return "Which waves Variant deals from. Classic: the original twelve. Alien, Metal and Voice: each "
                   "oscillator's list goes from calm to harsh (or from a closed vowel to an open one), so Timbre "
                   "sweeps into screeches, scrapes or voices.";
        case kDisperseOn: return "Switches Disperse on: the sound is split into bands that the Disperse dial raises one by one.";
        case kDisperse:
            return "The dial: at 0 every band is silent, turning it up raises the bands one after another (in the "
                   "Seed's order), each from silent to full; at 100 % every band is full.";
        case kDisperseBands: return "How many bands the sound is split into (4 to 32), spread from 80 Hz to 12 kHz.";
        case kDisperseSeed: return "The order the bands rise in. The same Seed and Bands always give the same order.";
        case kDisperseWidth:
            return "How wide each band is. 100 %: the bands together sound like the input. Lower: narrow, ringing bands "
                   "with gaps between them, like a vocoder.";
        case kDisperseMix: return "The input against the bands: 100 % hears only the bands.";
        default: return nullptr;
    }
}

constexpr const char* kCipherView =
    "Left: each oscillator's list of waves (rows), with its pitch offset under each wave and a cinnabar mark where "
    "Timbre (and Drift) has it now. Right: the processor's taps against time, their heights their levels, and a haze "
    "for Space's diffusion.";

constexpr const char* kDisperseView =
    "Disperse's bands from 80 Hz (left) to 12 kHz (right), each bar as tall as the band's level at the dial now "
    "(no bar: silent; to the top: full). The outlined slot is the next band to rise.";

} // namespace ciphr::help
