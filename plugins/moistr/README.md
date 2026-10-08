# moistr

moistr turns a dry bass, typically a detuned saw or a Reese, into the wet, moving texture of neuro bass.
Its core is the **SWEEP** stage, on by default: two broad bell EQs sweep slowly up and down through the
low end, one boosting and one cutting, at slightly different rates so they keep drifting against each
other; a resonant **High Shelf** goes round a slow orbit, its corner and its gain moving together; then a
saturator, level matched, makes it all bite. The defaults are a proven recipe: bell A +18 dB sweeping 20
to 120 Hz at 0.70 Hz, bell B -18 dB sweeping 30 to 300 Hz at 0.77 Hz (both Q 0.71), the shelf going round
between 100 Hz and 1 kHz and -18 and +6 dB at Q 18, and 18 dB of drive. Wide sweeps with broad bumps
sound wet and talking, never like a phaser.

After the sweep, moistr can also split the sound into **moving bands** (see [Moving bands](#moving-bands)):
the low end locked, the bands above it rising and falling on a seeded pattern, glued back together with a
compressor and soft clipping, with a frequency shifter, **Link** and a vowel-like **Liquid** resonance on
top. In a new instance they are all neutral (**Movement**, **Glue** and **Grit** at 0), so the sound is the
sweep alone; turn them up to layer the movement on. Install instructions are in the
[top-level README](../../README.md).

![moistr](../../docs/moistr/ui_moistr.png)

## How to use it

1. Put moistr on a dry bass track, for example a detuned saw. The defaults already sound: the two bells
   sweep the low end, the **High Shelf** opens and closes the top, and the saturator glues it.
2. Set how hard it bites with **Drive** in **SWEEP** (12 to 24 dB sounds best on most basses). The level
   follows the input, so more **Drive** is more grit, not more volume.
3. Shape the sweeps in **BELL A** and **BELL B**: **Rate** (or **Sync** and its rate for the song tempo),
   where each bell goes (**Low**, **High**), how far it boosts or cuts (**Gain**) and how broad it is
   (**Width**, 0.71 is broad). **Phase** sets where each starts.
4. In **HIGH SHELF** set the orbit: where its corner goes (**Low**, **High**, up to 5 kHz), how far it cuts
   and boosts (**Min**, **Max**), how resonant it is (**Q**), how loose the orbit is (**Wander**: 0 a circle,
   higher a smooth random path) and how much less it boosts at high corners (**Tilt**, so it never gets
   nasal).
5. For more, layer the moving bands on: turn up **Movement** (and **Glue** and **Grit**), or start from the
   *Sweep/Sweep + Bands* preset. The other steps are under [Moving bands](#moving-bands).

Point at any control for help in the info box at the bottom (**?** also switches on hover tooltips).

## The technique

A common way of making this kind of bass by hand: a parametric EQ with a big, broad boost swept slowly
through the low end, a second with a broad cut swept a little faster over a wider range, a high shelf
moved around by hand (lower gain the higher it goes, so it does not get nasal), then a saturator. The two
sweeps at nearly the same rate keep moving against each other, so the sound never quite repeats. moistr
does all of that in one plug-in, and the sweeps follow the song position, so a render is the same every
time.

The older technique, splitting the bass into bands and letting the bands above the low end come and go
on their own timing, is moistr's second stage (the moving bands).

## Signal flow

```
input -> SWEEP: bell A -> bell B -> High Shelf -> saturator (level matched)
      -> Drive -> pass 1 -> [pass 2] -> Mix (dry / wet) -> Output -> smacheratr (the end saturator)
pass:    split: Low | Mid | High [| Air] (crossovers) -> Low held, the others rising and falling
         -> [Liquid: the bands above Low only] -> [Shift: the bands above Low only] -> Low + the others
         -> Glue -> Grit
movement: Seed's pattern [blended with Seed B's] -> each band's rises and falls (Density, Rise / Fall,
          Speed, Depth [-> Drop Out]) [pulled together by Link]; the Low band's own pushes and dips (Push,
          Dip); Liquid's gliding resonance (Liquid Low .. Liquid High, raised by Link as the bands open)
```

## Controls

**SWEEP**: the stage at the front, before everything else.

- **Sweep** (on): switches the whole stage. Off, the sound goes straight to the bands (as moistr before
  0.24). Switching fades over 20 ms.
- **Drive** (0 to 36 dB, 18 dB by default): how hard the saturator after the bells and the shelf is
  driven. It is a smooth tanh soft clipper with first-order antiderivative anti-aliasing (as **Grit**), so
  it needs no oversampling and adds no latency; a bass's harmonics fall off fast, so what aliasing is left
  stays far down. Its level is matched automatically: the make-up is worked out for the input's level (a
  slow RMS of the input, before the bells, held through silence) and the bells' average gain, so the
  output is about as loud as the input. The sweep never moves the make-up, so nothing pumps.

**BELL A** and **BELL B**: two peaking EQs, A then B, each sweeping its centre from **Low** to **High**
and back on a log scale (a smooth cosine, so it slows at each end).

- **Rate** (0.05 to 8 Hz; A 0.70 Hz, B 0.77 Hz by default): how fast the bell sweeps, while **Sync** is off.
- **Sync** (off) and its rate under it (**4 Bars** to **1/8**, 1 Bar by default): one sweep, there and
  back, lasts that long at the song tempo.
- **Phase** (0 to 360 degrees, 0 by default): where the bell starts: 0 at **Low**, 180 at **High**.
  **Seed** does not move it.
- **Low** and **High** (20 Hz to 2 kHz; A 20 to 120 Hz, B 30 to 300 Hz by default): the range of the
  centre.
- **Gain** (-24 to +24 dB; A +18 dB, B -18 dB by default): how far the bell boosts or cuts.
- **Width** (Q 0.2 to 10, 0.71 by default): low is a broad bump, high a narrow, ringing peak.

**HIGH SHELF**: a resonant high shelf after the bells, its corner and gain going round an orbit.

- **High Shelf** (on): switches it. Switching fades over 20 ms.
- **Rate** (0.05 to 8 Hz, 0.53 Hz by default): how fast it goes round.
- **Low** (50 Hz to 2 kHz, 100 Hz by default) and **High** (300 Hz to 5 kHz, 1 kHz by default): where its
  corner goes, on a log scale.
- **Min** (-24 to 0 dB, -18 dB by default) and **Max** (-12 to +12 dB, +6 dB by default): its lowest and
  highest gain.
- **Q** (0.3 to 24, 18 by default): its resonance. A high Q adds a bump just above the corner and a dip
  just under it, which makes the shelf talk as it moves. It stays stable at every setting and sample rate.
- **Wander** (0 to 100 %, 50 % by default): the orbit's shape. At 0 the corner and the gain go round a
  perfect circle (low corner and half gain, then up, then the high corner, then down); higher, the angle,
  the size and the centre of the orbit drift on smooth random curves picked by **Seed**, so it never quite
  repeats. It never jumps and always goes the same way round.
- **Tilt** (0 to 100 %, 65 % by default): lowers how far the shelf may boost as its corner rises above
  1 kHz, by up to three quarters of **Max** minus **Min** at 5 kHz. At the default the gain at a 5 kHz
  corner tops out about 12 dB under the top at 1 kHz and below, so high corners never sound nasal. At 0 it
  has the same gain range at every corner.

While the host plays, the bells and the shelf follow the song position (synced: locked to it; free: set
from it when playback starts or jumps), so rendering from the same place gives the same sweep. Every
control glides, and the filters' coefficients glide sample by sample, so moving a knob never clicks.

## Moving bands

The second stage: the multiband split and its movement. In a new instance it is neutral (**Drive**,
**Movement**, **Glue** and **Grit** at 0: the bands add back up to the sound), so it only does something
once you turn it up. Projects and presets saved before 0.24 open with their old settings and **Sweep** off,
so they sound exactly as they did.

**SPLIT**

- **3 Bands** / **4 Bands** (Bands): 3 bands (Low, Mid, High) or 4 (Low, Mid, High, Air). The bands
  are split with Linkwitz-Riley crossovers, so with every band at the same level and nothing moving
  they add back up to the full sound.
- **Drive** (0 by default; 10 % before 0.24): light saturation before the split (a soft clipper, up to 18 dB of drive at 100 %).
  At 0 the input is untouched.
- **Mid X** (400 Hz to 6 kHz, 1.5 kHz by default): where the Mid band ends and the High band starts.
- **High X** (1.5 to 16 kHz, 5 kHz by default): where the High band ends and the Air band starts. Only
  used with **4 Bands** (dimmed with 3).

The Low band's crossover has no control of its own: **Seed** picks it, between 100 and 500 Hz. It never
moves (not even with **Seed B**, **Push** or **Dip**). **Mid X** and **High X** may drift a little with
the movement.

**LEVELS**: **Low**, **Mid**, **High** and **Air** (-48 to +12 dB, 0 by default): each band's level.
The Low band stays at its level (unless **Push** or **Dip** in **LOW** move it). For the other bands it is the level they rise to; they fall from it by
up to **Depth**. All the way down (-48 dB) switches a band off. **Air** is only used with **4 Bands**
(dimmed with 3).

**RISE / FALL**

- **Rise** (0.25 to 4, 1 by default): scales how long the moving bands take to rise. **Seed** picks the
  rise times; below 1 they are quicker, above 1 slower (from a quarter to four times as long).
- **Fall** (0.25 to 4, 1 by default): the same for the falls.
- **Depth** (0 to 48 dB, 24 dB by default): how far a moving band falls below its level with
  **Movement** and its Move at 100 %. With **Drop Out** on, the deepest falls go to silence.

**MOVEMENT**

- **Movement** (0 by default; 50 % before 0.24): how much the bands rise and fall overall. At 0 every band holds still at its
  level.
- **Rate** (0.05 to 2 Hz, 0.3 Hz by default): how often the pattern of rises and falls comes round.
- **Sync** (off) and its rate below it (**4 Bars**, **2 Bars**, **1 Bar**, **1/2**, **1/4**, **1/8**; 1
  Bar by default): with **Sync** on, one cycle of the movement lasts that long at the song tempo instead
  of following **Rate**.
- **Seed** (1 to 128, 1 by default): the pattern. Each number picks, for every moving band, when it
  rises and falls and how quickly, and where the Low band's crossover is. The same Seed always gives the
  same movement. Changing it while playing crossfades to the new pattern over 100 ms.

**BAND MOVE**: **Mid Move** (50 %), **High Move** (100 %) and **Air Move** (100 %): how much each moving
band rises and falls, as a share of **Movement**. By default the High band moves the most. **Air Move**
is only used with **4 Bands** (dimmed with 3). The Low band has no Move: it moves only with **Push** and
**Dip** in **LOW**.

**SEED B / LINK**: a second pattern, blended with Seed's, and how much the moving bands share one.

- **Seed B** (1 to 128, 2 by default): the second pattern, its own rises and falls and their times for
  every band. The Low band's crossover stays where **Seed** puts it, so the low end does not move.
  Dimmed while **Blend** is 0.
- **Blend** (0 to 100 %, 0 by default): at 0 only Seed's pattern plays (moistr as without Seed B), at
  100 % only Seed B's. In between both play and overlap: each band follows a soft maximum of the two
  (at 50 % a band is up whenever either pattern has it up), so the sound is fuller and louder. Changing
  **Seed B** while playing crossfades over 100 ms.
- **Link** (0 to 100 %, 0 by default): how much the moving bands (Mid, High and Air) rise and fall
  together. At 0 each follows its own moments, as before. At 100 % they all follow the Mid band's, so
  the whole top opens and closes as one, the way a filtered bass sweeps open; each band still keeps its
  own **Level**, its Move and so how far it falls. In between, each band's movement is pulled that far
  towards the Mid band's. With **Liquid** on, **Link** also lifts the resonance as the bands open. The
  Low band is never linked.

**LOW**: the Low band's own movement. Its crossover never moves and the shifter never touches it.

- **Push** (0 to 12 dB, 0 by default): on its own seeded moments (from **Seed**, apart from the other
  bands' pattern, and blended with **Seed B**'s like the rest) the Low band comes forward, up to this far
  above its level (x **Movement**). **Density**, **Rise**, **Fall** and **Speed** apply to it too.
- **Dip** (0 to 6 dB, 0 by default): on other moments it dips back, at most this far under its level (x
  **Movement**): never more than 6 dB, so the low end does not go far.

With both at 0 the Low band is locked, exactly as before.

**EXTREME**: more extreme movement.

- **Drop Out** (off by default): lets the deepest falls go all the way to silence. As a band's fall
  (**Depth** x **Movement** x its Move) goes past 30 dB its floor curves smoothly down, to nothing at
  48 dB. Falls of 30 dB or less are not changed. Switching it fades over 20 ms.
- **Density** (0.25 to 8, 1 by default): how many rises and falls. **Seed**'s pattern comes round
  this many times as often: 8 has eight times as many in the same time, 0.25 a quarter as many. Rises
  never overlap badly: a new rise during a fall takes over smoothly where it meets it. Changing it
  crossfades over 100 ms.
- **Speed** (1 to 16, 1 by default): divides every rise and fall time (after **Rise** and **Fall**),
  down to 1 ms. The ramps stay smooth raised-cosine curves, so even the fastest chops do not click.

At their defaults (Blend 0, Push and Dip 0, Drop Out off, Density and Speed 1) moistr sounds exactly
as it did before these controls.

**LIQUID**: a moving resonance on the bands above Low, the wet, liquid part of the sound. Two peaks, like
the two lowest resonances of a voice saying a vowel, glide from vowel to vowel through the upper bands
(after they rise and fall, before the shifter and **Glue**). Where they go comes from **Seed** (blended
with **Seed B** like the rest) and runs on the movement's clock (**Rate**, or **Sync**, times
**Density**): mostly slow glides, now and then a quick jump. The Low band never goes through it, so the
sub stays clean and mono.

- **Liquid** (0 to 100 %, 0 by default): how strong the peaks are (the first up to 18 dB, the second up
  to 12 dB). At 0 it is off and moistr sounds exactly as without it; **Res**, **Low** and **High** are
  then dimmed. It fades in and out smoothly.
- **Res** (0 to 100 %, 50 % by default): how sharp the peaks are, from a broad wash to a narrow,
  whistling vowel.
- **Low** (150 to 800 Hz, 250 Hz by default) and **High** (600 Hz to 4 kHz, 1.6 kHz by default): the
  range the first peak moves in. The second sits above it, as in a vowel.

At their defaults (**Link** and **Liquid** 0) moistr sounds exactly as it did before them.

**SHIFT**: a frequency shifter on the bands above Low, after they rise and fall and before **Glue**.
The Low band (everything under the Low crossover, so the sub) is never shifted.

- **On** (off by default): switches the shifter on. Off, moistr sounds exactly as without it, and
  **Shift** and **Shift Mix** are dimmed.
- **Shift** (-500 to +500 Hz, 0 by default): how far every frequency of the upper bands moves, up or
  down. It shifts by the same number of Hz (a single sideband, made with an allpass Hilbert
  transformer), not by a ratio as a pitch shifter does, so the harmonics no longer line up: a little
  gives a hollow, detuned edge, more a metallic, clangorous one.
- **Shift Mix** (100 %): the shifted upper bands against the unshifted ones. Below 100 % both play
  together, which beats and swirls.

**GLUE**

- **1 Pass** / **2 Passes**: with **2 Passes** the result of the first pass (the split, **Glue** and
  **Grit**) runs through the same bands, **Glue** and **Grit** a second time, with a movement of its own
  (a second pattern from the same Seed). That is like bouncing the sound and splitting it again.
  Switching crossfades over 20 ms.
- **Glue** (0 by default; 40 % before 0.24): a compressor on the bands together. It listens to the level (RMS over 10 ms) of both
  channels, with a soft knee (6 dB) and a fixed attack (10 ms) and release (150 ms). **Glue** sets the
  threshold (-10 to -30 dB) and the ratio (1:1 to 4:1) together, and makes up half of the gain it would
  take off at 0 dBFS. At 0 it is off.
- **Grit** (0 by default; 20 % before 0.24): soft clipping after the compressor (up to 24 dB of drive at 100 %), for a little
  dirt. Quiet signals pass at their level. The clipper is anti-aliased (first-order antiderivative
  anti-aliasing), so it needs no oversampling and adds no latency.

**OUTPUT**: **Mix** (100 %: the effect against the untouched input) and **Output** (-24 to +12 dB). At
**Mix** 0 the output is exactly the input (delayed by the end saturator's latency, as the effect is).

**smacheratr** (bottom panel): the saturator every plug-in here can end with (off, Drive 0 dB). While
it is off it folds to its header strips; click a strip (or switch it on) to open it. See
[smacheratr](../smacheratr/README.md#at-the-end-of-the-other-plug-ins).

## The displays

The sweep display (beside **HIGH SHELF**) shows the stage now from 20 Hz to 5 kHz: bell A's curve (solid),
bell B's (dashed) and the shelf's, each thin, and the whole stage's response in bold. Bars show where each
bell sweeps (at the bottom) and where the shelf's corner goes (at the top). The box at the right is the
shelf's orbit: its corner across (**Low** to **High**), its gain up (**Min** to **Max**), a dashed line for
the most it may boost at each corner (**Tilt**) and a dot where it is now.

The bands' display shows the bands against frequency (20 Hz to 20 kHz), each band's level from -48 dB (off) at
the bottom to +12 dB at the top. The Low band is drawn solid, with a lock and its crossover in Hz at the
top left; with **Push** or **Dip** it has no lock and moves like the others, between dashed lines for
how far it can come forward and dip back. Each moving band is a region between its crossovers, filled up to its level now: while sound
plays you see Mid, High and (with **4 Bands**) Air rise and fall. With the shifter on, the shift (for
example +120 Hz) is shown at the top of each upper band. A line marks each moving band's level
and a dashed line how far it can fall (its level minus **Depth**, scaled by **Movement** and its Move;
with **Drop Out**, lower, down to the bottom for a full fall).
With **Liquid** on, a bar at the top shows the range its resonance moves in, and while sound plays two
markers show where its peaks are now.
The first pass's gain reduction is shown at the top right. Half a second after the input goes quiet the
display stops following the movement and shows the bands at their levels.

## Movement and the song position

The movement is worked out from its phase alone, so it repeats exactly when the phase does:

- With **Sync** on and the host playing, the phase is locked to the song position: one cycle per
  **Sync** rate. A bar of the song always has the same movement, wherever playback starts.
- With **Sync** off and the host playing, the phase is set from the song position (in seconds at the
  current tempo, times **Rate**) when playback starts or jumps, and then runs at **Rate**. So playing (or
  rendering) from the same place gives the same movement, as long as **Rate** is not automated before
  that place.
- With the host stopped (or no song position), the movement runs on its own.

## Presets

The **Presets** menu in the header starts with **Init** (every control at its default) and has these
factory presets: *Sweep*: Moist Default (the defaults), Heavy, Gentle, Wide Bump, High Shelf 5k, Sweep +
Bands; *Moist*: Bar Pulse, Chop, Classic Moist, Coagulate, Double Pass, Fast Flicker, Four
Band, Liquid, Low Push, Seed Blend, Shifted Highs, Slow Swells, Wide Hollow; *Subtle*: Gentle Drift. Save your own with **Save As...** (a category and tags are
optional), filter the menu by tag, and use **Save as Default** to make every new moistr start from the
current settings. The menu is described in the [top-level README](../../README.md#presets).

## Latency

moistr adds no latency of its own (its sweeping filters, crossovers, compressor and clippers work sample
by sample). The end
saturator is always in the path, so switching it on or off never changes the latency, and its latency is
reported to the host for automatic compensation: 85 samples at 48 kHz at 4x **Oversampling** (the
default), 80 at 2x and 48 (its 1 ms look-ahead) with Off; changing it changes the latency, and the host
is told.

The *Moist* and *Subtle* presets are from before the SWEEP stage and keep it off, so they sound as they
always did.

## Credits

The frequency shifter's Hilbert transformer uses the allpass coefficients published by Olli
Niemitalo. moistr is built on the Steinberg VST 3 SDK (MIT) and VSTGUI (BSD 3-clause); see
[THIRD_PARTY_NOTICES.md](../../THIRD_PARTY_NOTICES.md).
