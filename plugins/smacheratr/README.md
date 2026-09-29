# Smacheratr

A waveshaping saturator in the spirit of Ableton Live's **Saturator**: from a little warmth or dirt
to hard clipping and wavefolding. Install instructions are in the [top-level README](../../README.md).


## What it does

Every sample is mapped through the **Analog** curve (Live's Analog Clip): linear up to half scale,
then a smooth knee into clipping at 0 dB. **Drive** (-36 ... +36 dB, default 14 dB) sets how far the
signal reaches into it. The display shows the curve with the driven signal's current reach
highlighted, so you can see when the saturation starts. Drag it up or down to set Drive.

**Pre-Limit** (on by default) is a look-ahead limiter in front of the drive: the input is
held at its **threshold** (default -6 dB), and the drive is applied after it. A transient then cannot
push further into the curve than the rest of the sound, so it is not squared off; the display shows
the furthest the driven signal can go as blue lines.

**Post Clip** (No Clip / Soft Clip / Hard Clip) clips the output at 0 dB after the curve, so the
output never exceeds the **Output** level (-36 ... 0 dB). **Dry/Wet** blends in the dry signal; use
100 % on a return track.

**Color** (on by default) enables two filters that are applied before the curve and undone
(inverted) after it. They change how much of each frequency range is saturated without changing the
balance of the output: with **Amt Lo** at -100 % the bass is kept clean and its energy passes through,
while the mids and highs saturate. **Amt Hi** does the same around **Freq** with the bandwidth set by
**Width**. The colour display shows the pre-curve EQ; drag the left handle for Amt Lo, the right handle
up/down for Amt Hi or sideways for Freq.

**Clarity** (above the curve) keeps a hard-pushed drive from going muddy: when the low mids (around
320 Hz) hit the curve hard they are turned down before it (3 dB for every 5 dB over -18 dBFS, at
most 8 dB, so it does nothing at gentle settings), and the lows go into the curve 4 dB down and are
lifted back after it, so the bass drives the curve less (less intermodulation mud) but keeps its level.

**Menu**: **Hi-Quality** runs the curve 4x oversampled (two linear-phase half-band stages) to keep
aliasing down; **Pre-DC Filter** removes DC offset before the curve; **Mid/Side** saturates the mid
and the side apart, so the side is driven by its own, lower level and a wide sound stays wide when
you push the drive (Widr's built-in saturator always works this way).

Latency: about 1.7 ms (83 samples at 48 kHz: the pre-limiter's 1 ms look-ahead plus the
oversampling), the same whatever the settings, and reported to the host.

Smacheratr is also built into Multidyn (after its Output) and into Smempler.
