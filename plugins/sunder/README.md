# sunder

sunder is a germanium compressor with a bass enhancer in front of it (or after it). The compressor is a FET compressor with
a choice of six knees, into a germanium output stage whose **Feedback** sets its gain and a resonant bass
bump; its compression curves, timing, detector and output stage were measured setting by setting from a
classic germanium compressor's software model, the base for a sound of its own. **Bass** blends in a bass
enhancer ahead of it (or after it, with **Bass Position**): a band of the low end at a frequency you tune, driven into a clipper, measured setting
by setting from a commercial bass enhancer. sunder was pinchr (projects saved with pinchr open in sunder) and
takes in bubr. Install instructions are in the [top-level README](../../README.md).

## How to use it

1. Put sunder on a drum bus, a bass, a kick, a vocal or a mix and press play. A new instance: **Ratio** 4,
   **Germ Soft**, **Attack** and **Release** 3, **Drive** 4, **Feedback** 3, **Bass** 0 (the enhancer off).
2. Turn **Input** up until the curve on the display shows the gain reduction you want. There is no makeup
   gain: more **Input** is louder until the compressor holds it, so trim with **Output**.
3. **Ratio** sets how hard it works (a lower threshold and a steeper knee as it goes up), **Curve** the
   knee's shape, from **R Soft** (the gentlest) to **Zener Hard** (the sharpest and earliest).
4. **Attack** and **Release**: 0 is fast, 10 slow. Short peaks release quickly, long loud passages more
   slowly, as on the hardware.
5. **Drive** and **Feedback** colour it: **Feedback** up gives more level and a bass bump (about 20 to 90
   Hz, higher with more **Drive**) with a steep cut below it; the display shows the response. **Saturation**
   sets how readily the germanium stage distorts (0 % for the clean, measured stage).
6. **Bass** adds weight before the compressor: tune **Bass Freq** to the sub or low end of the source in the
   BASS section, then bring **Bass** up. With **Bass Position** at **Post** it comes after the compressor
   instead, so the compressor does not pump on it.
7. **Mix** for parallel compression: both the compressed and the uncompressed signal go through the
   output stage.

Point at any control for help in the info box at the bottom (**?** also switches on hover tooltips).

## The display

- At the left, the output stage's response (**Drive** and **Feedback**) from 10 Hz to 20 kHz, relative to
  its gain at 1 kHz, with that gain at the top.
- At the right, the compression curve of the **Curve** at the **Ratio**: the level out against the level
  into the compressor, -50 to +12 dB, with a dot at the level reaching it and the gain reduction while
  audio plays.

## Controls

The Basic page has **Input**, **Ratio**, **Release**, **Drive**, **Feedback** and **Bass**, with **Mix**,
**Output**, **Soft Clip** and **Hard Clip**; the Advanced view has every control (**Bass Position** among
them: the Basic page's six main controls are taken).

**COMPRESSOR**

- **Curve** (**Germ Soft** by default): the knee's diodes. **R Soft** (a resistor), **Germ Soft** (one
  germanium diode), **Germ Med** (five), **Silicon Med** (one silicon diode), **Silicon Hard** (two),
  **Zener Hard**.
- **SC Filter** (**Off** by default): takes the lows out of what the compressor hears, **30**, **60**,
  **90**, **150** or **300 Hz**, as broad shelves. The detector always ignores the deepest lows.
- **Link** (on by default): one gain for both channels, set by the louder.
- **Input** (-48 to +48 dB, 0 by default): the level into the compressor. The knob is finer around 0 dB
  (its centre) and coarser towards the ends: a quarter turn from the centre is +15 dB, the rest of the way
  +48 dB. Double-click for 0 dB, or type a value. At the top the compressor sits at its deepest gain
  reduction (about 25 dB) and the rest is level into the germanium stage: with **Saturation** above 0 % it
  holds that near its headroom, at 0 % bring **Output** down or switch on **Soft Clip** and **Hard Clip**.
  The display's dot turns into a ring at its edge when the level is past it, with the level in its corner.
- **Ratio** (1 to 10 in steps, 4 by default): the detector's gain and the knee together.
- **Attack** and **Release** (0 to 10, 3 by default): 0 fast, 10 slow.
- **Ext SC** (off by default): the compressor listens to the sidechain input (route one to sunder in the
  host) instead of the sound it compresses; **SC Gain** (-24 to +24 dB, 0 by default) is its level.

The FET's second harmonic (it grows with the level and falls as the gain reduction deepens) is always in:
it is the normal sound (pinchr had it behind a **Dirty** switch).

**GERMANIUM**

- **Drive** (1 to 10 in steps, 4 by default): the output stage's gain switch, about 4.5 dB a step. Higher
  drive also raises the low resonance and the top end's lift.
- **Feedback** (1 to 10 in steps, 3 by default): the output stage's feedback. Up gives more gain and the
  resonant bass bump.
- **Saturation** (0 to 100 %, 50 % by default): the output stage's germanium transistor, an asymmetric
  saturation, mostly second harmonic, inside the stage's feedback loop. Higher sets it in at lower levels
  (from about +6 dBFS out of the stage just above 0 %, -3 dBFS at 50 %, -12 dBFS at 100 %), so more **Drive**
  also drives it harder. **Feedback** shapes it: low Feedback (much feedback) stays clean, then breaks up
  more firmly; high Feedback (little feedback) is warm from lower levels. At 0 % it is off: the measured
  clean stage.

**Drive** and **Feedback**'s steps span the measured hardware's positions 1 to 10 and go past them: step 1
is position 1, step 10 is what position 15 would be, evenly between (each step about 1.56 positions). Above
the measured 10 the stage is extrapolated: **Feedback** goes on along the way its last measured positions
went (more gain, the bass bump higher and firmer; its filters held to the measured span), **Drive** keeps
its top step's response and adds 2 dB a position. With **Saturation** above 0 % the stage holds its level
near its headroom however far they go; at 0 % the top steps are a lot of clean gain (up to about +36 dB at
1 kHz at Drive 10, Feedback 10), so bring **Output** or **Input** down.

**BASS** (the bass enhancer, ahead of the compressor or after it)

- **Bass** (0 to 100 %, 0 by default; on the Basic page too): the enhancer blended in. At 0 nothing of it
  runs and sunder sounds exactly as without it. Up to 100 %, the effect comes in under the dry sound with
  the enhancer's own blend law (bubr's **Dry/Wet** from -100 % to 0 %): on the way it is inverted against
  the dry sound at **Bass Freq**, so it first thins the band (around 50 % the low end is lighter than at 0),
  then builds it up, both at full level at 100 %. **Low Cut** comes in with it.
- **Bass Boost** (0 to 110 %, 55 % by default): the effect's level, 0.3 dB a percent, part into its
  clipper and part after it. The part into the clipper is taken back as the band gets loud, so it lifts the
  quieter passages most.
- **Bass Freq** (20 to 125 Hz, 40 Hz by default): the centre of the band the effect is made from.
- **Q-Factor** (0 to 100 %, 20 % by default): low spreads the band wide around **Bass Freq**, high narrows
  it onto **Bass Freq** (and drives the clipper harder there).
- **Intensity** (0 to 100 %, 50 % by default): the gain into the clipper, from about unity to some 36 dB
  more. The effect's ceiling does not move.
- **Bass Drive** (0 to 100 %, 20 % by default): the gain after the clipper, about 5.7 dB over its range.
  **x2** (off by default) doubles its stage: about 9.6 dB.
- **Oversample** (**Off** by default): the clipper at **2x** or **4x** (less aliasing when it is driven
  hard). It adds latency (the dry sound is delayed to match), which sunder reports while it is set, at
  **Bass** 0 too; with **Off** sunder's latency is the compressor's alone.
- **Bass Gate** (-60 to 0 dB, -30 dB by default): the threshold, on the input's peak (both channels
  together); below it the effect is turned down by **Gate Ratio** - 1 dB a dB (1 to 100, 4 by default; from
  about 50 it shuts). **Gate Attack** (0 to 300 ms, 0 by default) and **Gate Release** (20 to 500 ms, 100 ms
  by default) set how fast it opens and closes. The gate acts ahead of the clipper, so a gated signal also
  clips less.
- **Low Cut** (20 to 125 Hz, 20 Hz by default): a fourth-order high-pass on the effect, and on the dry sound
  as **Bass** comes in (at 100 % fully).

- **Bass Position** (**Pre** by default): where the enhancer sits. **Pre**: ahead of **Input** and the
  compressor, which then holds the added bass along with the rest (the enhancer's **Bass Gate** reads the
  input). **Post**: after the compressor and the germanium stage, on left and right, ahead of **Output**,
  **Soft Clip** and **Hard Clip**: the compressor does not hear it, so the bass comes out as set and does not
  pump the rest, and the clips still catch its peaks (its **Bass Gate** reads the compressed level). The
  latency is the same at both places. A change runs the new place unheard for 0.1 s and then crossfades
  over 30 ms, so it does not click; at **Bass** 0 without **Oversample** it changes nothing at all.

bubr's 0 dBFS output limiter (and its **Lookahead**) is not in sunder: the compressor follows, and the
limiter would have held the dry sound too as soon as **Bass** was above 0.

**OUTPUT**

- **Mode** (**Stereo** by default): **Stereo** or **Mid/Side** (the compressor and the output stage on the
  mid and the side; the bass enhancer works on left and right, ahead of them or after them).
- **Mix** (0 to 100 %, 100 % by default): the compressed signal against the uncompressed one, both through
  the output stage (and both with the bass effect in).
- **Output** (-24 to +24 dB, 0 by default).
- **Soft Clip** and **Hard Clip** (off by default): the end of the chain. Soft Clip rounds peaks off from
  -6 dBFS up to 0 dBFS; Hard Clip lets nothing above 0 dBFS out (after Soft Clip when both are on).

## Projects from pinchr and bubr

sunder keeps pinchr's plug-in IDs, so a project saved with pinchr opens with sunder (remove the old
`Pinchr.vst3`: the installers and the install scripts do it). Its settings carry over: **Drive** and
**Feedback** (0 to 10 in pinchr) go to the nearest step (pinchr's 5 is step 4; below 1, step 1), **Ratio**
(1 to 11) to the nearest whole number up to 10, and **Dirty** is ignored (always on now). The bass enhancer
starts off. pinchr's presets and settings copied from it load too.

Host automation recorded on pinchr's **Drive**, **Feedback** or **Ratio** cannot be converted: the host
plays the lanes back on the new ranges (a lane at pinchr's Feedback 5 now sets step 6, for one), so
redraw them after opening such a project.

Projects and presets saved with sunder 0.33.0 or earlier, and pinchr's, keep **Input** at the dB it had
(the range went from -21 to +21 dB to -48 to +48 dB in 0.34.0) and load with **Bass Position** at **Pre**,
so they sound as they did. Host automation of **Input** recorded before 0.34.0 is stored by the host on the
old range and plays back differently (a lane drawn from -21 to +21 dB now reads -48 to +48 dB, finer around
0 dB): redraw it if a project automates it.

bubr's projects do not open with sunder (bubr was a plug-in of its own): set the BASS section up by hand,
its controls are bubr's (Boost, Freq, Intensity, Q-Factor, Drive, x2, Gate, Ratio, Attack, Release, Low Cut,
Oversample) under the names above; bubr's Dry/Wet from -100 % to 0 % is **Bass** from 0 to 100 %.

## How it was measured

The compressor's reference was rendered in REAPER through scripted jobs (stepped sines for the compression
curves at every **Ratio** and **Curve**, tone steps and bursts for the timing, sweeps for the output stage
with a silent external sidechain so nothing compressed, single tones against fixed gain reductions for
the FET's second harmonic) and fitted in Python. The compression curves are its own, interpolated between
the measured **Ratio** positions; the output stage is a gain, three resonant low sections and a high peak
per **Drive** position, fitted at every **Feedback** position (within about 1 dB from 10 Hz to 16 kHz); the
detector is a fitted model of its timing. **Saturation** is sunder's own: the reference's output stage is
perfectly linear. The tables are generated: `src/core/CompShape.h`, `StageShape.h`, `DetectorShape.h`.

The bass enhancer is bubr's model: its band is four band-pass sections fitted at every **Bass Freq** (13
positions) and **Q-Factor** (11), with a clipper between the second and third whose ceiling at the
frequency is the same for every setting; a compressor on both channels together takes back Boost's share
as the band gets loud; a full-wave rectified copy of the band adds even harmonics; the gate is a peak
expander on the input; the blend and **Low Cut** follow the reference's law exactly. Against the
reference's own renders the band is within 0.33 dB and 0.9 degree from 6 to 480 Hz, the clipper and gains
within 0.5 to 0.7 dB rms per group (up to about 3 dB at extreme settings), the gate's curve within 0.3 dB
rms. Its tables are generated: `src/core/BandShape.h`, `WetShape.h`.
