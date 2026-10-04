# Dropr

Dropr slams a sound flat in six bands and drives the result into a saturator. Everything over a very
low threshold is pulled to one level, then made up and pushed into Smacheratr, so quiet tails and
room come right up and the whole sound gets dense and heavy. With negative ratios, louder input comes
out quieter: on a snare the snap is clamped down below the body, and the body comes up, heavily
saturated. Reach for it for aggressive drums, crushed parallel buses and sound design. Install
instructions are in the [top-level README](../../README.md).

![Dropr](../../docs/dropr/ui_dropr.png)

## How to use it

1. Put Dropr on a drum or a bus. The defaults are already the crushed, saturated sound.
2. Blend it back with **Dry/Wet**, or use it on a parallel bus.
3. Shape the result in the display: drag a band's gain point up or down, the threshold lines up or
   down, the crossovers sideways. **Release** and **Adaptive** set how it breathes.

Point at any control for help in the info box at the bottom (**?** also switches on hover tooltips).

## The chain

**Input** gain → six bands (Linkwitz-Riley crossovers) → per band: detector → downward and upward
compression → attack / release → the band's gain + **Tilt** + **Makeup** → the bands summed →
**Dry/Wet** → **Output** → **Smacheratr** (on and driven by default).

## Defaults

- **Input** +30 dB, **Bands** 6, with crossovers at 70 Hz, 800 Hz, 2.15 kHz, 4.5 kHz and 11 kHz.
- **Down Thr** -72 dB, **Negative** on with **Neg Ratio** at **1 : -inf** (**Range** 12 dB), **Attack**
  6 ms, **Release** 350 ms, **Adaptive** 50 %, **Knee** 0 dB.
- Upward compression off (**Up Ratio** 1 : 1, **Up Thr** -48 dB), **Link** 100 %, **Mode** **Stereo**.
- Every band's gain +12 dB, **Tilt** 0, **Makeup** +48 dB, **Dry/Wet** 100 %, **Output** 0 dB.
- **Smacheratr** on: Drive +18 dB, Hard Clip (which holds the very end to 0 dBFS), Pre-Limit off.

With these, everything over -102 dBFS at the input is brought to one level (the floor, -84 dB after the
Input gain), +60 dB of gain brings it to about -24 dB per band, and the saturator does the rest. On the
tests' snare-like hit the transient comes out about 17 dB closer to the body than it went in, peaking
at 0 dBFS. The threshold is so low that very quiet material (below -102 dBFS) is not compressed at all
and gets the whole +90 dB of gain: digital silence stays silent, but a noise floor around -110 dBFS
comes up to about -20 dBFS.

## The display

Frequency across (20 Hz to 20 kHz), dB up (+36 dB to -96 dB).

- **Crossover handles**: the vertical lines between the bands. Drag one sideways (each stays at least
  a sixth of an octave from its neighbours).
- **Gain points**: one per band at its centre, with a smooth curve through them. The height is the
  band's gain plus Tilt (Makeup comes on top). Drag a point up or down.
- **Thresholds**: the downward threshold (a solid line) and the upward one (dashed, dim while its ratio
  is 1 : 1) as horizontal lines; drag them up or down. With Negative on, the floor is drawn dotted.
- **Meters**, per band: its level after the Input gain (dim), its gain reduction hanging from 0 dB (an
  outlined bar, with the number at the top) and its level after its gain (lit).

Right-click or double-click a handle, point or line to reset it.

## Controls

**CHANNELS**

- **Bands** (1 to 6): with fewer bands the top band takes everything above the last crossover in use
  (the crossovers above it still run, as all-passes). A change crossfades the bands' gains over 20 ms.
- **Mode**: **Stereo** (each band compresses left and right) or **M/S** (mid and side). The bands are
  split in left / right and turned into mid / side after the split; a change dips the output for 5 ms
  on each side of the switch.
- **Link** (Channel Link, 0 to 100 %): each channel's detector level (in dB) moves towards the louder
  channel's by this much. 100 %: one gain for both channels; 0 %: each on its own.

**DYNAMICS**

- **Adaptive** (Adaptive Time, 0 to 100 %, 50 % by default): the larger the move the gain has to make,
  the faster Attack and Release run: both times are divided by 1 + 3 × Adaptive × min (1, |move| / 24
  dB). At 100 % a move of 24 dB or more runs four times as fast, at 50 % two and a half times; small
  moves keep the set times, so steady material stays smooth while the jumps of a hit are caught quickly.
  At 0 % the times are always the set ones.
- **Attack** (0.1 to 100 ms) and **Release** (5 to 2000 ms): a band's gain (in dB) moves towards what
  the gain law asks for with one-pole time constants, Attack while it goes down (more reduction, or less
  upward lift), Release while it comes back up. It gets 63 % of the way in that time.
- **Down Thr** (Downward Threshold, -96 to 0 dB) and **Down Ratio** (1 : 1 to 1 : inf, a brickwall at
  the threshold), or with **Negative** on, **Neg Ratio** (1 : -0.1 to 1 : -inf, see below).
- **Range** (1 to 60 dB, Negative only): the floor, this far below the downward threshold.
- **Up Thr** (Upward Threshold, -96 to 0 dB) and **Up Ratio** (1 : 1 to 1 : inf): below the threshold
  quiet sounds are brought up, by at most 30 dB.
- **Knee** (Soft Knee, 0 to 24 dB): rounds the corners at both thresholds over this width.
- **Tilt** (±3 dB/oct): tilts the bands' gains across the spectrum, pivoting at 1 kHz (each band gets
  Tilt × log2 (its centre / 1 kHz)).
- **Makeup** (-24 to +72 dB): gain on every band.

**LEVELS**

- **Input** (-24 to +48 dB): gain before the bands.
- **Dry/Wet**: the dry signal is the input before the Input gain, sent through the same all-passes as
  the bands so any blend lines up in phase.
- **Output** (±24 dB): after the blend, before the saturator.

**Smacheratr** (bottom panel): the saturator at the end of the chain, with all its controls.

## The gain law

Per band and channel, the detector level L is the band's peak (in dB, after the Input gain), held for
one period of the band's lowest frequency (its lower crossover; 25 Hz for the lowest band) and then
falling with a time constant of a quarter of that, so a steady tone reads its peak without ripple. With
e = L − T (T the downward threshold, W the knee width), the output's excess over the threshold is

    c(e) = e                                  for e ≤ −W/2
         = e + (s − 1)(e + W/2)² / (2W)       inside the knee
         = s·e                                for e ≥ W/2

- **Normal ratios** 1 : r: s = 1/r (r from 1 to inf, so s from 1 to 0; 1 : inf holds the output at the
  threshold).
- **Negative ratios** 1 : −x: s = −x. Above the threshold every dB the level rises takes the output
  down by x dB: louder in gives quieter out (1 : −1 mirrors the level at the threshold). The output
  never goes below the floor, Range dB under the threshold: c(e) = max (c(e), min (e, −Range)). At 1 :
  −inf (s = −10000, so the law stays continuous) everything over the threshold goes straight down to
  the floor, so loud and quiet parts leave at the same level. (In the usual in : out notation a ratio 1
  : −x is −1/x : 1.)

The downward gain is c(e) − e. Upward (threshold Tu, ratio 1 : ru, u = 1/ru, e = L − Tu): the gain is
(1 − u)(−e) below the threshold (the knee likewise), at most +30 dB, and levels below -150 dB count as
-150 dB. The two gains add up, then go through Attack / Release.

## Crossovers

Linkwitz-Riley 24 dB/oct splits: the first separates band 1 from everything above it, the next band 2
from what is left, and so on. Each lower band goes through the all-passes of the crossovers above it,
so with no compression the bands add up to the input exactly in level (shifted in phase). A moved
crossover glides there over about 20 ms.

## Latency

Only the end saturator's (about 1.8 ms at 48 kHz, always in the path): the crossovers add none and
there is no look-ahead. It never changes and is reported to the host for automatic compensation.

## Older projects

Dropr's first version (a drawn transient shape) was never in a release. A project saved with it opens
with the defaults above.
