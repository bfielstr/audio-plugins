// Hover help shown as tooltips (toggle with the "?" button in the top bar).
#pragma once

#include "Params.h"

#include "multidyn/src/ui/Help.h"
#include "para/src/ui/Help.h"

namespace smemplr::help {

inline const char* forParam (uint32_t id)
{
    if (id == kFxParaOn)
        return "Para after the sampler: a parallel high-pass and low-pass that track the notes you play here.";
    if (id == kFxMdOn)
        return "Multidyn (with its Smacheratr) after Para. Off, the sound passes untouched with the same latency.";
    switch (id)
    {
        case kMsOn:
            return "The mid/side EQ after the effects: a high-pass on the side signal makes the low end mono below "
                   "its cutoff.";
        case kMsSideHp: return "Below this frequency the side signal is removed, so the low end is mono.";
        case kMsSlope:
            return "How steeply the sides are tapered below the cutoff: 6 to 96 dB per octave, or Brickwall (a very steep "
                   "cut: the side is gone just below the cutoff).";
        case kMsSideGain: return "Level of the side signal (the stereo width).";
        case kMsMidGain: return "Level of the mid signal.";
        case kRootKey: return "The note on which the sample plays at its own pitch (C3 by default).";
        default: break;
    }
    if (isModLfoParam (id))
        switch ((id - kModLfoBase) % kModLfoFields)
        {
            case kModShape:
                return "The modulation LFO's shape: Sine, Triangle, Saw Up, Saw Down, Square, S&H (a new random value every "
                       "cycle) or Smooth Random (gliding from one random value to the next).";
            case kModRate: return "The LFO's speed in Hz, 0.01 to 40 (while Sync is Off).";
            case kModSync:
                return "Off: the LFO runs at its Rate in Hz. A note length (1/64 to 8 Bars): it runs at the host's tempo (120 "
                       "BPM without one), and while the host plays it follows the song position (unless Retrig is on).";
            case kModPhase: return "Where in its cycle the LFO starts (on Retrig) or is shifted to, 0 to 360 degrees.";
            default:
                return "Retrig: every note starts the LFO again at its Phase. Off: it runs freely (synced, with the song).";
        }
    if (id >= kTailBase)
        return nullptr; // the saturator panel has its own tips
    if (id == kParaDragGain)
        return para::help::forParam (para::kDragGain);
    if (id == kParaLiquid)
        return para::help::forParam (para::kLiquid);
    if (id == kParaFade)
        return para::help::forParam (para::kFade);
    if (id == kParaNotch)
        return para::help::forParam (para::kNotch);
    if (id >= kFxParaBase && id < kFxParaBase + para::kHostedParams)
        return para::help::forParam (id - kFxParaBase);
    if (id >= kFxMdBase && id < kFxMdBase + kLegacyMdParams)
        return multidyn::help::forParam (id - kFxMdBase);
    switch (id)
    {
        case kMode:
            return "Playback mode. Classic: pitched instrument with ADSR and looping. One-Shot: plays the whole region "
                   "once, monophonic (drums, phrases). Slicing: cuts the sample into slices played from C1 upwards.";
        case kGain: return "Level of the sample before the filter (separate from the output Volume).";
        case kStart: return "Where playback starts, as a percentage of the region between the flags.";
        case kLength:
            return "The loop's length, from Start (a share of the region between the flags). The sample itself "
                   "always plays to the end flag; with Loop off Length does nothing.";
        case kLoopLen: return "Not used any more: Length sets the loop.";
        case kLoopFade:
            return "Crossfades the loop end into the loop start to hide clicks, and fades the loop's start in on the "
                   "first pass too, so the note does not start abruptly. Off (0 %) by default. Not available while "
                   "warping.";
        case kLoopOn: return "Loop from Start while the note is held (on by default). You can also click the loop bar in the waveform.";
        case kSnap: return "Snap the flags and loop points to zero crossings (left channel) to avoid clicks.";
        case kVoices: return "Maximum number of simultaneous notes. The oldest note is faded out when you exceed it.";
        case kRetrig: return "Replaying a note that is still sounding cuts the old one instead of overlapping it.";
        case kTriggerGate:
            return "Trigger: the sample plays to the end regardless of note length. Gate: releasing the key fades it "
                   "out over the Fade Out time.";
        case kFadeIn: return "Fade-in time at the start of the sample (or slice).";
        case kFadeOut: return "Fade-out time before the end of the sample (or slice). In Gate mode also the release time.";
        case kSliceBy:
            return "How slices are made: Transient (detected hits), Beat (musical divisions), Region (equal parts) or "
                   "Manual (double-click the waveform to add slices).";
        case kSensitivity: return "Transient detection sensitivity: higher finds more slices (up to 64).";
        case kDivision: return "Beat length of each slice, based on the Warp As length of the region.";
        case kRegions: return "Number of equal slices.";
        case kSlicePlayback:
            return "Mono: one slice at a time. Poly: slices can overlap. Thru: a slice keeps playing through to the end "
                   "of the region.";
        case kWarp: return "Warp on: the sample follows the host tempo whatever note you play; notes change pitch only.";
        case kWarpMode:
            return "Beats: drums and loops. Tones: monophonic pitched material. Texture: pads and noise. Re-Pitch: "
                   "speed follows tempo like a turntable. Complex / Complex Pro: full mixes (more CPU).";
        case kWarpBeats: return "How many beats the region between the flags lasts. Sets the sample's tempo.";
        case kBeatsPreserve: return "Beats mode: where the audio is cut into segments (at transients or on a beat grid).";
        case kBeatsLoop: return "Beats mode: what fills the gap when a segment ends early (slower tempos): silence, a forward loop or back-and-forth loop.";
        case kBeatsEnvelope: return "Beats mode: fades each segment. 100 = no fade, lower = tighter, gated segments.";
        case kTonesGrain: return "Tones mode: grain size. Larger suits lower-pitched material.";
        case kTextureGrain: return "Texture mode: grain size.";
        case kTextureFlux: return "Texture mode: randomness of the grain positions.";
        case kFormants: return "Complex Pro: how much the vocal/instrument formants are kept when transposing.";
        case kCproEnvelope: return "Complex Pro: detail of the spectral envelope used for formant preservation.";
        case kFilterOn: return "Switch the filter on or off (off saves CPU).";
        case kFilterType: return "Low-pass, high-pass, band-pass, notch, or Morph (sweeps continuously between them).";
        case kFilterCircuit:
            return "Filter character. Clean: transparent. OSR: hard-clipped resonance. MS2: soft-clipped. SMP: ladder "
                   "with soft feedback. PRD: ladder with a driven input. MS2/SMP/PRD are for low- and high-pass.";
        case kFilterSlope: return "12 dB or 24 dB per octave.";
        case kFilterFreq: return "Cutoff frequency. You can also drag horizontally in the filter display.";
        case kFilterRes: return "Resonance: boosts frequencies around the cutoff. Drag vertically in the display too.";
        case kFilterDrive: return "Drives the signal into the filter circuit for saturation (not for Clean).";
        case kFilterMorph: return "Morph filter position: low-pass > band-pass > high-pass > notch > low-pass.";
        case kFilterVel: return "How much note velocity raises the cutoff.";
        case kFilterKey: return "Key tracking: 100% moves the cutoff one semitone per semitone played (relative to C3).";
        case kFilterEnvAmt: return "How far the filter envelope moves the cutoff, in semitones.";
        case kPitchEnvAmt: return "How far the pitch envelope bends the pitch, in semitones.";
        case kAmpLoopMode:
            return "Amp envelope looping while a note is held. Loop / Trigger: restart after the decay (Trigger also "
                   "ignores note-off until the cycle ends). Beat / Sync: restart every Rate (Sync locks to the bar grid).";
        case kAmpLoopTime: return "Loop / Trigger: time to return from the sustain level to the start before restarting.";
        case kAmpLoopRate: return "Beat / Sync: how often the envelope restarts.";
        case kLfoOn: return "Switch the LFO on or off.";
        case kLfoWave: return "LFO waveform.";
        case kLfoSync: return "Rate in Hz, or synced to the host tempo.";
        case kLfoRate:
        case kLfoSyncRate: return "LFO speed.";
        case kLfoAttack: return "Time for the LFO to fade in after each note starts.";
        case kLfoRetrig: return "R: restart the LFO at the Offset phase on every note.";
        case kLfoOffset: return "Start phase of the LFO when R (retrigger) is on.";
        case kLfoKey: return "Higher notes get faster LFOs.";
        case kLfoVol: return "LFO to volume (tremolo).";
        case kLfoPitch: return "LFO to pitch (vibrato), up to +/-12 semitones.";
        case kLfoPan: return "LFO to pan (auto-pan).";
        case kLfoFilter: return "LFO to filter cutoff (wah).";
        case kPan: return "Stereo position.";
        case kPanRand: return "Random pan per note.";
        case kSpread: return "Stereo chorus: each note uses two detuned voices panned left and right.";
        case kVolume: return "Output level of the instrument.";
        case kVelVol: return "How much velocity affects volume.";
        case kTranspose: return "Transpose in semitones. The sample plays at original pitch on C3.";
        case kDetune: return "Fine tuning in cents.";
        case kPbRange: return "Pitch bend range in semitones.";
        case kGlideMode: return "Glide: monophonic legato slides. Portamento: every new note slides from the last one.";
        case kGlideTime: return "Glide / portamento time.";
        case kLoopFadePower: return "Loop crossfade shape: constant power (on) or linear.";
        case kTransHpOn:
            return "HP (off by default): a high-pass that follows the transposition. Its cutoff moves with Transpose, "
                   "Detune, pitch bend, the pitch envelope and the LFO's pitch (not with the key played), gliding a few "
                   "ms, so a sample transposed far up keeps its own low end (rumble, DC) below the audible range.";
        case kTransHpFreq:
            return "The high-pass's cutoff at 0 semitones, 10 to 200 Hz (20 by default): it doubles for each octave the "
                   "sample is transposed up (20 Hz at +48 is 320 Hz). Drag up/down, double-click to reset.";
        case kTransHpSlope:
            return "The high-pass's slope, as in Para: 6 and 18 dB are -3 dB at the cutoff, 12, 24, 36 and 48 dB "
                   "(Linkwitz-Riley) -6 dB. 24 dB by default.";
        default: break;
    }
    const uint32_t b = envAdsrBase (0);
    for (int e = 0; e < 3; ++e)
    {
        const uint32_t a = envAdsrBase (e);
        if (id == a)
            return "Attack: time to reach the peak. Drag the first point in the envelope display too.";
        if (id == a + 1)
            return "Decay: time from the last breakpoint (or the peak) to the sustain level.";
        if (id == a + 2)
            return "Sustain: level held while the key is down.";
        if (id == a + 3)
            return "Release: time to fade to zero after the key is released.";
    }
    (void)b;
    return nullptr;
}

constexpr const char* kScope = "The final output (after every effect): left bright, right dim, 0 dBFS in red. Click to change the time span.";

constexpr const char* kWaveform =
    "Waveform. Drag the orange flags to set the sample region. Classic: drag the white markers for Start / Length, "
    "click the green loop bar to switch looping on or off, drag it to move the loop, drag its edges to resize. "
    "Slicing: double-click to add or remove a slice, drag to move, Alt-click to toggle manual/auto. Click the "
    "waveform to audition. Scroll or drag the ruler to pan; Cmd/Alt + scroll or drag the ruler vertically to zoom. "
    "Drop an audio file here to load it, from a file browser or straight from a DAW (a clip dragged out of REAPER or "
    "Live; a temporary file is copied to Documents/bfielstr/Samples first). Right-click for the sample menu.";
constexpr const char* kEnvelope =
    "Envelope. Drag a point to move it. Double-click between the peak and the sustain point to add a breakpoint "
    "(up to 6); double-click a square breakpoint to remove it. Shift + drag a segment to bend its curve. Every point "
    "and curve is an automatable parameter.";
constexpr const char* kFilterDisplay =
    "Filter response. Drag left/right for cutoff and up/down for resonance (Shift for fine); the mouse wheel while "
    "dragging (or with Shift) also sets the resonance. Env shows the filter envelope.";
constexpr const char* kHelpButton = "Show or hide these help tooltips.";
constexpr const char* kLoad = "Load a sample (WAV, AIFF, FLAC or MP3). You can also drop a file on the waveform.";
constexpr const char* kPrevNext = "Load the previous / next audio file in the same folder.";
constexpr const char* kMenu = "Sample menu: normalize, reverse, crop, loop fade type, show the file, interface size.";
constexpr const char* kLfoHandle =
    "Drag this LFO onto any knob, slider or value of Smemplr (the effects' too) to modulate it: the control gets a "
    "ring (a line under it if it is not a knob) in the LFO's colour. Drag a knob's ring up or down to change the "
    "depth, right-click the ring to remove it. Switches and menus cannot be modulated. The host's value stays "
    "where it is: the LFO moves the sound around it.";
constexpr const char* kLfoScope = "The LFO's shape over one cycle, and where it is now.";
constexpr const char* kModList =
    "What the LFOs modulate, and how far (a share of the control's range, + or -). Drag a depth up or down to "
    "change it (Shift: fine), double-click it to turn it over; click x or right-click a row to remove it. A "
    "mapping onto an effect in the rack follows the effect when it moves, goes with it when it is removed, and "
    "pauses (dimmed) while another effect is in its slot. Up to 24 mappings, saved with the project.";
constexpr const char* kWarpAs = "Change the Warp As length: -/+ one beat, or halve / double it.";

} // namespace smemplr::help
