# Smacheratr

A waveshaping saturator in the spirit of Ableton Live's **Saturator**: from a little warmth or dirt
to hard clipping and wavefolding. Install instructions are in the [top-level README](../../README.md).


## What it does

Every sample is mapped through a shaping curve; **Drive** (−36 … +36 dB) sets how far the signal
reaches into it. The display shows the curve with the driven signal's current reach highlighted, so
you can see when the saturation starts. Drag it up or down to set Drive.

**Curve types**

| Curve | Character |
|---|---|
| Analog Clip | Linear below the clipping point, a smooth knee around it |
| Soft Sine | Sine-shaped saturation, gentle low-order harmonics |
| Bass Shaper | Analog Clip with an adjustable **Bass Shaper Threshold** (0 … −50 dB): linear below it, a smooth tanh above it. Low thresholds are soft, 0 dB is a hard clip. Made for 808s and synth bass |
| Medium Curve | tanh saturation |
| Hard Curve | Saturates later and harder |
| Sinoid Fold | Wavefolding: past the clipping point the output folds back over itself |
| Digital Clip | Immediate hard clipping |
| Waveshaper | Shaped by its own six controls (see below) |

**Post Clip** (Off / Soft Clip / Hard Clip) clips the output at 0 dB after the shaper (Soft uses the
Analog Clip curve), so the output never exceeds the **Output** level (−36 … 0 dB). **Dry/Wet** blends
in the dry signal; use 100 % on a return track.

**Color** enables two filters that are applied before the shaper and undone (inverted) after it.
They change how much of each frequency range is saturated without changing the balance of the
output: with **Amt Lo** at −24 dB the bass is kept clean and its energy passes through, while the mids
and highs saturate. **Amt Hi** does the same around **Freq** with the bandwidth set by **Width**. The
colour display shows the pre-shaper curve; drag the left handle for Amt Lo, the right handle up/down
for Amt Hi or sideways for Freq.

**Waveshaper controls** (active with the Waveshaper curve): **Drive** blends the shaped curve with a
plain clip (at 0 % the other controls do nothing), **Curve** adds mostly third-order harmonics,
**Linear** sets the size of the linear region, **Depth** superimposes a sine wave on the curve with
**Period** setting the density of its ripples, and **Damp** flattens the curve around zero like an
ultra-fast gate.

**Menu**: **Hi-Quality** runs the shaper 4x oversampled (two linear-phase half-band stages) to keep
aliasing down, at a little more CPU; **Pre-DC Filter** removes DC offset before the shaper.

Latency: under a millisecond (35 samples at 48 kHz), the same whether Hi-Quality is on or off, and
reported to the host for automatic compensation.
