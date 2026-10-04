# deepr

deepr makes a bass or a reese sound deeper without making it louder. While the sub plays, it dips the
low mids just above it, so the sub becomes the strongest part of each note; between notes the low mids
come back. It also folds the sub to mono, so detuned layers stop cancelling each other down there. Reach
for it when a bass sounds big on its own but thin or boxy in the mix. deepr adds no latency of its own.
Install instructions are in the [top-level README](../../README.md).

![deepr](../../docs/deepr/ui_deepr.png)

## How to use it

1. Put deepr on the bass. Set **Split** where the sub ends (100 Hz by default).
2. Set **Thresh** so the dip starts when the sub plays. **Listen** **Sub** helps: it plays the sub band
   alone.
3. Set how deep the dip goes with **Depth**, and where with **Dip** and **Width**. **Listen** **Cut**
   plays what the dip takes out.

Point at any control for help in the info box at the bottom (**?** also switches on hover tooltips).

## Controls

deepr splits the signal at **Split** into the sub and the rest. The sub's level is the key: while it
plays, the low mids of the rest are dipped; when it stops, they come back.

**DIP**

- **Depth** (0 to 12 dB, 6 dB by default): how far the low mids are dipped at most.
- **Dip** (Dip Frequency, 80 to 800 Hz, 250 Hz by default) and **Width** (Dip Width, 0.5 to 4 octaves,
  1.5 by default): the band that is dipped, a bell exactly Depth deep at its centre.
- **Thresh** (Threshold, -60 to 0 dB, -30 dB by default): the sub level where the dip starts. It grows
  in proportion to how far the sub is over it and reaches the full Depth 12 dB over the threshold.
- **Attack** (1 to 100 ms) and **Release** (20 to 1000 ms): how fast the dip follows the sub coming in
  and lets go after it.

**SUB**

- **Split** (Sub Split, 40 to 200 Hz, 100 Hz by default): a Linkwitz-Riley crossover, 24 dB/oct.
- **Mono** (Mono Sub, 100 % by default): how much of the sub's side is folded into its mid. Above Split
  the stereo image is untouched.
- **Sub** (Sub Gain, ±6 dB): the sub band's level.
- **Listen**: **Off** (the result), **Sub** (the sub band alone) or **Cut** (what the dip takes out),
  to set Split, Dip and Thresh by ear.

**OUTPUT**: **Mix** (dry / wet) and **Output** (±12 dB).

With Depth at 0 the output is flat at every frequency: the split and the sum make an all-pass, and the
dry signal goes through the same all-pass so any Mix lines up.

**smacheratr** (bottom panel): the saturator every plug-in here can end with (off, Drive 0 dB).

## Why it sounds deeper

Low frequencies mask the band just above them, and the ear is least sensitive down in the sub, so what a
listener hears as a bass's weight is largely the sub against the low mids. Dipping the low mids only
while the sub plays makes the sub the loudest part of each note, without raising the track's level or
driving the sub any harder. Between notes the low mids of everything else are left alone.

## Presets

The **Presets** menu in the header starts with **Init** (every control at its default) and has these
factory presets: *Bass*: Deeper, Reese, Subtle. Save your own with **Save As...** (a category and
tags are optional), filter the menu by tag, and use **Save as Default** to make every new deepr
start from the current settings. The menu is described in the [top-level
README](../../README.md#presets).

## Latency

None of deepr's own: everything runs sample by sample (IIR filters and an envelope follower). The only
latency is the end saturator's, about 1.7 ms, which is always in the path, so switching the saturator on
or off never changes it. Its share depends on its **Oversampling**: 85 samples at 48 kHz at 4x (the default), 80 at 2x and 48 (its 1 ms look-ahead) with Off; changing it changes the latency, and the host is told. It is reported to the host for automatic compensation.
