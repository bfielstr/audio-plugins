# orbitr

orbitr turns a sound into a swarm. It plays the sound through 1 to 16 virtual sources, the orbs, that
fly around you. Each orb bends in pitch as it moves towards or away from you, the way a passing car
does, and gets louder as it comes close, so a plain sound becomes a moving, shimmering, liquid cloud.
Reach for it on pads, FX, risers, vocal chops and anything that should move around the listener.
Install instructions are in the [top-level README](../../README.md).

![orbitr](../../docs/orbitr/ui_orbitr.png)

## How to use it

1. Put orbitr on a track. By default 6 orbs swarm 3 m ahead of you at 18 m/s, mixed 50 / 50 with the
   input.
2. Raise **Speed** for more pitch bend, lower **Distance** to bring the swarm closer, and pick
   **Orbit** for circles instead of **Swarm**.
3. Use **Mix** to set how much of the orbs you hear against the input.
4. For a granular cloud, switch on **Grains**: each orb then plays short grains cut from the last
   second of the input, still flying round you.

Point at any control for help in the info box at the bottom (**?** also switches on hover tooltips).

## Controls

The sound (both channels summed) is played by **Orbs** virtual sources moving round a listener, each
heard through its own variable delay line. So its pitch follows its speed towards or away from the ears
(true Doppler: the delay is the time the sound takes from where the orb was when it left it, so the
shift is the moving-source f c / (c + v), c = 343 m/s) and its level follows its distance (Distance /
distance, at most +12 dB).

**MOTION**

- **Orbit** / **Swarm**: **Orbit** moves the orbs in circles round a centre; **Swarm** (the default)
  moves each orb on its own smooth path inside a ball round the centre.
- **Floor** (on): each orb's reflection off the floor (an image source 1.7 m under the ears, 0.4 x).
- **Orbs** (1 to 16, 6 by default).
- **Speed** (0 to 80 m/s, 18 by default), **Distance** (0.5 to 20 m, 3: the centre ahead of the
  listener), **Radius** (0.1 to 3 m, 2: how far from the centre the orbs move).
- **Spread** (80 %): from centred to equal-power panning by each orb's direction. It also sets the ears'
  spacing (8.75 cm each side at 100 %), so the time difference between the ears.
- **Random** (Randomness, 60 %): in Orbit, each orbit's tilt, size, speed and place; in Swarm, the
  spread of the paths' rates.
- **Mix** (50 %): the orbs against the input.

**GRAINS** (off by default; while it is off, orbitr sounds exactly as it did before Grains)

With **Grains** on, each orb plays a stream of short grains instead of the input itself. A grain is a
short piece of the recent input with a smooth (Hann) fade in and out. Each orb has its own slice of the
input: the first orb plays the input as it comes, the others start further back, spread evenly over the
last half second. The orbs still move as before, so each grain bends in pitch with its orb's Doppler and
pans and fades with its orb's place: the cloud swirls round you.

- **Grains** (off): plays grains instead of the input. Switching it crossfades over 30 ms.
- **Size** (Grain Size, 10 to 500 ms, 80 by default): how long each grain is.
- **Density** (Grain Density, 0.1 to 8, 2 by default): how many of an orb's grains overlap. At 2 each
  grain starts halfway through the one before, a smooth cloud (with no **Scatter** and **Pitch** the
  orb then plays its slice untouched). Below 1 the grains leave gaps, down to a sparse patter; above
  2 they pile up into a denser, smeared cloud (turned down so it stays about as loud).
- **Scatter** (Grain Scatter, 30 %): how far each grain jumps about: up to half a second further back
  than the orb's slice at 100 %, and its start in time moves by up to half the gap between grains. At
  0 every grain comes from the orb's own slice.
- **Pitch** (Grain Pitch, -12 to +12 semitones, 0 by default): transposes the grains by playing them
  faster or slower, before the orbs' own Doppler.

**OUTPUT**: **Dry/Wet** (100 %: the whole effect against the input, lined up in time) and **Output**
(-24 to +12 dB).

The orbs are summed / sqrt (Orbs), so the swarm stays about as loud as the input whatever their number.
The delays are read with 4-point Hermite interpolation and worked out every 16 samples (ramped in
between).

The display shows the orbs from above, with their trails: you at the bottom, facing up, the swarm's ball
ahead at the Distance, rings every metre (every 5 m when far). With **Grains** on each orb wears a ring
and swells with its grains.

**smacheratr** (bottom panel): the saturator every plug-in here can end with (off, Drive 0 dB). While
it is off it folds to its header strips; click a strip (or switch it on) to open it. See
[smacheratr](../smacheratr/README.md#at-the-end-of-the-other-plug-ins).

## Presets

The **Presets** menu in the header starts with **Init** (every control at its default) and has these
factory presets: *Grains*: Grain Cloud, Octave Sparks; *Motion*: Dense Swarm, Slow Orbit, Subtle
Shimmer. Save your own with **Save
As...** (a category and tags are optional), filter the menu by tag, and use **Save as Default** to
make every new orbitr start from the current settings. The menu is described in the [top-level
README](../../README.md#presets).

## Latency

The delays are taken relative to the centre: a source at the centre is 10 ms late, so orbitr's own
latency is a constant 10 ms (480 samples at 48 kHz) whatever the settings; the input in Mix and Dry/Wet
is delayed as much. **Grains** add none (the grains are read from input that has already arrived).
With the end saturator's (always in the path, so switching it on or off never
changes it) it is reported to the host for automatic compensation. Its share depends on its **Oversampling**: 85 samples at 48 kHz at 4x (the default), 80 at 2x and 48 (its 1 ms look-ahead) with Off; changing it changes the latency, and the host is told.

## CPU

The defaults take about 7 % of one core at 48 kHz stereo; 16 orbs about 12 % (measured as process CPU
time, the best of three runs, on the build machine, the saturator on). **Grains** add little at their
defaults: 16 orbs with Grains measured about 1.3 times 16 orbs without, and about 2 times at the
heaviest grain settings (500 ms grains at **Density** 8).
