# Widr

Extreme, cinematic stereo width, the kind heard in trailers and blockbuster mixes, that keeps the
mono fold. Every Widr in a session knows about the others, so a mix full of them shares the stereo
field instead of stacking width in the same place. Install instructions are in the
[top-level README](../../README.md).

![Widr](../../docs/widr/ui_widr.png)

## What it does

Width that is only a side signal (L − R) is heard as a diffuse, phasey wall around the whole sound.
The separation in film mixes comes from **different material on each side of a dry centre**, so
Widr builds two **voices** from the mid, one on the left and one on the right, each played a little
differently, like a double-tracked part, and a little later than the centre, so the centre keeps
its place in front. The two voices are unrelated to each other, so folding to mono only adds a
little energy and nothing cancels.

- **Width** (0–200 %): how loud the voices are. 0 % is a bypass (bit-exact), 100 % a clear left,
  centre and right, 200 % voices as loud as the centre.
- **Character** sets how each voice is made, from four generators with a different setting on
  each side:
  - a **second take**: the mid, band-limited and delayed (1–40 ms, the right side later than the
    left), wandering a little in time and pitch the way a second performance would;
  - a **decorrelator**: its own chain of all-passes per side, width without audible delay;
  - a **micro pitch shift**: a few cents down on the left and up on the right, slowly drifting;
  - **early reflections**: each side its own taps from a fixed pattern, the edge of a large room.

  *Tight* is decorrelated voices right beside the centre (no delay, no room); *Wide* is a second
  take 12 and 16 ms late on each side; *Epic* is later, detuned, wandering takes with big
  reflections, louder voices and the strongest contrast; *Surround* lets the room and the reverb
  lead, with the voices far out and late.
- **Contrast** keeps the centre and the sides apart, the trailer trick. In time, the voices duck
  under the hits in the mid and bloom between them, so drums and consonants stay dry and
  centred while the tails go wide. Across the spectrum, it gives way where the mid is strong for
  its neighbourhood (a voice's presence) and fills where the mid is thin. Each Character bakes in
  its own amount; Contrast scales it (default 50 %).
- **SPACE**: **Size** (the voices' delays and the spacing of the reflections), **Space** (a short
  FDN reverb, fed band-limited, with its own left and right outputs going to the voices),
  **Decay**, **Pre-Delay** and **Damping** (which also sets how dark the voices are; 5.5 kHz by
  default).
- **Air** lifts the side above ~6 kHz; **Beyond** lifts it around 4 kHz, which dips it in the far
  speaker so images seem to reach past the speakers.
- **Mono Below** (default 150 Hz): below it the output is mono. The side goes through an 8th-order
  Linkwitz-Riley high-pass and the mid through the matching all-pass, so the low end is centred and
  mid and side stay in phase above it (more than 80 dB of side rejection two octaves down).
- **Mono Guard**: in 24 third-octave bands Widr measures the mid, the side and what the voices add
  to each; per band it keeps the mono fold from gaining more than about 1.2 dB (at 100 %) and the
  side from getting too close to the mid. 0 % sets no limit.
- **Mono Check** listens to L + R; **Output** sets the level.

## Mix awareness

Every Widr publishes, once per block, its role, group, width, Space and its side and mid energy
in 24 bands, and reads the other Widrs of its **Group** (1–8):

- **Role**: Anchor > Support > Wide > Ambient. With others around, the role also pulls the width
  (an Anchor narrows and holds the centre, Wide and Ambient widen), by **Mix Aware**.
- Per band, where Widrs with a higher role already generate side, this one gives way in proportion
  to their share, by up to **Mix Aware** (0 % ignores the others, 100 % yields fully). The gains
  move over 200 ms, so nothing pumps.
- Two Widrs with the same role widen in opposite directions instead of identically.
- The outcome depends only on what the instances publish (and not on the order the host runs
  them in), so offline renders match playback.

The header says **Alone** or **3 in group 1**. Instances see each other only when the host runs
them in **one process**, the default in REAPER and Live. With REAPER's *run as separate process* or
bridging, Bitwig's sandboxing and the like, every Widr is alone and works as a standalone widener.
Widr never uses the network.

## Display

The **STAGE** shows the stereo field from above: you at the bottom, the speakers at the sides, this
Widr as an orange arc whose angle is its width and whose distance is its Space, the others of the
group in grey (*Widr 2 · Wide*). Drag an end of the orange arc for Width, drag up or down for Space,
double-click to reset them, Shift for fine steps. The strip under it shows the width kept per band
(blue where it gives way to the group). On the right: a **goniometer** of the output (mono is a
vertical line) and a **correlation** meter (+1 mono, 0 unrelated, below 0 it cancels in mono).

**Smacheratr** (bottom panel): the optional saturator at the end of the chain (off, Drive 0 dB). Here
it saturates the mid and the side apart, so pushing it does not narrow the image.

Latency: about 1.8 ms (85 samples at 48 kHz, the saturator's, constant whether it is on or off),
reported to the host for automatic compensation.
