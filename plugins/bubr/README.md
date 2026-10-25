# bubr

bubr is a bass enhancer: it takes a band of your sound at a frequency you tune (20 to 125 Hz), drives it into
a clipper and blends it back with the dry sound, with a gate that keeps the effect to the louder parts and a
0 dBFS limiter at the output. It was measured setting by setting from a commercial bass enhancer, as a 1:1
model to start from (a version with pinchr in it, and an analog version, are to come). Install instructions
are in the [top-level README](../../README.md).

## How to use it

1. Put bubr on a kick, a bass, a drum bus or a sound effect and press play. A new instance is the model's
   own defaults: **Boost** 55 %, **Freq** 40 Hz, **Intensity** 50 %, **Q-Factor** 20 %, **Drive** 20 %, the gate at
   -30 dB, **Dry/Wet** 0 % (the dry sound and the effect both at full level).
2. Tune **Freq** to the sub or low end of the source; the display shows where the effect sits.
3. **Boost** sets how much of the effect you hear. **Intensity** drives the clipper harder (more weight and
   harmonics, not more level once it clips); **Drive** makes the clipped effect louder; **x2** doubles Drive's
   stage. **Q-Factor** narrows the band onto **Freq** (and drives it harder there).
4. Raise **Gate** if the effect hangs on in quiet parts: below it, the effect is turned down by **Ratio**
   (from about 50 it shuts). **Attack** and **Release** set how fast it opens and closes.
5. **Low Cut** removes rumble from the effect (and from the dry while **Dry/Wet** mixes it in). Turn **Dry/Wet**
   to +100 % to hear the effect alone, -100 % for the dry sound.
6. The output limiter holds everything under 0 dBFS; **Output** comes after it. **Soft Clip** and **Hard
   Clip** at the very end, off by default.

Point at any control for help in the info box at the bottom (**?** also switches on hover tooltips).

## The display

- At the left, the effect's response (the band at **Freq** and **Q-Factor**, its gains and **Low Cut**, while it
  is not clipping) from 10 Hz to 1 kHz, **Freq** and **Low Cut** marked, the gain at **Freq** at the top.
- At the right, the gate's curve: the effect's level against the input's peak (-72 to 0 dB), with a dot at the
  input's level now, and the gate's gain and the output limiter's gain reduction while audio plays.

## Controls

**BAND**

- **Freq** (20 to 125 Hz, 40 Hz by default): the centre of the band the effect is made from.
- **Q-Factor** (0 to 100 %, 20 % by default): low spreads the band wide around **Freq**, high narrows it onto
  **Freq** (and drives the clipper harder there: about +9 dB from 0 to 100 %).
- **Low Cut** (20 to 125 Hz, 20 Hz by default): a fourth-order high-pass on the effect. It is always in (20 Hz
  is its lowest). From **Dry/Wet** 0 % up it filters the dry sound too; below 0 % it fades out of the dry.

**EFFECT**

- **Boost** (0 to 110 %, 55 % by default): the effect's level, 0.3 dB a percent, part into the clipper and part
  after it. The part into the clipper is taken back as the band gets loud (a compressor on both channels
  together, about 19 ms attack and 34 ms release), so it lifts the quiet passages most.
- **Intensity** (0 to 100 %, 50 % by default): the gain into the clipper, from about unity (0 %) to some 36 dB
  more (100 %). The effect's ceiling does not move.
- **Drive** (0 to 100 %, 20 % by default): the gain after the clipper, about 5.7 dB over its range. **x2**
  (off by default) doubles its stage: about 9.6 dB.

**GATE**

- **Gate** (-60 to 0 dB, -30 dB by default): the threshold, on the input's peak (both channels together).
- **Ratio** (1 to 100, 4 by default): below the threshold the effect drops **Ratio** - 1 dB a dB; the middle of
  the knob is 10, and from about 50 the gate shuts.
- **Attack** (0 to 300 ms, 0 by default) and **Release** (20 to 500 ms, 100 ms by default).

The gate acts ahead of the clipper, so a gated signal also clips less.

**OUTPUT**

- **Dry/Wet** (-100 to +100 %, 0 % by default): -100 % the dry sound only, 0 % both at full level, +100 % the
  effect only. Below 0 % the effect is inverted against the dry at **Freq**, so as it comes in it first thins
  the band before it builds it up.
- **Output** (-60 dB, silence, to 0 dB; 0 dB by default), after the output limiter.
- **Oversample** (**Off** by default): the clipper at **2x** or **4x** (less aliasing when it is driven hard; some
  latency).
- **Lookahead** (on by default): the output limiter ramps in 1 ms early (that latency). Off: no latency, a
  little more distortion while it limits.
- **Soft Clip** and **Hard Clip** (off by default): the end of the chain, as in the other plug-ins.

## How it was measured

The reference was rendered in REAPER through scripted jobs (bubrcal: every setting written into the
plug-in's state, including the ones its host cannot automate) and fitted in Python: sweeps and a stepped-tone
file for the band at every **Freq** (13 positions) and **Q-Factor** (11), stepped sines at every Intensity,
Boost and Drive position and at nine frequencies for the clipper, a quiet probe under a loud control tone for
the gate, bursts and hot program material for the limiter, the dry, wet and gated paths apart for **Dry/Wet**
and **Low Cut**.

What it found: the band is four band-pass sections (fitted per grid point, within about 0.3 dB and 1 degree
over 6 to 480 Hz) with a clipper between the second and third; the clipper's ceiling at **Freq** is the same
for every **Freq** and **Q-Factor**; ahead of the clipper a compressor on both channels together takes back
Boost's share as the band gets loud (so **Boost** lifts the quiet parts most); a full-wave rectified copy of the
band adds even harmonics at every level; the
gate is a peak expander on the input whose gain moves in the linear domain; **Dry/Wet** and **Low Cut** follow
the reference's law exactly (two second-order high-passes, Q 0.8785, blended in with a Q that rises from 0.354);
the output has a 0 dBFS limiter with a fast and a slow envelope.

The tables are generated (`src/core/BandShape.h`, `WetShape.h`).

## How close it is

Against the reference's own renders (bubrcal/validate.py):

- The band, while it is linear: within 0.33 dB and 0.9 degree from 6 to 480 Hz at every Freq and Q-Factor tried
  (6 Freqs, 4 Q-Factors), wherever the response is within 40 dB of its peak.
- The clipper and the gains (stepped sines, 75 settings): the fundamental within 0.5 to 0.7 dB rms per group
  (Intensity, Boost, Drive, x2, Freq and Q-Factor, nine frequencies), up to about 3 dB near the onset of
  clipping at the extremes of Boost and in combined extreme settings; the third harmonic within 2 to 6 dB rms.
- The compressor (a quiet probe on one channel while the other plays): 0.3 to 0.6 dB rms over level, Freq,
  Intensity and Q-Factor, 1.7 dB rms at Boost 110 %; its attack and release within 0.2 to 0.6 dB rms.
- The gate's curve: within 0.3 dB rms at every threshold and ratio tried.
- The output limiter on hot program material: its gain reduction within 0.9 to 1.4 dB rms.
- Program material (drums, a bass line, a voice-like noise, sparse kicks, 26 settings): levels within 0.5 dB
  (0.8 dB on the effect alone), 50 ms envelopes within 0.1 to 0.8 dB (median), octave bands within about 1 dB
  where the sound has energy. Further off: Dry/Wet below 0 %, where the inverted effect cancels the dry at Freq
  and small differences show as large ones (2.7 dB median at -50 %); Drive x2 pushed into the output limiter
  (about 3 dB less above 250 Hz); the effect alone below 20 Hz (up to 3 dB more); a stereo source with different
  material per channel (1.5 dB median on the louder side).
- Not modelled: the reference's fixed 480-sample latency (bubr reports only its own: the lookahead and the
  oversampling); a square-wave input opens the reference's gate about 6 dB early (bubr's gate reads the plain
  peak, as every other waveform tried does in the reference).
