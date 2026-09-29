# Widr

Extreme, cinematic stereo width, the kind heard in trailers and blockbuster mixes, that keeps the
mono fold. Every Widr in a session knows about the others, so a mix full of them shares the stereo
field instead of stacking width in the same place. Install instructions are in the
[top-level README](../../README.md).

![Widr](../../docs/widr/ui_widr.png)

## What it does

Widr only ever **adds side signal** (L − R). The mid (L + R) passes untouched, so folding the output
to mono gives back the input's mid: nothing cancels, whatever the settings.

- **Width** (0–200 %): how much width is added. 0 % is a bypass (bit-exact), 100 % fills the
  speakers, 200 % reaches past them.
- **Character** sets the blend of four width generators, each made from the mid:
  - a **Haas pair**: the mid, band-limited and delayed (0.1–25 ms), added to one channel and
    subtracted from the other, so the combs are complementary and the centre stays put;
  - a **decorrelator**: a cascade of all-passes, width without audible delay or tone change;
  - a **micro pitch spread**: a few cents down on the left and up on the right, slowly drifting;
  - **early reflections**: 8 or 16 taps from a fixed stereo pattern, the edge of a large room.

  The Characters are built to sound clearly different: *Tight* is clean decorrelation only (no delay,
  no room); *Wide* puts a Haas pair up front; *Epic* has big reflections, a detuned spread, a louder
  side and the strongest contrast; *Surround* lets the reverb and a large room lead.
- **Contrast** keeps the centre and the sides apart, the trailer trick. In time, the added width
  ducks under the hits in the mid and blooms between them, so drums and consonants stay dry and
  centred while the tails go wide. Across the spectrum, it gives way where the mid is strong for
  its neighbourhood (a voice's presence) and fills where the mid is thin. Each Character bakes in
  its own amount; Contrast scales it (default 50 %).
- **SPACE**: **Size** (the Haas delay and the spacing of the reflections), **Space** (a short FDN
  reverb, fed mostly from the side and added to the side only, so it reads as width rather than
  distance), **Decay**, **Pre-Delay** and **Damping**.
- **Air** lifts the side above ~6 kHz; **Beyond** lifts it around 4 kHz, which dips it in the far
  speaker so images seem to reach past the speakers.
- **Mono Below** (default 150 Hz): below it the output is mono. The side goes through an 8th-order
  Linkwitz-Riley high-pass and the mid through the matching all-pass, so the low end is centred and
  mid and side stay in phase above it (more than 80 dB of side rejection two octaves down).
- **Mono Guard**: in 24 third-octave bands Widr measures the mid and side; where the side gets
  close to the mid, the added width in that band backs off (a floor under each band's
  correlation). 0 % sets no limit.
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

**Smacheratr** (bottom panel): the optional saturator at the end of the chain (off, Drive 0 dB).

Latency: about 1.8 ms (85 samples at 48 kHz, the saturator's, constant whether it is on or off),
reported to the host for automatic compensation.
