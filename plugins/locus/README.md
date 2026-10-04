# Locus

A low-end contrast processor in the spirit of iZotope Ozone's **Low End Focus**. Install
instructions are in the [top-level README](../../README.md).

![Locus](../../docs/locus/ui_locus.png)

## What it does

Locus splits the low end into dozens of narrow bands (~12 Hz each) and compares every band's
level with its *neighbourhood*, the level of nearby bands over the recent past, instead of
against an absolute threshold like a compressor does.

- **Positive Contrast**: bands weaker than their surroundings are pushed down, including the mud
  between the harmonics of a bass note, so the dominant events come into focus (clarity, punch).
- **Negative Contrast**: everything moves closer in level (weight, density, sustain).
- The loudness of the range is kept steady, so Contrast changes focus, not level. **Gain** sets
  the level of the range.
- **Punchy** mode uses fast time constants (transients, impact); **Smooth** uses slow ones
  (sustain, weight).
- **Low / High** set the range (default 30–300 Hz). Everything outside it passes through
  untouched (bit-exact apart from the latency).
- **Solo** plays only the range.

The display shows the input spectrum (a faint copper body), the output (a bright line) and the gain applied
per band.
Drag the range edges to move them, drag inside the range sideways to move it or up/down to set
Contrast, and double-click to reset Contrast.

**Smacheratr** (bottom panel): the optional saturator at the end of the chain (off, Drive 0 dB).

Latency: about 87 ms (4096 samples at 48 kHz for the analysis plus the saturator's 1.7 ms), reported
to the host for automatic compensation.
