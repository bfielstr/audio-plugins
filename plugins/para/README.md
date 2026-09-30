# Para

A high-pass and a low-pass filter **in parallel**, in the style of a morphing EQ. Install instructions are in the [top-level README](../../README.md).

## What it does

The two filters run side by side and their outputs are summed. With the high-pass above the low-pass
(the defaults: 300 Hz over 100 Hz, 24 dB per octave) that leaves a notch between them; with the two
meeting (resonance 0) the sum is flat, so nothing happens; pushing them further apart widens the
notch. The pairs are chosen so they meet flat at every slope: first order at 6 dB, Linkwitz-Riley
at 12, 24 and 36 to 96 dB, a quadrature Butterworth pair at 18 dB, and at Brickwall a 13th-order
elliptic pair that is doubly complementary (the two are made of the same two all-passes).

- **HIGH-PASS / LOW-PASS**: each filter's cutoff (at the root note), resonance and **gain**, down
  to -inf (at -inf only the other filter is heard).
- **SPLIT**: **Slope**, a drop-down (click for the list, or scroll over it): 6, 12, 18, 24, 36, 48,
  60, 72, 84 or 96 dB per octave, or **Brickwall** (-3 dB at the cutoff, 40 dB down a tenth of the
  way past it, 80 dB down from 0.8 x the cutoff on: 1.25 x for the low-pass), for both filters.
  Resonance raises the sections' Q; from 36 dB on it is spread over the sections so the peak at the
  cutoff is as high as at 24 dB (not a power of it); 6 dB and Brickwall, which have no section to
  raise, get a resonant bell at the cutoff instead (up to +26 dB). A new slope crossfades from the
  old one over 10 ms (the new filters first run over the last 30 ms of the input, so they fade in
  settled), so switching never clicks; as each slope's pair sums flat with its own phase, a tone
  can dip for those 10 ms while two slopes of very different phase cross. Projects saved with the
  three slopes of before (12 / 18 / 24 dB) keep their slope. **Link Res** (the low-pass uses the
  high-pass resonance), **Split** (moves the filters apart or together in semitones; automate it, or use the
  envelope), and the envelope triggered by every MIDI note (**Env** amount, **Attack**, **Decay**).
- **OUTPUT**: **Dry/Wet**, **Output** and **Movement**:
  - *Free*: the filters move independently.
  - *Vocal*: the filter you moved last leads. When it crosses the other, the other is pushed along
    and fades out, from 0 dB at the crossing to -inf **Fade** semitones past it (an octave by
    default), so one filter sweeps alone: a low-pass swept up takes the high-pass with it, a
    resonant high-pass swept down fades the low-pass out.
  - **Liquid** (switch): Vocal, plus Split swinging with the sweep: the filter you move overshoots
    the way it moves and flows back when it stops, for liquid, techy Reese movement.
- **Drive** (in OUTPUT): a drive in each filter's branch, apart from the saturator at the end:
  Smacheratr's Analog curve, 4x oversampled. Each has its own switch (**HP**, **LP**) and amount
  (**HP Drive**, **LP Drive**: 0 to +36 dB into the curve), so the lows and the highs saturate
  apart (a loud bass does not bend the top) or only one of them does. Where both sit is one
  setting: **Pre** (the default) drives each filter's input, so the filter shapes what its drive
  adds (the low-pass takes the top harmonics away); **Post** drives each filter's output (before
  its gain), so the harmonics stay. The dry part of Dry/Wet is never driven. The drives delay the
  sound by the same small amount on or off, Pre or Post (37 samples at 48 kHz), so the latency
  never changes; moving them between Pre and Post fades the sound out and back in for a moment
  (about 10 ms). Both drives on, stereo, take about 1.5% of a core. Projects from before the drive
  was per filter had one drive for both: they open with both drives as that one was (on, amount,
  Pre or Post). With Dry/Wet at 100% (the default) Pre sounds as before (the dry part used to be
  driven too); Post drove the sum of the filters (after Dry/Wet) and now drives each filter apart,
  which differs where both filters sound at once.
- **SMACHERATR** (end of the chain): the optional saturator every plug-in here has (off, Drive 0 dB).

The display shows the high-pass (orange), the low-pass (blue) and what you hear (white), over live
spectra of the input (grey) and the output (light). The curves are the digital filters as they
are (they bend near the top of the spectrum). A handle sits at its cutoff, as high as its resonant
peak: drag it sideways for the cutoff and up/down for the **resonance**; with **Drag Gain** on (the
button on the display) the gain moves with it; Alt-drag moves the gain alone (to the bottom: -inf);
double-click resets it. With audio running the display adds everything the engine does to the
settings (envelope, glide, Vocal), so edits show at once; the handles glow with the
envelope and the ENV meter shows it.

The cutoffs do not follow the notes; MIDI notes only trigger the Split envelope (route MIDI to
the plug-in for that). Para is also built into Smemplr.
