# Detonatr

An explosion designer built on the user's own explosion chain in REAPER, the one that makes their
explosions punchy:

MVocoder -> Spiff -> SpinTracer ("Liquid Debris") -> Oxford TransMod -> Pro-L2 -> TransMod -> Pro-C 3
(TTM) -> Pro-C 3 (TTM) -> Saturn 2 ("Warm Tape", two bands split at 200 Hz) -> Pro-L2.

Detonatr has ten stages that do what those plug-ins do there, in that order by default: **Vocoder**,
**Spike**, **Motion**, **Transient 1**, **Limiter 1**, **Transient 2**, **Comp 1**, **Comp 2**,
**Tape** and **Limiter 2**. Then **Dry/Wet** (the input lined up in time), **Output**, and the
**Smacheratr** every plug-in of the suite ends with (off by default).

Their defaults are the user's settings from that chain where they sent them (screenshots); a few were
read off knob positions or not shown and are estimates (they are marked ESTIMATED in the one table
of defaults at the top of `src/core/Params.cpp`, so they are easy to change). The default chain is
level matched stage to stage and never goes over 0 dBFS (the last limiter's ceiling is 0 dBTP).

**Old projects:** the earlier Detonatr (up to 0.7) had other stages (Clean, Tone, Multiband,
Transient, Saturator) and other parameters. By the user's choice they are gone: a project saved with
it (state version 1 or 2) opens with the new defaults (its old settings, and its recordings, are not
carried over), so it sounds different.

## The strip

The stages run in the order of the **strip** at the top: drag a stage sideways to move it, click it to
show its page, click its light to turn it on or off. The Smacheratr's box at the end of the strip shows
its page and has a light too; it stays at the end. A stage that is off only delays, so the latency
(about 44 ms at 48 kHz, reported to the host) never changes, whatever the order or what is on: the
Motion stage's 10 ms, the limiters' 5.3 ms each, the Tape stage's 21.6 ms and the Smacheratr's. A
stage turned on or off crossfades over 10 ms; one coming back starts fresh and fades in once its own
delay has filled.

The **hit display** under the strip shows the input (grey) and output (orange) levels of the last
second; click it to show a quarter, half, one or two seconds.

## The stages

### Vocoder

MeldaProduction MVocoder in its Vocoder mode with the same signal on its side-chain: the sound vocodes
itself. A bank of **Bands** band-pass filters (8 to 100, 32 by default), log-spaced from **Low** to
**High** (40 Hz to 16 kHz), each as wide as the spacing; **Order** cascades one, two or three filter
sections (Gentle, Medium, Steep). Each band gets a level follower (**Attack**, **Release**) and is
multiplied by its own level: the band-wise square of the sound, so loud bands come out louder against
quiet ones, and each band's attack, where its level jumps, louder against its tail. That is the punch.
The vocoded sound is level matched to the input (its gain follows the ratio of their powers over
60 ms), so on held material it is as loud as the input and a hit comes out up to 8 dB louder per band.
**Ratio** (0.46 by default) blends the input (0) with the vocoded sound (1), as MVocoder's Ratio
works as a dry/wet in this mode. The level followers are linked: one per band for both channels, so
the stereo image stays. No latency. The page shows each band's level.

### Spike

oeksound Spiff: a spectral transient processor, here 16 bands from **Low** to **High** (20 Hz to
20 kHz, a flat curve), boosting (**Mode** Boost, the default) or cutting (Cut) the transients in each
band. In each band two followers run on its level, a fast one and a slower one; a transient is where
the fast one is over the slow one.
- **Depth** (0 to 10, 5.1): the most a band is boosted or cut, 1.8 dB per step (5.1: 9.2 dB).
- **Sensitivity** (0 to 10, 3.7): how far over counts: from 10 - Sensitivity dB on (6.3 dB), fully
  6 dB further.
- **Decay** (0 to 10, 7.1): how long the boost lasts after each transient (2 ms x 2^(0.7 Decay): 63 ms).
- **Sharpness** (0 to 10, 1.3): how quickly the slow follower catches up (17 ms at 1.3, 3.6 ms at 10),
  so higher counts only sharper onsets.
- **Decay LF/HF** (-10 to +10, 0): tilts the Decay across the bands (at +10 the highest band lasts
  four times as long and the lowest a quarter).
- **Stereo Link** (100 %): each channel's own transients (0 %) against the louder channel's.
- **Mix** (100 %) and **Trim** (0 dB).
The change is added to the input, so without transients the sound passes untouched. No latency. The
page shows each band's boost or cut.

### Motion

Tonsturm SpinTracer, "Liquid Debris"-like: the sound (both channels summed) is played by **Orbs**
virtual sources (1 to 16, 6 by default) moving round a listener, each heard through its own variable
delay line, so its pitch follows its speed towards or away from the ears (true Doppler: the delay is
the time the sound takes from where the orb was when it left it, so the shift is the moving-source
f c / (c + v), c = 343 m/s) and its level its distance (Distance / distance, at most +12 dB).
- **Pattern**: **Orbit** (circles round a centre) or **Swarm** (the default: each orb on its own
  smooth path inside a ball round the centre).
- **Speed** (m/s, 18 by default), **Distance** (m, 3: the centre ahead of the listener), **Radius**
  (m, 2: how far from the centre the orbs move).
- **Spread** (80 %): from centred to equal-power panning by each orb's direction; it also sets the
  ears' spacing (8.75 cm each side at 100 %), so the time difference between the ears.
- **Randomness** (60 %): Orbit: each orbit's tilt, size, speed and place; Swarm: the spread of the
  paths' rates.
- **Floor** (on): each orb's reflection off the floor (an image source 1.7 m under the ears, 0.4 x).
- **Mix** (50 %).
The delays are read with 4-point Hermite interpolation and taken relative to the centre: a source at
the centre is 10 ms late, the stage's constant latency. The page shows the orbs from above, with
their trails. (The user has not sent this one's settings; the defaults are an estimate.)

### Transient 1 and Transient 2

Sonnox Oxford TransMod's controls. A peak follower (**Rise Time**: its attack; **Overshoot**: its
release, how long a gain change lasts after a peak) against a longer-term average of it (**Recovery**).
The gain change is **Ratio** x (peak - average) in dB: at +1 a peak 10 dB over the average comes out
20 dB over it; negative values soften the attacks (and since the peak follower falls faster than the
average, a positive Ratio also lowers the tail between hits). **Deadband**: differences smaller than
it are ignored. **Threshold**: levels under it do not count. **Gain** is the level into the process,
**Overdrive** mixes saturation into the raised attacks (snap and weight), then **Output** and **Mix**.
One detector for both channels. No latency. The page shows the gain over the last seconds.
- Transient 1: Gain -8.49 dB, Threshold -80 dB, Deadband 0 dB, Ratio +0.27, Overshoot 25.94 ms, Rise
  0.10 ms, Recovery 77.46 ms, Overdrive 60.88 %.
- Transient 2: Gain -1.47 dB, Threshold -14.8 dB, Deadband 0 dB, Ratio +0.58, Overshoot 6.42 ms, Rise
  0.10 ms, Recovery 77.46 ms, Overdrive 0 %.

### Limiter 1 and Limiter 2

FabFilter Pro-L2's controls (Transparent, True Peak on): **Gain** drives the input (Limiter 1 +4.9 dB,
Limiter 2 0 dB), **Ceiling** (0 dBTP). The gain is worked out in two parts, like Pro-L2's two stages:
a slow one on the body (**Attack**, **Release**, no look-ahead) and a fast look-ahead one for what it
leaves over the ceiling, which glides into each peak over the **Lookahead** (0.1 to 5 ms; 1 ms)
along an S-curve. Every sample's gain is never above what it needs, so the ceiling holds by
construction; a last clamp guarantees the sample peak (the tests check it never acts). **True Peak**:
the peaks between samples are found at 4x (a 32-tap interpolator, as Pro-L2's oversampled
detection) and held 0.1 dB under the ceiling (programme material stays within about 0.05 dB of it on
an 8x meter). **Link**: each channel limited on its own (0 %) or both by the louder (100 %). The
latency is constant (5 ms plus 15 samples) whatever the Lookahead. The page shows the gain reduction
over the last seconds. Lookahead 1 ms, Attack 3 ms, Release 50 ms and Link 0 % are estimates.

### Comp 1 and Comp 2

FabFilter Pro-C 3 in its TTM ("To The Max") style, factory preset "BPF - Explosive", both the same:
upward and downward compression on three bands (24 dB/oct Linkwitz-Riley crossovers at **Low Split**
and **High Split**, 150 Hz and 2.5 kHz by default; the bands add up flat), so a band louder than
its target is turned down and a quieter one up, by (1 - 1 / **Ratio**) of the difference, within
**Range**.
- **Threshold** is the target; with **Auto Threshold** (on) each band's target is its own long-term
  level (rising over 300 ms, falling over 1 s), so each band is pulled towards where it has been
  sitting and the spectrum's balance stays.
- **Knee** blends the two stages: within Knee dB of the target the correction fades out, so 0 dB
  corrects every dB (the most pumping) and a wide knee leaves the level near the target alone.
- **Attack**: how fast a cut or a boost comes in. Long (the preset): a hit passes before the cut
  catches it, and the body after it swells as the boost comes in: the pumping.
- **Release**, **Hold** (a cut holds before it lets go). **Auto Release** (on): program dependent,
  1.5 x Release on held material, faster the further a band's level is over its 100 ms average, and
  a boost lets go of a hit within a millisecond.
- **Auto Gain** (on): keeps the stage as loud as its input (their powers over 500 ms).
- **Dry** (off): the bands, unprocessed, back in at this level. **Output**.
A band under -60 dBFS is never raised, so noise floors and silence stay down. No look-ahead and no
latency. The page shows each band's level, its target and its gain. Ratio 20, Attack 200 ms, Release
400 ms, Knee 3 dB, Range 24 dB, Hold 330 ms and the splits are estimates.

### Tape

FabFilter Saturn 2, Warm Tape, two bands split at **Split** (200 Hz). The split is linear phase and
complementary (Saturn's Linear Phase crossover): the lows are the input through moving averages
(2B - B^2), the highs the input minus the lows, so they add back to the input exactly. Each band is
saturated at 4x (Saturn's High Quality) on its own: **Drive** into a warm tape curve (a tanh with a
bias, so even harmonics too, and a gentle roll-off of the highs at 14 kHz), level compensated so a
-18 dBFS sine comes out as loud as it went in; **Dynamics** (-100 to +100 %) moves the drive with the
band's level (positive drives louder parts harder); **Mix** against the band clean and **Level**.
What the saturation adds goes through a DC blocker. The latency is constant (21.6 ms at 48 kHz: what
the lowest Split, 80 Hz, needs). The page shows the two bands' curves. Defaults: Mix 100 %, Dynamics
0, the low band Level +1 dB; both Drives 6 dB, the high band's Mix, Dynamics and Level are estimates.

## Dry/Wet, Output and the Smacheratr

**Dry/Wet** mixes the input, lined up with the stages, and **Output** sets the level; then the
**Smacheratr** (off by default) with all its controls and displays, on its own page (the last box of
the strip).

The Menu has the interface size and **Copy / Paste Settings** (every parameter as text on the
clipboard); the preset bar saves and loads presets.

## CPU

The full default chain takes about 19 % of one core at 48 kHz stereo (measured as process CPU time,
the best of three runs, on the build machine).
