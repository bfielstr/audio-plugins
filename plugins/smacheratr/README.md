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
the furthest the driven signal can go as dashed lines.

**Post Clip** (No Clip / Soft Clip / Hard Clip) clips the output at 0 dB after the curve, so the
output never exceeds the **Output** level (-36 ... 0 dB). With **Soft Clip** and **Hard Clip** nothing
leaves above 0 dBFS at all (Soft only clips more gently on the way there): what comes after the curve's clip could rise over it again (Hi-Quality's downsampling
filter overshooting the clipped edges by up to 4 dB, Gentlr's bands, the dry part of a mix, Mid/Side
going back to left / right: up to 6 dB), so the very end, after Output, is held to 0 dBFS too; only
those overshoots are cut. **Dry/Wet** blends in the dry signal; use 100 % on a return track.

**Color** (on by default) enables two filters that are applied before the curve and undone
(inverted) after it. They change how much of each frequency range is saturated without changing the
balance of the output: with **Amt Lo** at -100 % the bass is kept clean and its energy passes through,
while the mids and highs saturate. **Amt Hi** does the same around **Freq** with the bandwidth set by
**Width**. The colour display shows the pre-curve EQ; drag the left handle for Amt Lo, the right handle
up/down for Amt Hi or sideways for Freq.

**Gentlr** (the panel at the bottom; it was called Clarity) keeps a hard-pushed drive from going
muddy or harsh: a compressor on a band (the low mids around 250 Hz by default; a second band can be
given a Range too) that turns the band down before the curve when it hits it hard (3 dB for every
5 dB over -18 dBFS, at most the band's **Range**, 8 dB by default, so it does nothing at gentle
settings) and after it by half as much. Set the band with **Freq** and **Width**, or in the colour
display: drag its handle sideways for the frequency and down for the Range, drag an edge (or hold
Alt / Option and drag the band sideways) for the width.

**Advanced** (in the Gentlr panel) gives each band a **Threshold** instead of the fixed -18 dB: a
vertical slider per band at the right edge of the colour display, with the band's level as Gentlr
measures it rising beside it (bright where it is over the threshold, which is where the band is
being cut). Over the threshold the law is the same: 3 dB of cut for every 5 dB over (2.5 : 1, hard
knee), reaching the Range (Range / 0.6) dB over it. Advanced also has the region **Drive** (and its
**Amount**, 0 ... 36 dB): the bands Gentlr works on are split out again with the same band filters,
put through the Analog curve on their own and added back, level-matched (the curve's output divided
by the gain), so the cut region gets density and harmonics while the rest of the sound stays clean.
It runs oversampled with the rest when Hi-Quality is on, fades in and out when switched, and does not
change the latency. With Advanced off, Gentlr is exactly the Clarity it was before.

**Sub** (a third Gentlr band) compresses the sub region: a shelf, flat from the very
bottom of the spectrum up to its **Freq** (20 to 100 Hz, 40 Hz by default), where its cut starts to let go
(nearly none two octaves up). A Gentlr band whose edge reaches an end of the spectrum (20 Hz or 20 kHz)
turns into a shelf there too, running flat past that end instead of dipping back up. It has no button of its own: like band 2 it works while Gentlr is on and its **Range**
is above 0 dB, and its Range starts at 0 dB (no cut). It has the same law as the other bands (3 dB of cut for every 5 dB over -18 dBFS,
or over its **Threshold** in Advanced) and the same region Drive. In the colour display drag its handle sideways
for the frequency and down for the Range (at Range 0 it sits flat at 0 dB, ready to pull down); it has no
width. At Range 0 Gentlr is exactly what it was.

**High** (a fourth Gentlr band) is the Sub band's mirror for the top of the spectrum
(harshness, fizz, sibilance): a shelf, flat from its **Freq** (2 to 16 kHz, 7 kHz by default) up to the
very top, its cut letting go below it (about half of it an octave down, nearly none two octaves down,
so at 7 kHz the presence region of 1 to 3 kHz is left alone). It is the complement of a critically
damped 12 dB/oct low-pass, so its cut is exactly the Range where it is flat. Like the Sub band it
has no button: it works while Gentlr is on and its **Range** is above 0 dB (0 dB to start with), with the
same law, its own **Threshold** in Advanced and the same region Drive; its handle in the colour display
has no width. At Range 0 Gentlr is exactly what it was, bit for bit. Projects saved while
the Sub and High bands had buttons sound the same: a band that was off loads at Range 0, one that was
on keeps its Range.

**Slope** (in the Gentlr panel, under the band buttons; one for both bands, the Sub and High bands keep
their own shape) sets the shape of Gentlr's two bands: **12 / 12** (the default: 12 dB/oct below the
band and 12 dB/oct above it; the band is symmetric, so at its centre the cut is exactly the Range),
**Signature** (24 dB/oct below, 12 dB/oct above: a steeper floor under the band, so a band on the low
mids leaves the bass under it alone) or **Classic** (12 dB/oct below, 6 dB/oct above: the only shape
before the Slope). Every band is scaled to peak at 0 dB, and a band that reaches an end of the spectrum
is still a shelf there, keeping the slope of its other side; the colour display draws the shape
selected. Projects saved before the Slope load with Classic and sound exactly as they did; the end
saturator in the other plug-ins has the same Slope (beside No Overlap, above its colour display).

**No Overlap** (in the Gentlr panel, off by default): Gentlr's working bands never cover the same
frequencies. Dragging or widening a band in the colour display pushes its neighbours' edges along (a
neighbour narrows, and once it is 0.5 octaves wide moves as a whole); where a neighbour cannot move
further (Sub at 20 Hz, High at 16 kHz) the dragged band stops at it. Switched on, bands that overlap
are split at the middle of the overlap. The engine keeps the bands apart the same way when automation
makes them overlap, and leaves bands that do not overlap exactly as they are.

**Menu**: **Hi-Quality** runs the curve 4x oversampled (two linear-phase half-band stages) to keep
aliasing down; **Pre-DC Filter** removes DC offset before the curve; **Mid/Side** saturates the mid
and the side apart, so the side is driven by its own, lower level and a wide sound stays wide when
you push the drive (Widr's built-in saturator always works this way).

Latency: about 1.7 ms (83 samples at 48 kHz: the pre-limiter's 1 ms look-ahead plus the
oversampling), the same whatever the settings, and reported to the host.

Smacheratr is also built into Multidyn (after its Output) and into Smemplr.
