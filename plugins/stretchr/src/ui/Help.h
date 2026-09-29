#pragma once

#include "Params.h"

namespace stretchr::help {

inline const char* forParam (uint32_t id)
{
    switch (id)
    {
        case kAlgorithm:
            return "How the clip is stretched and shifted. Simple Windowed: fast overlap-add. Balanced: waveform-aligned, "
                   "a good all-rounder. Polyphonic: phase vocoder for chords and mixes. Soloist: for single voices and "
                   "monophonic instruments. Beats: slices at transients. Extreme: spectral smear for huge stretches. "
                   "Tape: varispeed, pitch follows speed.";
        case kPitch: return "Transposition in semitones (added to the pitch envelope).";
        case kFine: return "Fine transposition in cents.";
        case kFormant:
            return "Moves the formants (the vowel colour) up or down without changing the pitch. Polyphonic and Soloist.";
        case kPreserveFormants:
            return "Polyphonic: keep the formants in place when the pitch moves, so voices don't sound chipmunked. "
                   "Soloist always keeps them.";
        case kSpeed: return "Playback speed of the whole clip: 50 % is twice as long. Stretch markers work on top of this.";
        case kFollowTempo: return "Set the speed from the host tempo divided by Source Tempo, so the clip follows the song.";
        case kSourceBpm: return "The tempo the clip was recorded at (used by Follow Tempo).";
        case kWindow: return "Grain length of Simple Windowed, Balanced and Alien. Short for drums, long for sustained sounds.";
        case kTransients:
            return "Polyphonic frame size. Crisp keeps attacks sharp, Smooth favours sustained tones and bass.";
        case kSmear:
            return "Extreme: analysis window, longer is smoother and more washed out. Alien: how far the grains scatter in "
                   "time (a quarter of it either way).";
        case kStereo:
            return "Extreme and Alien. Wide: left and right are smeared (or scattered) apart, for a wide sound. Same: one "
                   "channel, duplicated.";
        case kTrigger:
            return "Play: the clip starts the moment the host starts playing, from wherever the playhead is. Timeline: it "
                   "plays where it sits on the timeline.";
        case kGain: return "Level of the rendered clip.";
        case kOutside:
            return "What happens to the track's audio outside the clip while the transport plays: pass it through or "
                   "mute it.";
        default: return nullptr;
    }
}

inline const char* algorithmSummary (int a)
{
    switch (a)
    {
        case kWindowed: return "Plain overlap-add. Fast and light; fine for drums\nand small changes, can warble on sustained tones.";
        case kBalanced: return "Overlap-add aligned by waveform similarity.\nA good all-rounder for mixed material.";
        case kPolyphonic: return "Phase-locked vocoder. Best for chords, pads and\nfull mixes; formant shift and preservation.";
        case kSoloist: return "Pitch-synchronous, for voices and monophonic\nlines. Keeps the formants in place.";
        case kBeats: return "Slices at transients. Keeps drums tight;\ntails loop when slowed down.";
        case kExtreme: return "Spectral smearing for huge stretches\n(down to 5 % speed): turns anything into a pad.";
        case kTape: return "Varispeed: pitch follows Speed, like a tape\nrunning fast or slow.";
        case kAlien: return "A granular cloud: scattered, sometimes reversed,\nslightly detuned grains. Alien textures.";
        default: return "";
    }
}

constexpr const char* kClipView =
    "Stretch mode: drag a marker to stretch the audio around it, double-click to add a marker, double-click a marker to "
    "remove it; the last marker sets the clip length. Pitch mode: double-click to add a pitch point (snaps to "
    "semitones, Shift for free), drag points, double-click one to remove it. Shift: fine. Wheel: scroll, "
    "Ctrl/Alt+wheel: zoom, double-click the ruler: zoom to fit. Right-click for more. Drop an audio file to load it at "
    "the playhead.";
constexpr const char* kCapture =
    "Record the track's audio into Stretchr: click, then start playback in the host; recording stops when the "
    "transport stops or loops (or click again). The clip is placed where it was recorded.";
constexpr const char* kLoad = "Load an audio file (WAV, AIFF, FLAC or MP3) as the clip, starting at the playhead.";
constexpr const char* kToPlayhead = "Move the clip so it starts at the host's playhead.";
constexpr const char* kClear = "Remove the clip (the track plays through untouched).";
constexpr const char* kStretchMode = "Edit stretch markers.";
constexpr const char* kPitchMode = "Edit the pitch envelope.";
constexpr const char* kUndo = "Undo the last clip edit.";
constexpr const char* kRedo = "Redo.";
constexpr const char* kExport = "Save the rendered clip as a 32-bit float WAV file.";
constexpr const char* kDragOut =
    "Drag this onto a track in REAPER or Live to drop the rendered clip there as audio (bounce in place). The file is "
    "kept in Music/Stretchr Renders.";

} // namespace stretchr::help
