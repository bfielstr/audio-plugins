# Simplr

A VST3 sampler instrument modelled on Ableton Live's **Simpler** (Live manual §31.11), built on the
Steinberg VST3 SDK + VSTGUI. It's aimed at REAPER, and works in any VST3 host.

## Build & install (macOS)

```sh
git clone --depth 1 --recurse-submodules https://github.com/steinbergmedia/vst3sdk.git external/vst3sdk
scripts/build.sh            # build universal binary, run all tests, install to ~/Library/Audio/Plug-Ins/VST3
```

This works with only the Command Line Tools installed (no Xcode needed). In REAPER, rescan plug-ins
(Preferences → Plug-ins → VST → *Re-scan*), then insert **Simplr** on a track and drop a sample on it.

## What's implemented (mapped to the Simpler manual)

| Simpler | Simplr |
|---|---|
| **Classic** mode: start/end flags, Start / Length / Loop %, Loop on/off, loop crossfade (constant-power or linear), Snap to zero crossings, Gain, Voices (1–32) with subtle voice stealing, Retrig | ✓ |
| **One-Shot** mode: monophonic, Trigger / Gate, Fade In / Fade Out, Snap | ✓ |
| **Slicing** mode: Slice by Transient (Sensitivity, ≤64) / Beat (Division) / Region / Manual; Mono / Poly / Thru playback; Trigger / Gate; fades per slice or to region end; slices mapped chromatically from C1 (MIDI 36) | ✓ |
| Slice editing: double-click adds a slice (white) or removes one; drag to move; Alt-click toggles manual/auto | ✓ |
| **Warp** in every mode, locked to host tempo: Beats (Preserve, Transient Loop Mode, Envelope), Tones (Grain Size), Texture (Grain Size, Flux), Re-Pitch, Complex, Complex Pro (Formants, Envelope); "Warp as" with ÷2 / ×2 | ✓ (own algorithms, see below) |
| **Filter**: LP / HP / BP / Notch / Morph, 12/24 dB, circuits Clean / OSR / MS2 / SMP / PRD, Drive, Morph, Vel / Key / Env / LFO modulation, draggable response curve | ✓ |
| **Envelopes**: Amp / Filter / Pitch ADSR with draggable displays; amp loop modes None / Trigger / Loop / Beat / Sync with Time / Rate | ✓ |
| **LFO**: Sine / Square / Triangle / Saw Down / Saw Up / Random, Hz or tempo sync, Attack, Retrigger + Offset, Key, → Volume / Pitch / Pan / Filter, per voice | ✓ |
| **Global**: Pan, Random Pan, Spread (2 detuned voices L/R, decided at note-on), Volume, Vel→Vol, Transpose ±48, Detune ±50 ct, pitch bend (range adjustable, default ±5), Glide (mono legato) / Portamento (poly) + Time | ✓ |
| Context menu: Normalize, Reverse, Crop, constant-power fade toggle, Show in Finder | ✓ (all non-destructive; the file on disk is never changed) |
| Sustain pedal (CC64) | ✓ |
| Sample stays at its original pitch on C3 (MIDI 60) | ✓ |

Extras: audition by clicking the waveform (plays the slice under the mouse in Slicing mode),
◀ ▶ step through the samples in the current folder, zoom (Cmd/Alt + scroll, or drag the ruler
vertically) and pan (scroll, or drag the ruler horizontally), live playheads, resizable UI (drag
the window corner, or Menu → Interface Size).

## Differences from Live's Simpler

- **Warp algorithms are my own implementations**, not Ableton's (which are proprietary): Beats =
  transient-segmented playback, Tones = WSOLA-aligned grains, Texture = scattered grains, Complex =
  phase-locked phase vocoder, Complex Pro = the same plus cepstral formant correction. They're
  tempo-accurate and tested, but they won't sound identical to Live. Live's manual warp markers
  aren't supported, since there's no clip to take them from.
- The filter circuits are my models of the described behaviour (hard-clipped SVF, soft-clipped
  SVF, ladder variants), not Cytomic's code.
- Live-only features are left out: Slice to Drum Rack / New MIDI Track, Simpler → Sampler conversion.
- Samples are referenced by path in the project (like most samplers). If a file moves, Simplr
  shows "Missing" but keeps the reference so it isn't lost when you save.
- No MPE yet. Mod wheel is mapped but not routed anywhere.

## Layout

```
src/core     framework-free DSP: params, sample loading/analysis, slicing, filters, envelopes,
             LFO, warp engines, voices/engine        (dr_wav/dr_flac/dr_mp3 for decoding)
src/plugin   VST3 processor / controller / state; Bridge shares sample data between them
src/ui       VSTGUI editor, waveform view, displays, widgets
tests        core_tests.cpp (headless DSP tests) and host_test.mm (loads the built .vst3,
             plays MIDI, checks audio/state, clicks the real editor, writes screenshots)
```

The processor and controller share memory (the plug-in is registered as not distributable), which
is how every mainstream host, REAPER included, runs VST3 instruments.
