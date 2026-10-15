# sweepr

sweepr is an EQ whose three bands move. Each band is a bell (a boost or a cut around its frequency),
and each has an LFO that sweeps that frequency up and down, in time with the song or free. After the
bands the sound goes through a tape stage: drive into a fast leveller and a clipper that
holds the level, then softer highs as the tape **Speed** comes down. Put it on a bass and
a boost and a cut roll over the harmonics with the beat; the curve on the display moves with them. Install
instructions are in the [top-level README](../../README.md).

![sweepr](../../docs/sweepr/ui_sweepr.png)

## How to use it

1. Put sweepr on a bass, a pad or a loop and press play. A new instance is already moving: band A boosts
   13 dB at 110 Hz, band B cuts 15 dB at 320 Hz, and each LFO sweeps its band an octave up and down
   every beat (1/4, Sine), into tape. Band C starts flat (0 dB at 1 kHz): turn its **Gain** up or down
   for a third moving bell.
2. Drag the **A**, **B** and **C** handles on the display: sideways for the frequency, up and down for the
   gain; the mouse wheel over a handle (or Alt and drag) for Q. The bright line is the curve now, the
   dashed line the curve at the settings, the bar under each handle how far its LFO sweeps.
3. Set each band's LFO in its panel: **Sync** on for a note value (**1/32** to **4 Bars**, triplets and
   dotted ones too), off for a free **Rate** in Hz; **Shape**; **Depth** (how many octaves up and down;
   0 keeps the band still); **Phase**. **Link** in band B makes B follow A's LFO, offset by B's
   **Phase** (180: against A).
4. Shape the tape: **Drive** for more saturation (and, once it saturates, a level that hardly moves),
   **Speed** lower for a darker tape, **Compression** for a smoother, quieter, less distorted one (0: a
   hard clip). **Tape** off leaves the moving EQ alone.
5. **Mix** blends in the dry sound, **Output** sets the level.

Point at any control for help in the info box at the bottom (**?** also switches on hover tooltips).

## The display

The curve of the three bands from 20 Hz to 20 kHz, +18 to -18 dB, on the grid of a classic two-band EQ
screen (31 Hz to 16 kHz in octaves):

- the bright line over a faint fill: the curve now, with each band where its LFO has swept it, and a
  cinnabar dot on each band's frequency now;
- the dashed line: the curve at the knobs' settings (while the bands are swept away from it);
- the **A** and **B** handles: each band's **Freq** and **Gain**, to drag;
- the bar under a handle: how far its LFO sweeps (**Depth** octaves each way from **Freq**);
- at the top right: each band's settings, and while audio plays where they are now.

| On a handle | What it does |
|---|---|
| drag sideways | **Freq** |
| drag up and down | **Gain** |
| Alt and drag up and down | **Q** |
| mouse wheel | **Q** |
| Shift and drag | fine steps |
| double-click or right click | the band back to its default **Freq**, **Q** and **Gain** |

The curve follows the LFOs while the host runs sweepr (also during silence: the LFOs keep moving), and
shows the settings when it does not.

## Controls

**BAND A**, **BAND B** and **BAND C** (each the same, B with **Link**)

- **Freq** (30 Hz to 16 kHz; A 110 Hz, B 320 Hz, C 1 kHz by default): the bell's centre, where the LFO sweeps from
  (the LFO takes it further, up to just under half the sample rate).
- **Q** (0.25 to 16.25; A 1.00, B 1.60, C 1.00): the bell's width. Low Q is broad and gentle, high Q a narrow peak
  or notch. A boost keeps its width at any **Gain**; a cut widens the deeper it goes.
- **Gain** (-18.1 to +17.9 dB in the classic EQ's steps, 0 dB in the middle; A +13 dB, B -15 dB, C 0 dB): how far
  the bell boosts or cuts.
- **LFO** (on by default): the switch at the top of the panel turns the band's LFO on or off. Off, the
  band glides back to its **Freq** and stays there; its LFO settings are kept for when it comes back on.
  - **Sync** (on by default): on, the LFO's length is a note value locked to the song position; off,
    it runs free at **Rate**.
  - the note value (shown with **Sync** on; **1/32**, **1/16T**, **1/16**, **1/16D**, ... **1/2D**,
    **1 Bar**, **2 Bars**, **4 Bars**; **1/4** by default): one LFO cycle. T is a triplet, D dotted; a
    bar is four beats.
  - **Rate** (shown with **Sync** off; 0.01 to 40 Hz, 1 Hz by default): the free LFO's rate.
  - **Shape** (**Sine** by default): **Sine**, **Triangle**, **Saw Up**, **Saw Down**, **Square**,
    **Random** (a new value each cycle) or **Smooth Random** (gliding from one random value to the next).
  - **Depth** (-4 to +4 octaves, +1 by default): how far the LFO sweeps **Freq**, up and down. Negative
    sweeps the other way round (down where a positive one goes up); 0 keeps the band still.
  - **Phase** (0 to 360 degrees, 0 by default): where in its cycle the LFO is at the start (synced: its
    offset from the beat).
- **Link** (band B, off by default): B's LFO follows A's: A's **Sync**, note value or **Rate** and
  **Shape**, with B's **Phase** as the offset from A (180: B moves against A) and B's own **Depth**.
  B's own LFO settings are dimmed while it is on.

**TAPE**

- **Tape** (on by default): the tape stage after the three bands. Switching it fades over 10 ms.
- **Drive** (0 to 100 %, 75 % by default): the gain into the tape, -10 dB to +50 dB: clean and quieter
  at 0, saturated and held at a steady level from about a third up. There is no makeup: like a real
  tape, more drive is louder until it saturates.
- **Speed** (0 to 100 %, 80 % by default): the tape speed, a high shelf after the curve: at 100 % almost
  flat (-2 dB at the very top, a little lift around 10 kHz), at 80 % -10 dB from about 15 kHz, at 0 %
  -48 dB above 2 kHz.
- **Compression** (0 to 100 %, 35 % by default): a fast leveller before the curve (2 ms attack, 21 ms
  release): the higher, the lower it holds the level into the curve, so the softer, quieter and less
  distorted the tape. At 0 nothing holds it, and a high **Drive** clips to a square wave.

**OUTPUT**

- **Mix** (100 % by default): the effect against the untouched input, kept in time with each other (no
  comb filtering).
- **Output** (-24 to +12 dB, -4.4 dB by default): the overall level. At the defaults the tape holds a
  bass at about -13 dBFS RMS whether it comes in at -18 dBFS or at 0.

**smacheratr** (bottom panel): the saturator every plug-in here ends with. In sweepr it starts **off**,
since the tape stage is the saturator; switch it on for more (its gentlr is on, ready). See
[smacheratr](../smacheratr/README.md#at-the-end-of-the-other-plug-ins).

The **Basic** page has the display, **A Gain**, **B Gain**, **C Gain**, **Drive**, **A Depth**, **B Depth**,
**C Depth**, **Mix** and **Output**; **Advanced** shows everything.

## How it works

```
input -> bell A -> bell B -> 4x up -> tape: drive, leveller, clipper -> 4x down -> tape: DC blocker,
      the Speed shelf -> Mix (with the input, delayed as much) -> Output -> smacheratr (off by default)
```

**The bells.** Each band is the bell of a classic two-band digital EQ, measured step by step and matched
to within a few thousandths of a dB: its **Freq**, **Q** and **Gain** knobs have that EQ's ranges and
tapers, a boost keeps the width **Q** gives it at any **Gain**, a cut widens in proportion to its depth,
and a bell high up is squeezed towards half the sample rate (and widened) as that EQ's is. It is built as a
trapezoidal state-variable filter at the sample rate, and its coefficients are worked out again for
every sample from the swept frequency. That filter keeps a valid
state whatever its coefficients do, so even a 40 Hz **Square** sweeping four octaves neither zippers nor
clicks. The LFO's movement glides over 2 ms, so a **Square** or **Random** step sweeps quickly rather than
jumping. **Freq**, **Q** and **Gain** glide over 20 ms when you turn them.

**The LFOs.** Synced, an LFO's position is the song position in beats divided by the note value, plus
**Phase**: it never restarts, and playing from any point of the song sounds exactly the same as getting
there from the start. The random shapes take their values from the cycle's number, so they repeat with
the song too. With the host stopped, a synced LFO runs on at the last tempo from where it was. Free, an
LFO runs at **Rate** from the moment playback (or the plug-in) starts.

**The tape stage.** A classic tape saturation, measured knob by knob (sines from -72 to 0 dBFS, noise
and level steps) and matched: the gain and the odd harmonics of a 100 Hz sine within about 1 dB at every
level and setting measured. At four times the sample rate:

- **Drive**'s gain, -10 to +50 dB;
- a leveller on both channels together (the louder sets the gain of both): a peak follower (2 ms attack,
  21 ms release) and a gain of 1 / (1 + 3.21 x **Compression** x the level), so the level into the curve
  heads for a ceiling that **Compression** lowers;
- a sine clipper: the input's sine (slope 1 at 0, a gentle bend) up to the top of the sine, then flat at
  half of full scale. Its harmonics are odd (a third harmonic of -14 dB at the defaults, -9.5 dB fully
  clipped), with a hint of even ones (about -60 dB). It is anti-aliased (its antiderivative), so with the
  oversampling even a 7 kHz sine at -6 dBFS clipped to a square wave leaves no fold-back above -49 dB.

Then, at the normal rate: a 10 Hz DC blocker and **Speed**'s high shelf.

**Calibrating the bells and the tape.** Everything that decides a bell's shape (the knobs' ranges and
tapers, how Q and Gain combine) is in one file, `src/core/BellShape.h`; the filter and the display are
both made from it. The tape's (Drive's range, the leveller, the curve, Speed's shelf) is in
`src/core/TapeShape.h`. `sweepr_analyze` (built with the plug-in) compares recordings:

```sh
sweepr_analyze dry.wav processed.wav    # processed's response against dry per 1/12 octave, and the null depth
sweepr_analyze dry.wav reference.wav --preset settings.txt --static
    # dry through sweepr at the settings (a preset file's format) against another EQ's render of it:
    # both responses, their difference per 1/12 octave and how deep the two renders null
```

## Presets

The **Presets** menu in the header starts with **Init** (every control at its default) and has these
factory presets: *Sweep*: Clean Sweep, Default, Slow Wobble, Talking Mids; *Tape*: Dark Tape. Save your
own with **Save As...**, and use **Save as Default** to make every new sweepr start from the current
settings. The menu is described in the [top-level README](../../README.md#presets).

## Latency

sweepr's own latency is constant whatever the settings, so automating anything never moves the sound in
time: the oversampling's filters, 37 samples at 48 kHz. smacheratr at the
end adds its own (85 samples at 48 kHz at its default 4x Oversampling, also while it is off, so switching
it never changes the latency). The host is told and compensates.

## CPU

The defaults take about 2 % of one core at 48 kHz stereo, about 3 % with smacheratr at the end on
(measured as process CPU time, the best of three runs, on the build machine).
