# locus

locus cleans up or thickens the low end. Turn **Contrast** up and the main bass notes and kick hits
come forward while the mud between them drops away, for a clearer, punchier low end. Turn it down and
the low end evens out, for more weight and sustain. The level of the range stays steady either way, so
you hear the change in focus, not a change in loudness. Reach for it on a bass, a kick and bass bus or
a full mix whose bottom end sounds woolly or thin. Install instructions are in the
[top-level README](../../README.md).

![locus](../../docs/locus/ui_locus.png)

## How to use it

1. Set the range with **Low** and **High** (30 to 300 Hz by default), or drag the range edges in the
   display. Everything outside it passes through untouched.
2. Turn **Contrast** up for clarity and punch, or down for weight and density. Drag up or down inside
   the range in the display to do the same.
3. Pick **Punchy** for transients and impact, or **Smooth** for sustain and weight. **Solo** plays only
   the range, so you can hear what locus is working on.

Point at any control for help in the info box at the bottom (**?** also switches on hover tooltips).

## How it works

locus splits the low end into dozens of narrow bands (about 12 Hz each) and compares each band's level
with its neighbourhood: the level of nearby bands over the recent past. A compressor compares against a
fixed threshold instead.

- Positive **Contrast**: bands weaker than their surroundings are pushed down, including the mud between
  the harmonics of a bass note, so the dominant events come into focus.
- Negative Contrast: everything moves closer in level.
- The loudness of the range is kept steady. **Gain** sets the level of the range.
- **Mode**: **Punchy** uses fast time constants, **Smooth** slow ones.
- **Low** / **High**: the range. Outside it the signal is bit-exact apart from the latency.
- **Solo**: plays only the range.
- **Output**: the overall level.

## The display

The display shows the input spectrum (a faint copper body), the output (a bright line) and the gain
applied per band. Drag the range edges to move them, drag inside the range sideways to move it or up and
down to set Contrast, and double-click to reset Contrast.

**smacheratr** (bottom panel): the saturator every plug-in here can end with (off, Drive 0 dB).

## Presets

The **Presets** menu in the header starts with **Init** (every control at its default) and has these
factory presets: *Low End*: Punchy, Thick. Save your own with **Save As...** (a category and tags
are optional), filter the menu by tag, and use **Save as Default** to make every new locus start
from the current settings. The menu is described in the [top-level README](../../README.md#presets).

## Latency

About 87 ms (4096 samples at 48 kHz for the analysis, plus the saturator's 1.7 ms), reported to the
host for automatic compensation.
