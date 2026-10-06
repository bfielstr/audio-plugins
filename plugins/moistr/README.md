# moistr

moistr turns a bass, typically a detuned Reese, into the wet, moving texture of neuro bass. It splits
the sound into three bands: a low band, a low-mid band and a high band, with a clear hollow between the
low mids and the high end. Each band's filter frequency and level then drift slowly and on their own,
the high band the most, so the sound keeps shifting without anything being automated by hand. A
compressor and a little soft clipping glue the bands back together, and a second pass can run the
result through the bands again. The movement comes from a **Seed** number, so a movement you like is
always there again. Install instructions are in the [top-level README](../../README.md).

![moistr](../../docs/moistr/ui_moistr.png)

## How to use it

1. Put moistr on a bass track, for example a detuned saw Reese. The defaults split it at 180 Hz (Low),
   450 Hz (Mid) and 3 kHz (High), with the High band moving the most.
2. Turn **Gap** right to widen the hollow between Mid and High for a wetter, more hollow sound, or left
   to close it.
3. Set how much the bands move with **Movement** and how fast with **Rate** (or switch on **Sync** to
   take the speed from the song tempo). **Low Move**, **Mid Move** and **High Move** share the movement
   out between the bands.
4. Try other **Seed** numbers for other patterns of movement. The same number always moves the same way.
5. Use **Glue** and **Grit** to glue the bands back together. For an even denser, more processed sound,
   pick **2 Passes**.

Point at any control for help in the info box at the bottom (**?** also switches on hover tooltips).

## The technique

The idea is a common way of making this kind of bass by hand: put three copies of the bass in a group,
one through a low-pass filter, one through a band-pass in the low mids and one through a high-pass,
leave a big gap between the low mids and the high end, get the ratios between the filters right and
then move each filter's frequency a little, and each copy's volume, with the high end moving the most.
After the group comes some compression and subtle distortion to glue it together. Often the result is
bounced and filtered again. moistr does all of that in one plug-in, and the movement repeats exactly
every time you play the song.

## Signal flow

```
input -> Drive -> pass 1 -> [pass 2] -> Mix (dry / wet) -> Output -> smacheratr (the end saturator)
pass:    Low (low-pass) + Mid (band-pass) + High (high-pass), each moving -> Glue -> Grit
```

## Controls

**SPLIT**

- **12 dB** / **24 dB** (Slope): how steeply the band filters cut. At 24 dB each filter is two stages
  (the second stage of the low- and high-pass has no resonance of its own, so **Res** works the same).
- **Drive** (10 %): light saturation before the split (a soft clipper, up to 18 dB of drive at 100 %).
  At 0 the input is untouched.
- **Gap** (-100 to 100 %, 0 by default): moves Mid down and High up by the same ratio, up to an octave
  each at 100 %, so the hollow between them widens while the ratio between them stays the idea of the
  sound. To the left both move towards each other. Low does not move.

**LOW**, **MID**, **HIGH**: each band has **Freq**, **Res** and **Level**.

- **Freq**: Low is a low-pass (40 Hz to 1 kHz, 180 Hz by default), Mid a band-pass in the low mids
  (100 Hz to 4 kHz, 450 Hz), High a high-pass (500 Hz to 16 kHz, 3 kHz).
- **Res** (resonance): from a gentle Q of 0.5 to a sharp peak of Q 12 (15 % for Low and High, 35 % for
  Mid by default). The band-pass stays at 0 dB at its centre whatever its **Res**.
- **Level** (-48 to +12 dB, 0 by default): the band's level in the mix of the three. All the way down
  (-48 dB) switches the band off.

**MOVEMENT**

- **Movement** (50 %): how far every band moves. At 0 the filters and levels stay exactly where they are
  set. At 100 % a band with a Move of 100 % swings up to an octave each way and its frequency is also
  offset by up to 0.15 octave (so the ratios between the bands shift a little as they move).
- **Rate** (0.05 to 2 Hz, 0.3 Hz by default): how fast. Each band moves at its own multiple of it (0.5 to
  1.5 times), so the bands never move in step.
- **Sync** (off) and its rate below it (**4 Bars**, **2 Bars**, **1 Bar**, **1/2**, **1/4**, **1/8**; 1
  Bar by default): with **Sync** on, one cycle of the movement lasts that long at the song tempo instead
  of following **Rate**.
- **Seed** (1 to 128, 1 by default): the pattern of the movement. Each number deals out, for every band,
  its frequency and level movements' rates, start phases, the blend between two slow sines and a smooth
  random curve, its share of the depth (75 to 100 % of its Move) and its frequency offset. The same
  Seed always gives the same movement.

**BAND MOVE**

- **Low Move** (20 %), **Mid Move** (50 %), **High Move** (100 %): each band's share of the movement.
  By default the high end moves the most and the low end the least.
- **Levels** (Level Move, 0 to 12 dB, 4 dB by default): how far a band's level moves up and down at
  full movement (a band's level movement is scaled by its Move, as its frequency movement is).

**GLUE**

- **1 Pass** / **2 Passes**: with **2 Passes** the result of the first pass (the bands, **Glue** and
  **Grit**) runs through the same bands, **Glue** and **Grit** a second time, with a movement of its own
  (a second pattern from the same Seed). That is like bouncing the sound and filtering it again.
  Switching crossfades over 20 ms.
- **Glue** (40 %): a compressor on the three bands together. It listens to the level (RMS over 10 ms)
  of both channels, with a soft knee (6 dB) and a fixed attack (10 ms) and release (150 ms). **Glue**
  sets the threshold (-10 to -30 dB) and the ratio (1:1 to 4:1) together, and makes up half of the gain
  it would take off at 0 dBFS. At 0 it is off.
- **Grit** (20 %): soft clipping after the compressor (up to 24 dB of drive at 100 %), for a little
  dirt. Quiet signals pass at their level. The clipper is anti-aliased (first-order antiderivative
  anti-aliasing), so it needs no oversampling and adds no latency.

**OUTPUT**: **Mix** (100 %: the effect against the untouched input) and **Output** (-24 to +12 dB). At
**Mix** 0 the output is exactly the input (delayed by the end saturator's latency, as the effect is).

**smacheratr** (bottom panel): the saturator every plug-in here can end with (off, Drive 0 dB). While
it is off it folds to its header strips; click a strip (or switch it on) to open it. See
[smacheratr](../smacheratr/README.md#at-the-end-of-the-other-plug-ins).

## The display

The display shows the three bands' filters against frequency (20 Hz to 20 kHz). Their set places are
drawn in dim copper, the hollow between Mid and High is shaded and its width is shown at the top left
(in octaves, between Mid's and High's frequencies). While sound plays, the bands are drawn again where
the movement has them now, with a cinnabar mark at each band's frequency; with **2 Passes** the second
pass is drawn dashed. The first pass's gain reduction is shown at the top right. Half a second after the
input goes quiet the display stops following the movement and draws only the set places.

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
factory presets: *Moist*: Classic Moist, Double Pass, High Wobble, Wet Hollow, Wide Gap; *Subtle*:
Gentle Drift. Save your own with **Save As...** (a category and tags are optional), filter the menu by
tag, and use **Save as Default** to make every new moistr start from the current settings. The menu is
described in the [top-level README](../../README.md#presets).

## Latency

moistr adds no latency of its own (its filters, compressor and clippers work sample by sample). The end
saturator is always in the path, so switching it on or off never changes the latency, and its latency is
reported to the host for automatic compensation: 85 samples at 48 kHz at 4x **Oversampling** (the
default), 80 at 2x and 48 (its 1 ms look-ahead) with Off; changing it changes the latency, and the host
is told.

## CPU

The defaults take about 1 % of one core at 48 kHz stereo; the heaviest settings (**2 Passes**, 24 dB,
full movement at 2 Hz, **Glue** and **Grit** at 100 %, the end saturator on) about 2.5 % (measured as
process CPU time, the best of three runs, on the build machine).
