# Multidyn

A multiband dynamics processor modelled on Ableton Live's **Multiband Dynamics** (Live manual §29.26):
upward and downward compression *and* expansion on **1 to 4** independent frequency bands, each
with an upper (Above) and lower (Below) threshold. Two styles: **OTT**, a measured model of Xfer's
OTT, and **Character**, Multidyn's own smoother sound. Install instructions are in the [top-level README](../../README.md).

![Multidyn](../../docs/multidyn/ui_multidyn.png)

## Style: OTT or Character

**Style** (top bar, OTT by default) picks how each band's dynamics work.

**OTT** is a model of Xfer Records' free OTT (measured from version 1.3.1), the plug-in that Live's
"OTT" preset became: fast and grainy like it. Its constants and laws are David Braun's fit to
measurements of the OTT binary, published as `co.xfer_ott` in Faust's `compressors.lib`
(MIT licence, https://github.com/grame-cncm/faustlibraries/pull/257); its static curves match the
plug-in to about 0.03 dB, program material to about 0.1 dB (median). Each band follows its stereo
mean square with a one-pole envelope (OTT's fixed attack, a release that follows Time), and OTT's gain
law: upward compression at about 4 : 1 below its knee up to a cap of about 36 dB, an infinite ratio
above its downward knee, soft knees, OTT's makeup gain and a gain floor. Amount is OTT's Depth and
Time is OTT's Time. The lowest band plays OTT's low band, the top band its high band, any between its
mid band (one band alone: the mid band). At the defaults it is OTT; the band controls move it from
there:

- a band's **Below / Above threshold** moves OTT's upward / downward knee by as much as it moved from
  its default;
- the **Below / Above ratio** scales that branch's strength by (1 - 1/r) against the default ratio's
  (1 : 1 turns the branch off; under 1 : 1 the Above branch expands, and the boost over OTT's makeup
  stops at the upward branch's cap, about 36 dB);
- **Attack / Release** scale OTT's times by the same factor they moved from their defaults;
- the band **Output** trims after OTT's makeup (the baked preset gains are Character's).

**Peak/RMS**, the **RMS Window**, **Soft Knee**, **Soften** and the transient guard are Character's and
do nothing in OTT style (their controls look disabled); Soften's **Color**, Pre-Limit, the side-chain,
the crossovers (24 dB: OTT's Linkwitz-Riley 4), the Sub band and the Smacheratr work in both. The
display's block numbers show OTT's gain over its makeup.

**Character** is Multidyn's own sound, described below. Projects saved before Style existed (and
Smemplr rack slots saved before it) open in Character, so they sound as they did.

Not affiliated with or endorsed by Xfer Records.

## How it works

Ratios are written the way Live writes them, **1 : x**. x > 1 always *compresses* (shrinks the
dynamic range), x < 1 expands, and **1 : inf** limits:

| Block | 1 : x with x > 1 | 1 : x with x < 1 |
|---|---|---|
| **Above** the upper threshold | downward compression / limiting (loud gets quieter) | upward expansion (loud gets louder) |
| **Below** the lower threshold | upward compression (quiet gets louder, up to +36 dB) | downward expansion (quiet gets quieter) |

Each block has its own level envelope that feeds the static curve: Above uses Attack when the level
rises and Release when it falls, Below the other way round (as described in Live's manual). Attack
and Release are the time to *reach* the new amount of compression (about 95 % of the change), and
because the envelope works on the level, how fast the gain moves also depends on how far the signal
is past the threshold. Upward compression never lifts a signal past the Below threshold, so hits
after silence don't jump.

**Default settings** are OTT style with Live's Multiband Dynamics **"OTT"** preset (the sound Xfer's OTT gets
close to): 3 bands split at 88.3 Hz and 2.5 kHz (8 kHz for a fourth), Below -40.8 / -41.8 / -40.8 dB
and Above -33.8 / -30.2 / -35.5 dB, 1 : 4.17 below and 1 : 66.7 above on every band, OTT's
attack/release times (47.8 / 22.4 / 13.5 ms, 282 / 282 / 132 ms), Amount and Time 100 %, Soft Knee
and RMS on, 24 dB/oct crossovers. A fourth band starts like the top band of three. The preset's
gain staging (band outputs +10.3 / +5.7 / +10.3 dB, a fourth band +10.3 dB) is **baked into the
processing**, so every Input, Output and band gain control reads 0 dB at the default and trims
around the preset. Use Amount to dial the processing back.

Up to version 0.6 the defaults were an "OTT pushed further", with that preset's gain staging baked
in (input +5.2 dB, band outputs +24 / +9.1 / +11.3 / +11.7 dB, output -7 dB). A project saved before
loads with the difference moved into its gain controls (a band 1 Output of 0 dB becomes +13.7 dB,
the Output -7 dB, every band Input +5.2 dB, and so on), so it sounds the same; only a trim that would
now go past a control's range (e.g. a band 1 Output above +10.3 dB) stops at its end. The new
settings below start where an old project was: 24 dB crossovers, Color and the Sub band off.

**Character** style: detection is smooth (the RMS Window, 50 ms by default, and a rounded onset), the knee
is wide (12 dB) and the release slows down up to 3x the deeper the gain change, so it moves like a
character compressor rather than grabbing peaks.

**Smacheratr** (bottom panel, end of the chain): the Analog curve after the Output gain, with its
optional pre-limiter, Drive, Post Clip and Dry/Wet. Off by default, Drive 0 dB; its latency is
constant whether it is on or off.

**Soften** (top band only): as the top band's Below threshold closes in on its Above threshold, the
part of the signal that upward compression lifts is low-passed and turned down and the gain changes
are rounded off, so a squashed top band stops sounding noisy and grainy. **Color** (under Soften, off
by default) adds Smacheratr's high colour after the Output gain: a peak at 5 kHz (Width 4) pushed into
the Analog curve and taken back down after it, at Drive 0 dB in Hi-Quality, so the loud highs upward
compression brings up come out rounder. Its amount follows Soften: 15 % at 0, 35 % at 100 %. Its
latency is always part of Multidyn's: off, the signal only goes through a delay of the same length
(the output is the plain signal, to the bit); switched on or off it crossfades (20 ms), so it never
clicks and the latency never changes.

**Pre-Limit** (off by default): a 1 ms look-ahead limiter on each band's input (after the band's
Input gain) with its **Ceiling** relative to the band's Above threshold (0 dB = right at it). When you
push hard into the thresholds, a transient would otherwise pass through at full level until the
attack catches up and then be squared by whatever follows (a saturator); the pre-limiter holds it
where the compressor settles anyway, so it reaches the saturator at the same level as the body and
gets the same rounding. Raise the Ceiling to let more of the transient through. Every band
always runs through the 1 ms look-ahead, and the saturator's and Color's oversampling add their own,
so the latency (218 samples at 48 kHz) never changes; it is reported to the host.

**Slope**: the crossovers' steepness, **6, 12, 18, 24, 36, 48, 60, 72, 84 or 96 dB/oct** or
**Brickwall**; 24 dB by default (Linkwitz-Riley 4, what Multidyn always had, so old projects keep it).
12 and 24 .. 96 dB are Linkwitz-Riley 2 .. 16 crossovers, 18 dB a third-order Butterworth, 6 dB a
first-order split whose two sides add up to the input itself, and Brickwall a Linkwitz-Riley 32
(192 dB/oct: a third of an octave from the crossover a band is down more than 60 dB). At every slope
the bands are phase-aligned so they sum back flat (within 0.1 dB, for any band count) when nothing
is processed; the steeper the slope, the more the phase turns around the crossovers (6 dB turns
none). None of them adds latency. A new slope starts a second set of crossovers beside the running
one, lets it settle and crossfades the bands to it, so changing it while playing does not click.

**Sub band** (off by default): an extra band below band 1 for the sub region. The input is split at
the **Sub** frequency (20 - 100 Hz, 40 Hz by default: the Sub band goes down to the 20 Hz end and
tapers off above it, with the crossovers' Slope) before the other bands, which get the rest (so band 1
no longer gets the sub region), and everything still sums back flat. The Sub band compresses
downward only: its **Threshold** (-18 dB) and **Ratio** (1 : 4) work like a band's Above threshold and
ratio, with its own **Attack** (30 ms, long enough for the sub's slow cycles), **Release** (200 ms),
**Input** gain (0 dB: before the compression, so it drives the Threshold harder, as a band's Input) and
**Output** gain (0 dB, nothing baked in); the detector, Soft Knee, Amount, Time, the side-chain and
Pre-Limit work on it as on the bands. It has no Below block (nothing is lifted in the sub region) and
no Solo (soloing a band mutes it). Off, none of it runs and the sound is exactly what it is without
it (to the bit); switched on, it settles for 100 ms beside the bands, then fades in over 30 ms (the
region around its frequency dips for those 30 ms, as the two phases are blended), and fades out the
same way; the latency stays the same.

## Controls

The layout follows Live's device: the **Split** column on the left holds the band names, On
(bypasses the band's dynamics and gains) and Solo, with the crossover frequency fields between the
lanes; then the per-band **Input** knobs, the lanes, the per-band **Output** knobs and the global
controls on the right.

- **Bands** (top): 1, 2, 3 or 4 bands; 1 makes Multidyn a single full-range processor.
- **Style** (top): OTT or Character (see above). In OTT style Soft Knee, Peak/RMS, the RMS Window
  and Soften look disabled: they only work in Character.
- **Value fields** beside each lane: Below threshold and ratio (left), Above threshold and ratio and
  Att/Rel (right). Drag a field up/down (Shift: fine), double-click to reset.
- **Display**: thin bars = input level, thick bars = output level. Drag a block edge to move a
  threshold; drag inside a block up (louder) or down (quieter) to set its ratio. **Cmd/Ctrl** = all
  bands, **Alt/Option** = Above and Below together, **Shift** = fine, **double-click** = 1:1. The
  number in a block is the gain it applies at its extreme (silence for Below, 0 dB for Above; in OTT
  style OTT's gain there, over its makeup).
- **Global**: Output, Time (scales all attack/release times), Amount (0% = every ratio acts as
  1:1), Soften and its **Color** on the right; Soft Knee, Peak/RMS detection and the RMS Window
  under the display, with the crossovers' **Slope** (a menu) and the **Sub** band's On and
  frequency in the row below; Pre-Limit and its Ceiling.
- **Sub band**: when it is on, the display gets a lane for it at the bottom, with its Above block
  (drag its edge for the Threshold, inside it for the Ratio, double-click for 1:1), its Threshold,
  Ratio, Attack and Release fields beside it, and its Input and Output as fields in the Input and Output
  columns.
- **Side-chain**: route another track into inputs 3/4 in REAPER; On, Gain, Dry/Wet (detector blend)
  and Listen; the status line under the Slope row says whether a signal is routed.

Every control is an automatable parameter; point at any control for help in the info box at the bottom (and in a tooltip: **?** toggles the tooltips).
