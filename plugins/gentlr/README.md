# Gentlr

Smacheratr's **Gentlr** (it was called Clarity) on its own: a gentle dynamic de-muddier and
de-harsher. Two bands, a Sub band and a High band watch their part of the spectrum and turn it down only while it
gets loud, so a mix, a bus or a synth keeps its body at normal levels and stops going boomy, muddy or
harsh when it is pushed. Nothing is cut while a band stays under its threshold. Install instructions
are in the [top-level README](../../README.md).

The chain: **Input** -> **band 1** -> **band 2** -> **Sub** -> **High** (each turned down on its own when it is
loud; with Advanced, the region they cut can be driven) -> **Mix** -> **Output** -> **Smacheratr**.

## The bands

Each band is a region of the spectrum (a 12 dB/oct high-pass below it, a 6 dB/oct low-pass above it,
around its frequency, scaled so it peaks at 0 dB: Smacheratr's band) with a compressor on it. When the
band's level (its peak level, as a sine's peak) goes over -18 dBFS, the band is turned down by 3 dB for
every 5 dB over (2.5 : 1, hard knee), at most by its **Range**, which it reaches (Range / 0.6) dB over
the threshold. The band is taken out of the signal and put back turned down (x + (g - 1) * band), so a
band that is not cutting leaves the signal exactly as it was, bit for bit.

- **Band 1**: the low mids, the mud: 250 Hz, 2 octaves wide, Range 8 dB.
- **Band 2**: the upper mids, the harshness: 3 kHz, 2 octaves wide, Range 6 dB.
- **On**, **Freq** (20 Hz to 20 kHz), **Width** (0.5 to 4 octaves between the band's edges) and
  **Range** (0 to 24 dB; at 0 dB the band does nothing) for each. The band's name shows the edges of
  its region now. A band whose edge reaches an end of the spectrum (its low edge at 20 Hz or below, or
  its high edge at 20 kHz or above) turns into a shelf there: it runs flat past that end instead of
  dipping back up.
- **Sub**: a shelf for the sub region, flat from the very bottom of the spectrum up to its
  **Freq** (20 to 100 Hz, 40 Hz by default), where its cut starts to let go (within about 1 dB of the
  full cut there, about half of it at twice that, nearly none two octaves up). It has a **Range** and the
  same law as the other bands, and no width. It has no On button: it is always there, and works while
  its Range is above 0 dB. Its Range starts at 0 dB, so it cuts nothing until you pull its handle down
  in the display (or turn its Range up).
- **High**: the Sub band's mirror, a shelf for the top of the spectrum
  (harshness, fizz, sibilance), flat from its **Freq** (2 to 16 kHz, 7 kHz by default) up to the very
  top, its cut letting go below it (within about 1 dB of the full cut at its Freq, about half of it an
  octave down, nearly none two octaves down). 7 kHz puts the whole cut on the fizz and sibilance, half
  of it around 3.5 kHz where harshness starts, and leaves the presence region (1 to 3 kHz) that
  carries a voice or a lead alone. It has a **Range** and a Threshold, the same law, and no width. Like
  the Sub band it has no On button: it starts at Range 0 dB (no cut) and works once its Range is above
  0 dB; at 0 dB Gentlr is exactly what it was, bit for bit.
- Projects saved before the Sub and High bands lost their On buttons sound the same: a band that was
  off loads with its Range at 0 dB, one that was on keeps its Range.

**No Overlap** (off by default): the bands never cover the same frequencies. Dragging or widening a
band in the display pushes its neighbours' edges along: a neighbour gets narrower, and once it is as
narrow as a band can be (0.5 octaves) it moves as a whole; where a neighbour cannot move further (the
Sub band at 20 Hz, the High band at 16 kHz) the dragged band stops at it. Within one drag the push is
measured from where the bands were when it began, so dragging back lets them go back. Switched on,
bands that already overlap are split at the middle of the overlap (on a log axis), each giving up half.
Only working bands take part. A band covers the octaves between its edges, the Sub band everything
below its Freq, the High band everything above its Freq. Automation (or a band starting to work) that
makes bands overlap is kept apart the same way in the engine, so they never overlap in the sound
either; bands that do not overlap are left exactly where they are.

The bands work one after the other (band 1, band 2, Sub, High, as in Smacheratr), each measuring its own
band. **Attack** (0.5 to 100 ms, 15 ms) and **Release** (20 ms to 2 s, 150 ms) set how fast a band's
cut follows its level going up and lets go after it (Smacheratr's times by default).

**Stereo**: **Stereo** (left and right share one detector per band, so the image stays put),
**Mid/Side** (the mid and the side are worked on apart, each with its own detectors), **Mid** or
**Side** (only that one; the other passes). A change of mode fades Gentlr out and back in (5 ms each
way), so it does not click.

## Advanced

**Advanced** gives each band its own **Threshold** instead of the fixed -18 dB: a vertical slider per
band (band 1, band 2, Sub, High) at the right edge of the display, with the band's level as Gentlr measures
it rising beside it, bright where it is over the threshold (there the band is being cut). The law
over the threshold stays the same. Drag a slider (Shift: fine); a double-click or right-click puts it
back to -18 dB. Advanced is on in a new Gentlr (the Thresholds at -18 dB sound the same as Advanced
off, so the sliders are there to move); with Advanced off, the Thresholds are kept but not used.

Advanced also has the region **Drive** (and its **Amount**, 0 to 36 dB, 12 dB by default): the bands
as they leave, after their cuts, go through Smacheratr's Analog curve on their own, level-matched
(the curve's output divided by the gain) and added back, so the region Gentlr works on gets denser
and gains harmonics without getting louder, while the rest of the sound stays clean. It runs 4x
oversampled, fades in and out when switched, and a quiet region passes it unchanged.

## The display

The display shows each band as a multiband compressor shows its bands: its region shaded, the most
it can cut outlined (dashed), the cut it is making now lit (cinnabar) from the 0 dB line and moving with
the audio, a handle at its centre (Sub, High: at their Freq) as deep as its Range, and the whole response as
the bright line (every band at its cut now, with its phase: the curve is what the sound gets). Behind them, the
output's spectrum (filled) and the input's (dotted), tilted 4.5 dB/oct so a mix reads level: where
the input stands above the output, Gentlr is cutting. The readouts at the top show each band's
frequency and its cut now.

- Drag a handle sideways for the band's frequency (Sub: 20 to 100 Hz, High: 2 to 16 kHz), down for its Range.
  The Sub and High handles sit flat at 0 dB until you pull them down; then the band starts cutting.
- Drag a band's edge, or hold Alt / Option and drag the band sideways, for its width (the band stays
  centred); the mouse wheel on a handle (while you hold it, or with Shift) too. The Sub and High
  bands have no width.
- With No Overlap on, a band you drag or widen pushes its neighbours along (see above).
- Double-click or right-click a handle to reset the band (its frequency, width and Range).
- Click a band's readout at the top to switch the band on or off.

**No Overlap** (above Mix and Output), **Mix** (dry / wet: the input, delayed to line up, against
Gentlr's output) and **Output** (+-24 dB, before the Smacheratr at the end).

**Smacheratr** (bottom panel): the optional saturator at the end of the chain (off, Drive 0 dB), with
all its controls, its own Gentlr included.

## Latency

The region Drive's 4x oversampler delays the signal a little (37 samples at 48 kHz); its delay is
always in the path, with the dry signal delayed to match, so the latency never changes with the
settings. The end saturator adds its own (about 1.7 ms), also always in the path. Both are reported
to the host for automatic compensation.
