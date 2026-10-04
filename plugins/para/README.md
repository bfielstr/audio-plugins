# Para

A high-pass and a low-pass filter **in parallel**, in the style of a morphing EQ. Install instructions are in the [top-level README](../../README.md).

## What it does

The two filters run side by side and their outputs are summed. With the high-pass above the low-pass
(the defaults: 300 Hz over 100 Hz, 24 dB per octave) that leaves a notch between them; with the two
meeting (resonance 0) the sum is flat, so nothing happens; pushing them further apart widens the
notch. The pairs are chosen so they meet flat at every slope: first order at 6 dB, Linkwitz-Riley
at 12, 24 and 36 to 96 dB, a quadrature Butterworth pair at 18 dB, and at Brickwall a 13th-order
elliptic pair that is doubly complementary (the two are made of the same two all-passes).

- **HIGH-PASS / LOW-PASS**: each filter's cutoff (at the root note), resonance and **gain**, from
  -inf (only the other filter is heard) to +12 dB, and under the gain its **Lock** (High-Pass Gain
  Lock, Low-Pass Gain Lock). With a filter's Lock on its gain never goes above 0 dB: the knob, the
  display's handle (Drag Gain and Alt-drag too) stop at 0 dB, switching the Lock on
  brings a higher gain down to 0 dB, and the engine caps the gain at 0 dB whatever the parameter says
  (automation included). The high-pass's Lock is on by default, the low-pass's off.
- **SPLIT**: **HP Slope** and **LP Slope**, a drop-down each (click for the list, or scroll over it):
  6, 12, 18, 24 (the default), 36, 48, 60, 72, 84 or 96 dB per octave, or **Brickwall** (-3 dB at the
  cutoff, 40 dB down a tenth of the way past it, 80 dB down from 0.8 x the cutoff on: 1.25 x for the
  low-pass), each filter its own. Resonance raises the sections' Q; from 36 dB on it is spread over the
  sections so the peak at the cutoff is as high as at 24 dB (not a power of it); 6 dB and Brickwall,
  which have no section to raise, get a resonant bell at the cutoff instead (up to +26 dB). With both
  slopes the same, the two filters meeting at one frequency sum flat; with different slopes they do
  not (each keeps its own phase; the high-pass is summed with its own slope's polarity) and the display
  draws the sum as it is. A filter's new slope crossfades from its old one over 10 ms (the new filter
  first runs over the last 30 ms of its input, so it fades in settled), each filter on its own, so
  switching never clicks; as each slope has its own phase, a tone where both filters sound can dip for
  those 10 ms while two slopes of very different phase cross. Projects saved with the three slopes of
  before (12 / 18 / 24 dB) keep their slope. **Link Res** (the low-pass uses the high-pass resonance),
  **Split** (moves the filters apart or together in semitones; automate it, or use the envelope), and
  the envelope triggered by every MIDI note (**Env** amount, **Attack**, **Decay**).
- **OUTPUT**: **Dry/Wet**, **Output** and **Movement**:
  - *Free*: the filters move independently.
  - *Vocal*: the filter you moved last leads. A low-pass swept up past **Dip** (80 Hz by default)
    takes the high-pass up with it and fades it out; a high-pass swept down past the low-pass pushes
    the low-pass down and fades it out from the crossing; so one filter ends up sweeping alone. The
    fade runs over **Fade** semitones (30 by default, 1 to 60) evenly in dB: from 0 dB in a straight
    line (in dB) towards -36 dB at the end of the Fade (-9 dB a quarter of the way, -18 dB half way,
    -27 dB at three quarters), and over its last 10 % also under a raised-cosine half window from 1
    to 0, so it is -32.4 dB at 90 % and silent at the end (and past it). The gain is smoothed (20 ms),
    so a sweep never steps. Split also swings with the sweep: the filter you move overshoots the way
    it moves and flows back when it stops, for liquid, techy Reese movement (this was the Liquid
    switch). **Floor**: the low-pass never goes below it (40 Hz by default), so the sub stays.
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

Projects saved before the separate slopes open sounding the same: the low-pass
gets the slope both filters had, the high-pass's Gain Lock is on unless the saved high-pass gain is
above 0 dB (then it is off, so nothing gets quieter), the low-pass's is off, and Fade keeps the
semitones it was saved with (its range was 1 to 36 then, so its stored value is converted; the new
default of 30 semitones is only for new instances: an existing instance keeps its Fade, 12 semitones
if it was never changed, and so its fade's length, though the fade's shape is now the even-in-dB one
above instead of the equal-power curve of before, which held near 0 dB longer and plunged at the
end).

The display shows the high-pass (a solid copper line, HP), the low-pass (dashed, LP) and what you
hear (the bright line), over live spectra of the input (dim) and the output (copper). The curves are the digital filters as they
are (they bend near the top of the spectrum). A handle sits at its cutoff, as high as its resonant
peak: drag it sideways for the cutoff and up/down for the **resonance**; with **Drag Gain** on (the
button on the display) the gain moves with it; Alt-drag moves the gain alone (to the bottom: -inf);
double-click resets it. With audio running the display adds everything the engine does to the
settings (envelope, glide, Vocal), so edits show at once; the handles glow with the
envelope and the ENV meter shows it.

The cutoffs do not follow the notes; MIDI notes only trigger the Split envelope (route MIDI to
the plug-in for that). Para is also built into Smemplr.
