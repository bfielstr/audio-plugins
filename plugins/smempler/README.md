# Smempler

A VST3 sampler instrument modelled on Ableton Live's **Simpler** (Live manual §31.11), built on the
Steinberg VST3 SDK + VSTGUI. It's aimed at REAPER, and works in any VST3 host on macOS, Windows and Linux. Install instructions are in the
[top-level README](../../README.md).

![Smempler in Slicing mode](../../docs/smempler/ui_slicing.png)

## What's implemented (mapped to the Simpler manual)

| Simpler | Smempler |
|---|---|
| **Classic** mode: start/end flags, Start / Length / Loop %, Loop on/off, loop crossfade (constant-power or linear), Snap to zero crossings, Gain, Voices (1–32) with subtle voice stealing, Retrig | ✓ (Loop is on by default and begins at Start; Loop % sets how far it runs) |
| **One-Shot** mode: monophonic, Trigger / Gate, Fade In / Fade Out, Snap | ✓ |
| **Slicing** mode: Slice by Transient (Sensitivity, ≤64) / Beat (Division) / Region / Manual; Mono / Poly / Thru playback; Trigger / Gate; fades per slice or to region end; slices mapped chromatically from C1 (MIDI 36) | ✓ |
| Slice editing: double-click adds a slice (white) or removes one; drag to move; Alt-click toggles manual/auto | ✓ |
| **Warp** in every mode, locked to host tempo: Beats (Preserve, Transient Loop Mode, Envelope), Tones (Grain Size), Texture (Grain Size, Flux), Re-Pitch, Complex, Complex Pro (Formants, Envelope); "Warp as" with ÷2 / ×2 | ✓ (own algorithms, see below) |
| **Filter**: LP / HP / BP / Notch / Morph, 12/24 dB, circuits Clean / OSR / MS2 / SMP / PRD, Drive, Morph, Vel / Key / Env / LFO modulation, draggable response curve | ✓ |
| **Envelopes**: Amp / Filter / Pitch ADSR with draggable displays; amp loop modes None / Trigger / Loop / Beat / Sync with Time / Rate | ✓ plus up to 6 extra breakpoints (double-click) and per-segment curves (Shift+drag), all automatable |
| **LFO**: Sine / Square / Triangle / Saw Down / Saw Up / Random, Hz or tempo sync, Attack, Retrigger + Offset, Key, → Volume / Pitch / Pan / Filter, per voice | ✓ |
| **Global**: Pan, Random Pan, Spread (2 detuned voices L/R, decided at note-on), Volume, Vel→Vol, Transpose ±48, Detune ±50 ct, pitch bend (range adjustable, default ±5), Glide (mono legato) / Portamento (poly) + Time | ✓ |
| Context menu: Normalize, Reverse, Crop, constant-power fade toggle, Show in Finder | ✓ (all non-destructive; the file on disk is never changed) |
| Sustain pedal (CC64) | ✓ |
| Sample stays at its original pitch on C3 (MIDI 60) | ✓ |

**After the sampler** (tabs at the bottom, in chain order), each with its own display:

- **Para**: the parallel high-pass / low-pass, tracking the notes played here around the sampler's
  **Root Note**, with its own Transpose (+48 by default, four octaves above the played note) added to the
  sampler's Transpose, or on its own with **Lock** on, and the sampler's pitch bend range.
  Its display shows the live spectrum, so you can see it working.
- **Multidyn**: the multiband dynamics, with its lanes and band fields.
- **M/S EQ**: a high-pass on the side signal (6 / 12 / **24** dB per octave, default 150 Hz) tapers
  the sides so the low end is mono below the cutoff, plus side and mid levels; live mid and side
  meters.
- **Smacheratr**: the Analog curve at the very end, with its optional pre-limiter (off, Drive 0 dB).

All are off by default; every control is an automatable parameter and the latency they add is
reported to the host. The **OUTPUT** scope on the right shows the final output (click it to change
the time span), so you can see what comes out, including squared-off peaks.

**Root Note** (Global panel): the note on which the sample plays at its own pitch (C3 by default).
**Voices** defaults to 1.

Extras: hover tooltips for every control (**?** toggles them), a clickable/draggable loop bar in
the waveform, audition by clicking the waveform (plays the slice under the mouse in Slicing mode),
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
- Samples are referenced by path in the project (like most samplers). If a file moves, Smempler
  shows "Missing" but keeps the reference so it isn't lost when you save.
- No MPE yet. Mod wheel is mapped but not routed anywhere.

## License

Smempler is released under the [MIT License](LICENSE). It builds on the Steinberg VST 3 SDK (MIT),
VSTGUI (BSD 3-clause) and dr_libs (public domain / MIT-0); see [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
