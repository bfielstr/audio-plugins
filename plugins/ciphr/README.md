# ciphr

ciphr is an 8-voice synthesizer you play from the keyboard. Each note plays a cluster of six
wavetable oscillators. **Timbre** sweeps every oscillator through its own short list of waves and
pitches, so one knob turns a plain saw into an organ, a vowel or a glassy chord. **Cross** makes the
oscillators bend or multiply each other for FM tones and metallic bells. After the voices comes a
processor that goes from crisp multi-tap echoes to a dense reverb wash, with a frequency shifter in
its feedback for endlessly climbing echoes or slowly moving notches. **Variant** deals out a new set
of waves, pitches and echo taps at every number, and **Drift** lets them wander. ciphr can also take
a track through its side-chain input and run it through the same filter and processor. Install
instructions are in the [top-level README](../../README.md).

![ciphr](../../docs/ciphr/ui_ciphr.png)

## How to use it

1. Put ciphr on an instrument track and play. By default each note is a bright saw-based cluster
   with soft echoes (**Blend** 35 %).
2. Turn **Timbre** slowly while you hold a chord: every oscillator crossfades through its list.
3. Try other **Variant** numbers for new clusters and tap patterns. The same number always gives the
   same sound, so a Variant you like is easy to find again.
4. Push **Space** up for reverb, set the echo time with **Length**, and raise **Regen** for more
   repeats. With a little **Shift** (a few Hz) the repeats climb (Regen right of centre) or sweep with
   moving notches (Regen left of centre).
5. For a living, slowly changing pad, raise **Drift**.

Point at any control for help in the info box at the bottom (**?** also switches on hover tooltips).

## Signal flow

```
MIDI notes -> 8 voices: oscillator cluster -> Cross -> (+ input, Voices path) -> filter -> amp envelope
           -> sum (+ input, Direct path) -> processor (taps, diffusion, shifter in the feedback)
           -> Blend -> Output -> smacheratr (the end saturator)
```

The voices are mono and centred; the processor makes the stereo (its taps are panned). New notes take
a silent voice; with all 8 sounding, a new note takes the quietest released voice, or else the one held
longest. A stolen or retriggered voice keeps its oscillators and filter running and its envelopes rise
from where they are, so stealing does not click. Playing a note that is already held retriggers it.

## Controls

**GENERATOR**

- **Timbre** (0 %): each oscillator has a list of four entries, each a wave and a pitch offset. At 0 %
  every oscillator plays its first entry, at 100 % its last; in between it crossfades the two
  neighbouring entries (each keeps its own phase), so the sweep is smooth.
- **Cross** (centre): at the centre the oscillators do not touch each other (exactly the same as no
  Cross at all). Left of centre is FM: each oscillator's phase is moved by its neighbour's output (the
  first by the last's), up to 0.35 cycles at the far left. Above a 2 kHz fundamental the depth is
  turned down in proportion, so high notes stay clean. Right of centre is ring modulation: each
  oscillator is crossfaded towards itself multiplied by its neighbour, all the way at the far right.
- **Character** (50 %): a macro over both halves of ciphr. In the cluster it spreads the oscillators'
  tuning apart, up to 18 cents each way for the outermost ones. In the processor it adds taps (2 at 0,
  8 at 100 %, fading in one after another) and brightens the feedback loop (its damping filter goes
  from 2.5 kHz up to 16 kHz).
- **Variant** (1 to 128, 1): a seed for a fixed random number generator. Each number sets every
  oscillator's four entries (waves from the built-in set, pitch offsets in octaves, fifths and fourths
  with a few cents of detune), the oscillators' start phases and the processor's tap pattern (eight tap
  times, levels, pans and wobble rates). The first oscillator's first entry is always a saw at the
  note, so every Variant has a solid root. The same Variant always gives the same patch.
- **Drift** (0 %): slow random glides. Every oscillator's place in its list (up to three quarters of an
  entry either way) and tuning (up to 12 cents), and every tap's time (3 %) and level (30 %), glide
  smoothly towards new random targets, between 0.05 and 0.5 times a second as Drift goes up. At 0
  nothing moves. The glides start from the same place for each Variant.
- **Tune** (-24 to +24 semitones).

**INPUT**: ciphr has a stereo side-chain input. **Input** (0 %) sets its level. **Direct** (the
default) adds it to the voices' sum just before the processor. **Voices** sends it, summed to mono,
into every sounding voice before its filter, so the keys play the input through their filters and
envelopes (each voice adds its own copy; with no note held you hear none of it).

**FILTER**: a morphing state-variable filter in each voice (zero-delay form). **Cutoff** (20 Hz to
20 kHz, 6 kHz), **Resonance** (20 %), **Type** (0 %: low-pass at 0, band-pass at 50 %, high-pass at
100 %, crossfading between them), **Key Track** (50 %: at 100 % the cutoff moves an octave per
octave, from C3) and **Env Amt** (Env Amount, +30 %: the filter envelope moves the cutoff up to 5
octaves up, or down left of centre).

**AMP ENVELOPE**: **Attack** (5 ms), **Decay** (400 ms), **Sustain** (80 %), **Release** (500 ms) and
**Velocity** (60 %: how much the key velocity sets the level). The attack is linear, the decay and
release exponential.

**FILTER ENVELOPE**: **Attack** (2 ms), **Decay** (600 ms), **Sustain** (20 %) and **Release**
(600 ms), for Env Amt.

**PROCESSOR**

A stereo multi-tap delay line with allpass diffusers and a feedback loop:

- **Space** (50 %): every diffuser crossfades between passing the sound untouched and diffusing it.
  At 0 the processor is a clean multi-tap delay (each tap an exact echo); at 100 % every echo is
  smeared into a wash, and the diffusers in the feedback loop make each repeat denser than the last,
  like a reverb. It also mixes the two channels in the loop a little.
- **Length** (10 ms to 2 s, 420 ms): the longest tap's time. The other taps sit at fixed fractions of
  it (from the Variant), and the diffusers scale with it too. Changing it glides over about 80 ms.
- **Movement** (30 %): slow sine wobble of each tap's time, each tap at its own rate, for a chorused,
  moving sound.
- **Regen** (+35 %): the feedback, taken from the longest tap. Right of centre only the
  frequency-shifted sound goes back round, so every repeat is shifted again (a spiral that climbs or
  falls forever). Left of centre the shifted and the unshifted sound go back together, which makes
  notches that sweep slowly through the sound. At 100 % either way the loop gain is 0.97; a soft
  clipper and a DC blocker in the loop keep it stable whatever the settings.
- **Shift** (-250 to +250 Hz, 0): how far the frequency shifter moves the sound each time round. It
  shifts every frequency by the same number of Hz (a single sideband, made with an allpass Hilbert
  transformer), so even small values like 1 to 5 Hz give slow, phasing movement.

**OUTPUT**: **Blend** (35 %: the dry voices against the processor's output) and **Output** (-24 to +12
dB).

The display shows the patch: on the left each oscillator's list of waves as small traces with their
pitch offsets, and a cinnabar mark where each oscillator is in its list now (it follows Timbre, and
Drift while notes play). On the right the processor's taps against time, each as tall as its level,
dim when Character has faded it out, under a haze as thick as Space.

**smacheratr** (bottom panel): the saturator every plug-in here can end with (off, Drive 0 dB). While
it is off it folds to its header strips; click a strip (or switch it on) to open it. See
[smacheratr](../smacheratr/README.md#at-the-end-of-the-other-plug-ins).

## The waves

The oscillators read from twelve built-in waves, generated when the plug-in loads (no sample files):
Sine, Triangle, Saw, Square, Pulse (25 %), Organ (harmonics 1, 2, 3, 4, 6 and 8), Vowel A and Vowel
O (sloping spectra with two formant-like peaks), Hollow (odd harmonics, falling quickly), Buzz (every
harmonic, very bright), Soft (a rounded saw) and Glass (sparse harmonics 1, 3, 7, 11 and 16).

Each wave is stored as ten band-limited tables, one per octave, the fullest holding 512 harmonics. A
note reads the fullest table whose highest harmonic stays below half the sample rate, so high notes
never fold harmonics back as off-pitch tones (aliasing). The tests measure what is left: below -90 dB
for a saw, square, pulse, Buzz and Vowel A at notes up to 7.8 kHz.

## Presets

The **Presets** menu in the header starts with **Init** (every control at its default) and has these
factory presets: *Basic*: Dry Cluster (the voices alone, a plain starting point); *FX*: Input Smear
(for the side-chain input); *Keys*: FM Pluck, Ring Bells; *Pads*: Barberpole Choir, Glass Cathedral.
Save your own with **Save As...** (a category and tags are optional), filter the menu by tag, and use
**Save as Default** to make every new ciphr start from the current settings. The menu is described in
the [top-level README](../../README.md#presets).

## MIDI

Notes and velocity. Pitch bend, the sustain pedal and other controllers are not used yet.

## Latency

ciphr's voices and processor add none. The end saturator is always in the path (so switching it on or
off never changes the latency) and its share is reported to the host: 85 samples at 48 kHz with its
**Oversampling** at 4x (the default), 80 at 2x and 48 with Off.

## CPU

Each voice has six oscillators (a fixed number in the code, `kOscs` in `src/core/Variant.h`). Eight
voices held take about 2.4 % of one core at 48 kHz with the defaults, and about 8.6 % at the heaviest
settings (FM, Drift, all eight taps, Space 100 %, the input on the Voices path and the end saturator
on), measured as process CPU time, the best of three runs, on the build machine. Eight oscillators
per voice measured about 3.0 % and 10.5 %.

## Credits

The frequency shifter's Hilbert transformer uses the allpass coefficients published by Olli
Niemitalo. ciphr is built on the Steinberg VST 3 SDK (MIT) and VSTGUI (BSD 3-clause); see
[THIRD_PARTY_NOTICES.md](../../THIRD_PARTY_NOTICES.md).
