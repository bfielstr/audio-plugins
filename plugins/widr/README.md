# widr

widr makes a part very wide, the big left-centre-right spread you hear in film and trailer mixes,
while the mix still holds up in mono. Reach for it on pads, leads, vocal doubles, FX and anything that
should fill the sides without washing out the centre. Every widr in a project knows about the others,
so a mix full of them shares out the stereo field instead of stacking width in the same place. Install
instructions are in the [top-level README](../../README.md).

![widr](../../docs/widr/ui_widr.png)

## How to use it

1. Put widr on a track and pick a **Character**: **Tight**, **Wide**, **Epic** or **Surround**.
2. Set **Width** (or drag an end of the lit arc on the stage). Raise **Space** for a room around the
   voices.
3. With several Widrs in a project, give each a **Role** (**Anchor**, **Support**, **Wide**, **Ambient**)
   and the same **Group**. Use **Mono Check** to listen to the mono fold.

Point at any control for help in the info box at the bottom (**?** also switches on hover tooltips).

## How it works

Width that is only a side signal (L - R) sounds like a diffuse, phasey wall around the whole sound. The
separation in film mixes comes from different material on each side of a dry centre. So widr builds two
**voices** from the mid, one on the left and one on the right, each played a little differently, like a
double-tracked part, and a little later than the centre, so the centre keeps its place in front. The two
voices are unrelated to each other, so folding to mono only adds a little energy and nothing cancels.

## Controls

**WIDTH**

- **Width** (0 to 200 %): how loud the voices are. 0 % is a bypass (bit-exact), 100 % a clear left,
  centre and right, 200 % voices as loud as the centre.
- **Character** sets how each voice is made, from four generators with a different setting on each
  side:
  - a **second take**: the mid, band-limited and delayed (1 to 40 ms, the right side later than the
    left), wandering a little in time and pitch the way a second performance would;
  - a **decorrelator**: its own chain of all-passes per side, width without audible delay;
  - a **micro pitch shift**: a few cents down on the left and up on the right, slowly drifting;
  - **early reflections**: each side its own taps from a fixed pattern, the edge of a large room.

  **Tight** is decorrelated voices right beside the centre (no delay, no room). **Wide** is a second take
  12 and 16 ms late on each side. **Epic** is later, detuned, wandering takes with big reflections,
  louder voices and the strongest contrast. **Surround** lets the room and the reverb lead, with the
  voices far out and late.
- **Contrast** keeps the centre and the sides apart. In time, the voices duck under the hits in the mid
  and bloom between them, so drums and consonants stay dry and centred while the tails go wide. Across
  the spectrum, it gives way where the mid is strong for its neighbourhood (a voice's presence) and
  fills where the mid is thin. Each Character has its own amount; Contrast scales it (50 % by default).
- **Air** lifts the side above about 6 kHz. **Beyond** lifts it around 4 kHz, which dips it in the far
  speaker so images seem to reach past the speakers.
- **Mono Below** (150 Hz by default): below it the output is mono. The side goes through an 8th-order
  Linkwitz-Riley high-pass and the mid through the matching all-pass, so the low end is centred and mid
  and side stay in phase above it (more than 80 dB of side rejection two octaves down).
- **Guard** (Mono Guard): in 24 third-octave bands widr measures the mid, the side and what the voices
  add to each. Per band it keeps the mono fold from gaining more than about 1.2 dB (at 100 %) and the
  side from getting too close to the mid. 0 % sets no limit.

**SPACE**: **Size** (the voices' delays and the spacing of the reflections), **Space** (a short reverb,
fed band-limited, with its own left and right outputs going to the voices), **Decay**, **Pre-Delay** and
**Damping** (which also sets how dark the voices are; 5.5 kHz by default).

**MIX**: **Role**, **Aware** (Mix Aware), **Group**, **Mono Check** (listens to L + R) and **Output**.

**Dry** and **Wet** (the sliders under the panels): the input and what widr adds (the voices and the
reverb), in parallel, each from -inf to +6 dB.

## Sharing the stereo field

Every widr publishes, once per block, its role, group, width, Space and its side and mid energy in 24
bands, and reads the other Widrs of its **Group** (1 to 8):

- **Role**: Anchor > Support > Wide > Ambient. With others around, the role also pulls the width (an
  Anchor narrows and holds the centre, Wide and Ambient widen), by **Aware**.
- Per band, where Widrs with a higher role already make side signal, this one gives way in proportion
  to their share, by up to **Aware** (0 % ignores the others, 100 % yields fully). The gains move over
  200 ms, so nothing pumps.
- Two Widrs with the same role widen in opposite directions instead of identically.
- The result depends only on what the instances publish (not on the order the host runs them in), so
  offline renders match playback.

The header says **Alone** or **3 in group 1**. Instances see each other only when the host runs them in
**one process**, the default in REAPER and Live. With REAPER's *run as separate process* or bridging,
Bitwig's sandboxing and the like, every widr is alone and works as a standalone widener. widr never
uses the network.

## Display

The **Stage** shows the stereo field from above: you at the bottom, the speakers at the sides, this widr
as a lit (cinnabar) arc whose angle is its width and whose distance is its Space, and the others of the
group as dashed copper arcs (*widr 2 · Wide*). Drag an end of the lit arc for Width, drag up or down for
Space, double-click to reset them, Shift for fine steps. The strip under it shows the width kept per
band (outlined where it gives way to the group). On the right: a **Goniometer** of the output (mono is
a vertical line) and a **CORRELATION** meter (+1 mono, 0 unrelated, below 0 it cancels in mono).

**smacheratr** (bottom panel): the saturator every plug-in here can end with (off, Drive 0 dB). Here it
saturates the mid and the side apart, so pushing it does not narrow the image.

## Presets

The **Presets** menu in the header starts with **Init** (every control at its default) and has these
factory presets: *Width*: Ambient Pad, Epic, Subtle. Save your own with **Save As...** (a category
and tags are optional), filter the menu by tag, and use **Save as Default** to make every new widr
start from the current settings. The menu is described in the [top-level
README](../../README.md#presets).

## Latency

About 1.8 ms (85 samples at 48 kHz, the saturator's, the same whether it is on or off), reported to the
host for automatic compensation. Its share depends on its **Oversampling**: 85 samples at 48 kHz at 4x (the default), 80 at 2x and 48 (its 1 ms look-ahead) with Off; changing it changes the latency, and the host is told.
