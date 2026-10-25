# pinchr

pinchr is a germanium compressor: a FET compressor with a choice of six knees, into a germanium output
stage whose **Feedback** sets its gain and a resonant bass bump. Its compression curves, timing, detector
and output stage were measured setting by setting from a classic germanium compressor's software model,
the base for a sound of its own. Install instructions are in the [top-level README](../../README.md).

## How to use it

1. Put pinchr on a drum bus, a bass, a vocal or a mix and press play. A new instance is the model's own
   defaults: **Ratio** 4, **Germ Soft**, **Attack** and **Release** 3, **Drive** 5, **Feedback** 5.
2. Turn **Input** up until the curve on the display shows the gain reduction you want. There is no makeup
   gain: more **Input** is louder until the compressor holds it, so trim with **Output**.
3. **Ratio** sets how hard it works (a lower threshold and a steeper knee as it goes up), **Curve** the
   knee's shape, from **R Soft** (the gentlest) to **Zener Hard** (the sharpest and earliest).
4. **Attack** and **Release**: 0 is fast, 10 slow. Short peaks release quickly, long loud passages more
   slowly, as on the hardware.
5. **Drive** and **Feedback** colour it: **Feedback** up gives more level and a bass bump (about 20 to 90
   Hz, higher with more **Drive**) with a steep cut below it; the display shows the response. **Saturation**
   sets how readily the germanium stage distorts (0 % for the clean, measured stage).
6. **Mix** for parallel compression: both the compressed and the uncompressed signal go through the
   output stage.

Point at any control for help in the info box at the bottom (**?** also switches on hover tooltips).

## The display

- At the left, the output stage's response (**Drive** and **Feedback**) from 10 Hz to 20 kHz, relative to
  its gain at 1 kHz, with that gain at the top.
- At the right, the compression curve of the **Curve** at the **Ratio**: the level out against the level
  into the compressor, -50 to +12 dB, with a dot at the level reaching it and the gain reduction while
  audio plays.

## Controls

**COMPRESSOR**

- **Curve** (**Germ Soft** by default): the knee's diodes. **R Soft** (a resistor), **Germ Soft** (one
  germanium diode), **Germ Med** (five), **Silicon Med** (one silicon diode), **Silicon Hard** (two),
  **Zener Hard**.
- **SC Filter** (**Off** by default): takes the lows out of what the compressor hears, **30**, **60**,
  **90**, **150** or **300 Hz**, as broad shelves. The detector always ignores the deepest lows.
- **Dirty** (off by default): the FET without its distortion compensation, a second harmonic that grows
  with the level and falls as the gain reduction deepens.
- **Link** (on by default): one gain for both channels, set by the louder.
- **Input** (-21 to +21 dB, 0 by default): the level into the compressor.
- **Ratio** (1 to 11, 4 by default): the detector's gain and the knee together.
- **Attack** and **Release** (0 to 10, 3 by default): 0 fast, 10 slow.
- **Ext SC** (off by default): the compressor listens to the sidechain input (route one to pinchr in the
  host) instead of the sound it compresses; **SC Gain** (-24 to +24 dB, 0 by default) is its level.

**GERMANIUM**

- **Drive** (0 to 10 in steps, 5 by default): the output stage's gain switch, about 3 dB a step.
- **Feedback** (0 to 10, 5 by default): the output stage's feedback. 0 is quiet (about -37 dB) and flat;
  up gives more gain and the resonant bass bump.
- **Saturation** (0 to 100 %, 50 % by default): the output stage's germanium transistor, an asymmetric
  saturation, mostly second harmonic, inside the stage's feedback loop. Higher sets it in at lower levels
  (from about +6 dBFS out of the stage just above 0 %, -3 dBFS at 50 %, -12 dBFS at 100 %), so more **Drive**
  also drives it harder. **Feedback** shapes it: low Feedback (much feedback) stays clean, then breaks up
  more firmly; high Feedback (little feedback) is warm from lower levels. At 0 % it is off: the measured
  clean stage.

**OUTPUT**

- **Mode** (**Stereo** by default): **Stereo** or **Mid/Side** (the compressor and the output stage on the
  mid and the side).
- **Mix** (0 to 100 %, 100 % by default): the compressed signal against the uncompressed one, both through
  the output stage.
- **Output** (-24 to +24 dB, 0 by default).
- **Soft Clip** and **Hard Clip** (off by default): the end of the chain. Soft Clip rounds peaks off from
  -6 dBFS up to 0 dBFS; Hard Clip lets nothing above 0 dBFS out (after Soft Clip when both are on).

## How it was measured

The reference was rendered in REAPER through scripted jobs (stepped sines for the compression
curves at every **Ratio** and **Curve**, tone steps and bursts for the timing, sweeps for the output stage
with a silent external sidechain so nothing compressed, single tones against fixed gain reductions for
**Dirty**) and fitted in Python. The compression curves are its own, interpolated between the eleven
**Ratio** positions; the output stage is a gain, three resonant low sections and a high peak per
**Drive** step, fitted at every **Feedback** position (within about 1 dB from 10 Hz to 16 kHz); the
detector is a fitted model of its timing. **Saturation** is pinchr's own: the reference's output stage
is perfectly linear. The tables are generated: `src/core/CompShape.h`,
`StageShape.h`, `DetectorShape.h`.
