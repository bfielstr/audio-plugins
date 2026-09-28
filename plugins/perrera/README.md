# Perrera

A high-pass and a low-pass filter **in parallel** that follow the notes you play. Install
instructions are in the [top-level README](../../README.md).

## What it does

The two filters run side by side and their outputs are summed. With the high-pass above the
low-pass (the defaults: 800 Hz over 200 Hz) that leaves a notch between them; with the two meeting
(resonance 0) the sum is flat, so nothing happens; pushing them further apart widens the notch.

- **HP Freq / HP Res**, **LP Freq / LP Res**: each filter's cutoff (at the root note) and
  resonance; **Slope** is 12 or 24 dB per octave for both.
- **Split**: moves the filters apart (positive) or together (negative) in semitones around their
  set frequencies. Automate it, or use the envelope.
- **Env Amt / Attack / Decay**: an envelope triggered by every MIDI note adds its amount to Split
  (negative amounts pull the filters together on each note).
- **Key**, **Transpose**, **Bend**, **Root**: both cutoffs track the played note. At 100 % Key
  the filters sit on the same harmonics of every note; the root note (60 = C3 by default) is where
  the cutoffs sit at their set frequencies, and Transpose and the pitch bend range are applied to
  the played note before tracking. Route MIDI to the plug-in (in REAPER: a MIDI item or track sent
  to the FX's MIDI input).
- **Dry/Wet**, **Output**.

The display shows the high-pass (red), the low-pass (green) and what you hear (white). Drag a
handle sideways for its cutoff and up/down for resonance; double-click resets it. When a note is
tracked the curves show where the filters actually are.

Perrera is also built into Smempler, where it tracks the sampler's own notes.
