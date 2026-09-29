# Para

A high-pass and a low-pass filter **in parallel**, in the style of a morphing EQ. Install instructions are in the [top-level README](../../README.md).

## What it does

The two filters run side by side and their outputs are summed. With the high-pass above the low-pass
(the defaults: 300 Hz over 100 Hz, 24 dB per octave) that leaves a notch between them; with the two
meeting (resonance 0) the sum is flat, so nothing happens; pushing them further apart widens the
notch. The pairs are chosen so they meet flat at every slope (Linkwitz-Riley at 12 and 24 dB, a
quadrature Butterworth pair at 18 dB).

- **HIGH-PASS / LOW-PASS**: each filter's cutoff (at the root note), resonance and **gain**, down
  to -inf (at -inf only the other filter is heard).
- **SPLIT**: **Slope** (12 / 18 / 24 dB per octave), **Link Res** (the low-pass uses the high-pass
  resonance), **Split** (moves the filters apart or together in semitones; automate it, or use the
  envelope), and the envelope triggered by every MIDI note (**Env** amount, **Attack**, **Decay**).
- **OUTPUT**: **Dry/Wet**, **Output** and **Movement**:
  - *Free*: the filters move independently.
  - *Vocal*: the filter you moved last leads. The other fades as it comes within a minor third
    (with the high-pass at 300 Hz, from a low-pass of about 250 Hz), is pushed along once crossed and
    is at -inf a minor third past, so one filter sweeps alone: a low-pass swept up takes the high-pass
    with it, a resonant high-pass swept down fades the low-pass out.
  - **Liquid** (switch): Vocal, plus Split swinging with the sweep: the filter you move overshoots
    the way it moves and flows back when it stops, for liquid, techy Reese movement.
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
the plug-in for that). Para is also built into Smempler.
