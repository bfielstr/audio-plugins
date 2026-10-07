# widr

widr makes a part very wide, the big left-centre-right spread you hear in film and trailer mixes,
while the mix still holds up in mono. Reach for it on pads, leads, vocal doubles, FX and anything that
should fill the sides without washing out the centre. Every widr in a project knows about the others,
so a mix full of them shares out the stereo field instead of stacking width in the same place. On a
whole mix, its cinema stage keeps the dialogue and the drums dry in the middle and pushes everything else
wide, deep and into a big dark hall. Install
instructions are in the [top-level README](../../README.md).

![widr](../../docs/widr/ui_widr.png)

## How to use it

1. Put widr on a track and pick a **Character**: **Tight**, **Wide**, **Epic** or **Surround**.
2. Set **Width** (or drag an end of the lit arc on the stage). Raise **Space** for a room around the
   voices.
3. With several Widrs in a project, give each a **Role** (**Anchor**, **Support**, **Wide**, **Ambient**)
   and the same **Group**. Use **Mono Check** to listen to the mono fold.
4. On a full mix or a stem, turn up **Cinema** for the trailer sound: the voice and the drums stay dry in
   the middle, everything else goes wide or past the speakers, with a deep low end and a big dark hall.
   Each element **lane** (**Voice**, **Bass**, **Hits**, **Tones**, **Ambience**) has its own place
   (**Centre**, **Wide** or **Beyond**) and **Width**. The *Cinema* presets are a good start.

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

**LANES** and **CINEMA**: the cinema stage, below.

**Dry** and **Wet** (the sliders under the panels): the input and what widr adds (the voices and the
reverb), in parallel, each from -inf to +6 dB.

## The cinema stage

Film and trailer mixes keep dialogue straight down the middle and push the music, the effects and the
room around it, very wide, over a deep low end, in a big room. widr can do that to a finished mix or a
stem: it listens for five kinds of element, puts each where you say, and leaves the rest of its work
(the voices, Space, Guard, Mono Below, the group) as it was.

**Cinema** (0 to 100 %, off by default) brings the whole stage in. At 0 % none of it runs: widr sounds
exactly as before, sample for sample, with no added latency. Above 0 % the input is split into lanes
(21 ms of latency, below), and Cinema blends the lane placement in, makes the voices up to 1.6 times
louder and leans them towards a cinema blend of the generators (stronger decorrelated voices, later takes,
wider reflections), and scales **Depth** and **Theatre**.

**The lanes.** Each moment of the sound, in each of several hundred narrow frequency bands, is shared out between five lanes
that always add back up to the input:

- **Voice**: speech and singing in the centre. A bin counts as voice when it is common to left and
  right, its level rises and falls at a syllable rate (2 to 8 times a second), it rises above that bin's
  steady floor (so a chord held under the voice stays out), it is not a hit, and it lies between about
  120 Hz and 7 kHz. The Voice lane is mono.
- **Bass**: what is left below about 100 Hz, fading out by 160 Hz.
- **Hits**: drums and other onsets, a sudden rise across many neighbouring bins, held while it rings.
- **Tones**: held notes, pads, leads, chords: what is steady.
- **Ambience**: what is diffuse (left and right unrelated) or noise-like: room, reverb, noise.

Each lane has a **Position** and a **Width**:

- **Centre**: dry and in the middle, with no voices and no hall. Width is how much of its own stereo it
  keeps (0 %: mono in the centre).
- **Wide**: the lane feeds widr's voices, by its Width.
- **Beyond**: it feeds them harder and gets cues that reach past the speakers: a decorrelated copy of
  it, a crosstalk cue (its side plus a slightly later, darker copy of itself, the way a crosstalk
  canceller widens) and its own side lifted. All three are pure side, so the mono fold never hears them,
  and Guard holds them under the mid like the voices.

By default Voice, Bass and Hits are in the Centre (Hits keeps 25 % of its stereo), Tones are Wide (80 %)
and Ambience is Beyond (100 %). Bass is always mono below **Mono Below**, wherever it is placed.

Two guards keep the centre clean: where the Voice lane holds a bin, and across the voice band while
someone speaks, the other lanes' widening gives way (by up to 10 dB), and the same where a hit is. So
what the separation could not take out of a voice or a kick is not smeared across the stereo field. The
price: music under dialogue gets a little narrower while the dialogue lasts.

**Depth** (50 % by default, scaled by Cinema): a deep, tight low end from the Bass lane. A sub an octave
down (the 40 to 120 Hz band drives a divider whose output is shaped by the band's level, so it stops with
the note) and a low shelf of up to 6 dB below about 90 Hz that backs off slowly when the lows are already
loud. It is mono, never touches the Voice lane, and backs off where the low end peaks near -4.4 dBFS.

**Theatre** (40 % by default, scaled by Cinema): a large, dark hall fed only by the Wide and Beyond lanes,
never the Centre ones, so a voice in the Centre stays dry. A theatre's early reflections (19 to 127 ms),
then a long hall (2.2 to 4.8 s, 30 to 80 ms before it starts, longer and later as Theatre goes up), dark
at the top and with some body in the low mids, like sound coming off a big screen. It goes through the
same per-band gains as the voices, so Guard and the group act on it too.

While Cinema is on, a soft limiter keeps the headroom: above -1 dBFS it turns the output down, and a soft
clip keeps it under 0 dBFS.

**What to expect from the separation.** It is a guess made from the sound alone, a few hundredths of a
second at a time, and it is soft: where two elements share a frequency at the same moment, that bin is
split between their lanes by how much of each is there, not handed to one. In the tests' mix (a centred
speech-like voice, kicks, a held chord and wide noise) 84 % of the voice lands in Voice, 96 % of the kick
in Bass (its body is below 160 Hz) and 1 % in Hits (the click), the chord 54 % in Tones and 20 % in
Ambience (23 % in Voice, where it shares bins with the voice), the noise 82 % in Ambience. With the guards,
the voice's side comes out about 28 dB under its mid and the kick's more than 60 dB, while the chord and the noise
get a side as strong as their mid or stronger. Some things it gets wrong: whispers, fricatives and long
sung notes are not voice (they go to Ambience or Tones); a centred pad with a 2 to 8 Hz tremolo can pass
for voice; a voice panned off the centre is not Voice; the onset of a held note counts as a hit for a few
milliseconds; Bass is a fixed frequency split, not an instrument. Turn a lane's Width down, or move it,
when it takes something it should not.

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
a vertical line) and a **CORRELATION** meter (+1 mono, 0 unrelated, below 0 it cancels in mono). With
Cinema on, five thin arcs on the stage are the lanes (Voice innermost, Ambience outermost), each as wide
as its Position and Width put it and lit by how much of the sound it holds right now.

**smacheratr** (bottom panel): the saturator every plug-in here can end with (off, Drive 0 dB). Here it
saturates the mid and the side apart, so pushing it does not narrow the image. While it is off it folds
to its header strips; click a strip (or switch it on) to open it. See [smacheratr](../smacheratr/README.md#at-the-end-of-the-other-plug-ins).

## Presets

The **Presets** menu in the header starts with **Init** (every control at its default) and has these
factory presets: *Cinema*: Dialogue Wide, Theatre, Trailer; *Width*: Ambient Pad, Epic, Subtle. Save your own with **Save As...** (a category
and tags are optional), filter the menu by tag, and use **Save as Default** to make every new widr
start from the current settings. The menu is described in the [top-level
README](../../README.md#presets).

## Latency

About 1.8 ms (85 samples at 48 kHz, the saturator's, the same whether it is on or off), reported to the
host for automatic compensation. Its share depends on its **Oversampling**: 85 samples at 48 kHz at 4x (the default), 80 at 2x and 48 (its 1 ms look-ahead) with Off; changing it changes the latency, and the host is told.

With **Cinema** above 0 % the lanes add 21.3 ms (1023 samples at 44.1 or 48 kHz, 2047 at 88.2 or 96 kHz),
also reported. Moving Cinema from 0 % to anything above switches the stage on and changes the latency, so
automate it between values above 0 %, not from 0 %. **Width** 0 % still bypasses everything (with Cinema
on, the input comes out delayed by the latency, untouched). Inside smemplr's rack widr has no cinema
stage.

## Credits

- Harmonic / percussive separation by median filtering: Derry Fitzgerald, "Harmonic/Percussive
  Separation using Median Filtering", DAFx 2010 (made causal here: a median over past frames only).
- Minimum statistics, for each bin's steady floor: Rainer Martin, "Noise Power Spectral Density
  Estimation Based on Optimal Smoothing and Minimum Statistics", IEEE Transactions on Speech and Audio
  Processing, 2001.
- Feedback delay networks (Space and Theatre): Jean-Marc Jot and Antoine Chaigne, "Digital Delay
  Networks for Designing Artificial Reverberators", AES Convention 1991.
- The Linkwitz-Riley crossover (Mono Below): Siegfried Linkwitz and Russ Riley.
