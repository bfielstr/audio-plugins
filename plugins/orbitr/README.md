# Orbitr

A Doppler swarm in the spirit of Tonsturm SpinTracer (not affiliated): the sound is played by
virtual sources, the orbs, moving round you, and each one's pitch follows its speed towards or away
from your ears. It is Detonatr's Motion stage on its own (Detonatr has left the suite), with its
"Liquid Debris"-like setting as the defaults. Install instructions are in the
[top-level README](../../README.md).

## What it does

The sound (both channels summed) is played by **Orbs** virtual sources (1 to 16, 6 by default)
moving round a listener, each heard through its own variable delay line. So its pitch follows its
speed towards or away from the ears (true Doppler: the delay is the time the sound takes from where
the orb was when it left it, so the shift is the moving-source f c / (c + v), c = 343 m/s) and its
level its distance (Distance / distance, at most +12 dB).

- **Pattern**: **Orbit** (circles round a centre) or **Swarm** (the default: each orb on its own
  smooth path inside a ball round the centre).
- **Speed** (0 to 80 m/s, 18 by default), **Distance** (0.5 to 20 m, 3: the centre ahead of the
  listener), **Radius** (0.1 to 3 m, 2: how far from the centre the orbs move).
- **Spread** (80 %): from centred to equal-power panning by each orb's direction; it also sets the
  ears' spacing (8.75 cm each side at 100 %), so the time difference between the ears.
- **Randomness** (60 %): Orbit: each orbit's tilt, size, speed and place; Swarm: the spread of the
  paths' rates.
- **Floor** (on): each orb's reflection off the floor (an image source 1.7 m under the ears, 0.4 x).
- **Mix** (50 %): the orbs against the input, part of the Liquid Debris-like setting.
- **Dry/Wet** (100 %): the whole effect against the input, lined up in time; **Output** (-24 to
  +12 dB).

The orbs are summed / sqrt (Orbs), so the swarm stays about as loud as the input whatever their
number. The delays are read with 4-point Hermite interpolation and worked out every 16 samples
(ramped in between).

The display shows the orbs from above, with their trails: you at the bottom, facing up, the swarm's
ball ahead at the Distance, rings every metre (every 5 m when far).

The defaults are the ones Detonatr's Motion stage had: an estimate of SpinTracer's "Liquid Debris"
preset (its numbers were never sent): 6 orbs swarming at 18 m/s, 3 m ahead, radius 2 m, Spread 80 %,
Randomness 60 %, Floor on, Mix 50 %.

**Smacheratr** (bottom panel): the optional saturator at the end of the chain (off, Drive 0 dB).

## Latency

The delays are taken relative to the centre: a source at the centre is 10 ms late, so Orbitr's own
latency is a constant 10 ms (480 samples at 48 kHz) whatever the settings; the input in Mix and
Dry/Wet is delayed as much. With the end saturator's (always in the path, so it never changes) it is
reported to the host for automatic compensation.

## CPU

The defaults take about 7 % of one core at 48 kHz stereo; 16 orbs about 12 % (measured as process
CPU time, the best of three runs, on the build machine, the saturator on).
