# moistr

moistr turns a bass, typically a detuned Reese, into the wet, moving texture of neuro bass. It splits
the sound into bands with crossovers: 3 bands by default (Low, Mid and High) or 4 (with an Air band on
top). The Low band is locked: it never moves, so the low end stays solid. The other bands rise and fall
on their own, on a pattern that comes from a **Seed** number, so the sound keeps shifting without
anything being automated by hand, and a movement you like is always there again. A compressor and a
little soft clipping glue the bands back together, and a second pass can run the result through the
bands again. An optional frequency shifter can move the bands above the Low band up or down, for a
metallic edge, without ever touching the sub. Install instructions are in the [top-level README](../../README.md).

![moistr](../../docs/moistr/ui_moistr.png)

## How to use it

1. Put moistr on a bass track, for example a detuned saw Reese. The defaults split it into 3 bands:
   Low (up to a crossover between 100 and 500 Hz that **Seed** picks), Mid (up to 1.5 kHz) and High.
2. Set where the moving bands meet with **Mid X** (and **High X** with **4 Bands**), and balance the
   bands with **Low**, **Mid**, **High** and **Air** in **LEVELS**.
3. Set how much the bands move with **Movement** and how often with **Rate** (or switch on **Sync** to
   take the speed from the song tempo). **Depth** sets how far a band falls; **Rise** and **Fall** make
   the rises and falls quicker or slower.
4. Try other **Seed** numbers for other patterns. Each number picks when each band rises and falls, how
   quickly, and where the Low band ends. The same number always moves the same way.
5. For a metallic, clashing top end, switch on **SHIFT** and turn **Shift** a little either way. The Low
   band is never shifted, so the sub stays clean.
6. Use **Glue** and **Grit** to glue the bands back together. For an even denser, more processed sound,
   pick **2 Passes**.

Point at any control for help in the info box at the bottom (**?** also switches on hover tooltips).

## The technique

The idea is a common way of making this kind of bass by hand: split the bass into a few bands, keep
the low end steady and let the bands above it come and go, each on its own timing, so the upper part
of the sound keeps opening up and closing down. After the split comes some compression and subtle
distortion to glue it together. Often the result is bounced and processed again. moistr does all of
that in one plug-in, and the movement repeats exactly every time you play the song.

## Signal flow

```
input -> Drive -> pass 1 -> [pass 2] -> Mix (dry / wet) -> Output -> smacheratr (the end saturator)
pass:    split: Low | Mid | High [| Air] (crossovers) -> Low held, the others rising and falling
         -> [Shift: the bands above Low only] -> Low + the others -> Glue -> Grit
```

## Controls

**SPLIT**

- **3 Bands** / **4 Bands** (Bands): 3 bands (Low, Mid, High) or 4 (Low, Mid, High, Air). The bands
  are split with Linkwitz-Riley crossovers, so with every band at the same level and nothing moving
  they add back up to the full sound.
- **Drive** (10 %): light saturation before the split (a soft clipper, up to 18 dB of drive at 100 %).
  At 0 the input is untouched.
- **Mid X** (400 Hz to 6 kHz, 1.5 kHz by default): where the Mid band ends and the High band starts.
- **High X** (1.5 to 16 kHz, 5 kHz by default): where the High band ends and the Air band starts. Only
  used with **4 Bands** (dimmed with 3).

The Low band's crossover has no control of its own: **Seed** picks it, between 100 and 500 Hz. It never
moves. **Mid X** and **High X** may drift a little with the movement.

**LEVELS**: **Low**, **Mid**, **High** and **Air** (-48 to +12 dB, 0 by default): each band's level.
The Low band stays at its level. For the other bands it is the level they rise to; they fall from it by
up to **Depth**. All the way down (-48 dB) switches a band off. **Air** is only used with **4 Bands**
(dimmed with 3).

**RISE / FALL**

- **Rise** (0.25 to 4, 1 by default): scales how long the moving bands take to rise. **Seed** picks the
  rise times; below 1 they are quicker, above 1 slower (from a quarter to four times as long).
- **Fall** (0.25 to 4, 1 by default): the same for the falls.
- **Depth** (0 to 48 dB, 24 dB by default): how far a moving band falls below its level with
  **Movement** and its Move at 100 %.

**MOVEMENT**

- **Movement** (50 %): how much the bands rise and fall overall. At 0 every band holds still at its
  level.
- **Rate** (0.05 to 2 Hz, 0.3 Hz by default): how often the pattern of rises and falls comes round.
- **Sync** (off) and its rate below it (**4 Bars**, **2 Bars**, **1 Bar**, **1/2**, **1/4**, **1/8**; 1
  Bar by default): with **Sync** on, one cycle of the movement lasts that long at the song tempo instead
  of following **Rate**.
- **Seed** (1 to 128, 1 by default): the pattern. Each number picks, for every moving band, when it
  rises and falls and how quickly, and where the Low band's crossover is. The same Seed always gives the
  same movement.

**BAND MOVE**: **Mid Move** (50 %), **High Move** (100 %) and **Air Move** (100 %): how much each moving
band rises and falls, as a share of **Movement**. By default the High band moves the most. **Air Move**
is only used with **4 Bands** (dimmed with 3). The Low band has no Move: it is locked.

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
- **Glue** (40 %): a compressor on the bands together. It listens to the level (RMS over 10 ms) of both
  channels, with a soft knee (6 dB) and a fixed attack (10 ms) and release (150 ms). **Glue** sets the
  threshold (-10 to -30 dB) and the ratio (1:1 to 4:1) together, and makes up half of the gain it would
  take off at 0 dBFS. At 0 it is off.
- **Grit** (20 %): soft clipping after the compressor (up to 24 dB of drive at 100 %), for a little
  dirt. Quiet signals pass at their level. The clipper is anti-aliased (first-order antiderivative
  anti-aliasing), so it needs no oversampling and adds no latency.

**OUTPUT**: **Mix** (100 %: the effect against the untouched input) and **Output** (-24 to +12 dB). At
**Mix** 0 the output is exactly the input (delayed by the end saturator's latency, as the effect is).

**smacheratr** (bottom panel): the saturator every plug-in here can end with (off, Drive 0 dB). While
it is off it folds to its header strips; click a strip (or switch it on) to open it. See
[smacheratr](../smacheratr/README.md#at-the-end-of-the-other-plug-ins).

## The display

The display shows the bands against frequency (20 Hz to 20 kHz), each band's level from -48 dB (off) at
the bottom to +12 dB at the top. The Low band is drawn solid, with a lock and its crossover in Hz at the
top left. Each moving band is a region between its crossovers, filled up to its level now: while sound
plays you see Mid, High and (with **4 Bands**) Air rise and fall. With the shifter on, the shift (for
example +120 Hz) is shown at the top of each upper band. A line marks each moving band's level
and a dashed line how far it can fall (its level minus **Depth**, scaled by **Movement** and its Move).
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
factory presets: *Moist*: Bar Pulse, Classic Moist, Double Pass, Fast Flicker, Four Band, Shifted
Highs, Slow Swells, Wide Hollow; *Subtle*: Gentle Drift. Save your own with **Save As...** (a category and tags are
optional), filter the menu by tag, and use **Save as Default** to make every new moistr start from the
current settings. The menu is described in the [top-level README](../../README.md#presets).

## Latency

moistr adds no latency of its own (its crossovers, compressor and clippers work sample by sample). The end
saturator is always in the path, so switching it on or off never changes the latency, and its latency is
reported to the host for automatic compensation: 85 samples at 48 kHz at 4x **Oversampling** (the
default), 80 at 2x and 48 (its 1 ms look-ahead) with Off; changing it changes the latency, and the host
is told.

## Credits

The frequency shifter's Hilbert transformer uses the allpass coefficients published by Olli
Niemitalo. moistr is built on the Steinberg VST 3 SDK (MIT) and VSTGUI (BSD 3-clause); see
[THIRD_PARTY_NOTICES.md](../../THIRD_PARTY_NOTICES.md).
