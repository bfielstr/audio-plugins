# Simplr

A VST3 sampler instrument modelled on Ableton Live's **Simpler** (Live manual §31.11), built on the
Steinberg VST3 SDK + VSTGUI. It's aimed at REAPER, and works in any VST3 host on macOS, Windows and Linux.

![Simplr in Slicing mode](docs/ui_slicing.png)

## Install

**macOS** (universal: Apple Silicon + Intel) **and Linux** (x86_64):

```sh
curl -fsSL https://raw.githubusercontent.com/bfielstr/simplr/main/scripts/install.sh | sh
```

**Windows** (x64), in PowerShell. Run it as Administrator to install into
`C:\Program Files\Common Files\VST3`; otherwise it installs for your user only:

```powershell
irm https://raw.githubusercontent.com/bfielstr/simplr/main/scripts/install.ps1 | iex
```

The installers download the latest [release](https://github.com/bfielstr/simplr/releases),
verify its SHA-256 checksum and copy `Simplr.vst3` into the standard VST3 folder
(`~/Library/Audio/Plug-Ins/VST3`, `~/.vst3`, or `Common Files\VST3` / `%LOCALAPPDATA%\Programs\Common\VST3`).
Set `SIMPLR_VERSION=v0.1.0` to pick a release or `SIMPLR_DEST=...` to choose the folder. You can
also download the zip for your platform from the releases page and copy `Simplr.vst3` yourself.

Then in REAPER: *Options → Preferences → Plug-ins → VST → Re-scan*, insert **Simplr** on a track
and drop a sample on it.

## Build from source

```sh
git clone https://github.com/bfielstr/simplr.git && cd simplr
cmake -B build -DCMAKE_BUILD_TYPE=Release  # first run downloads the VST3 SDK into external/
cmake --build build --config Release       # also runs Steinberg's VST3 validator
ctest --test-dir build -C Release          # DSP tests (+ plug-in host test on macOS)
```

- **macOS**: Command Line Tools are enough (no Xcode needed). `scripts/build.sh` does all of the
  above and installs the result.
- **Linux**: needs `libx11-dev libx11-xcb-dev libxcb-util-dev libxcb-cursor-dev libxcb-keysyms1-dev
  libxcb-xkb-dev libxkbcommon-dev libxkbcommon-x11-dev libfontconfig1-dev libcairo2-dev
  libfreetype6-dev libpango1.0-dev libgtkmm-3.0-dev` (Debian/Ubuntu names; gtkmm is only for the
  SDK's test hosts).
- **Windows**: Visual Studio 2022 with the C++ workload; use `cmake -B build -A x64`.

Every push is built and tested on all three platforms by GitHub Actions; pushing a `v*` tag
publishes a release and then runs the installers against it.

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
