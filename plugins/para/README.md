# Para

A high-pass and a low-pass filter **in parallel** that follow the notes you play, in the style of a
morphing EQ. Install instructions are in the [top-level README](../../README.md).

## What it does

The two filters run side by side and their outputs are summed. With the high-pass above the low-pass
(the defaults: 822 Hz over 185 Hz, 24 dB per octave) that leaves a notch between them; with the two
meeting (resonance 0) the sum is flat, so nothing happens; pushing them further apart widens the
notch. The pairs are chosen so they meet flat at every slope (Linkwitz-Riley at 12 and 24 dB, a
quadrature Butterworth pair at 18 dB).

- **HIGH-PASS / LOW-PASS**: each filter's cutoff (at the root note), resonance and **gain**, down
  to -inf (at -inf only the other filter is heard).
- **SPLIT**: **Slope** (12 / 18 / 24 dB per octave), **Link Res** (the low-pass uses the high-pass
  resonance), **Split** (moves the filters apart or together in semitones; automate it, or use the
  envelope), and the envelope triggered by every MIDI note (**Env** amount, **Attack**, **Decay**).
- **TRACKING**: **Key**, **Transpose**, **Bend** and **Root**: both cutoffs track the played note. At
  100 % Key the filters sit on the same harmonics of every note; the root note (C3 by default) is
  where the cutoffs sit as set, and Transpose and the pitch bend range apply to the played note.
  Route MIDI to the plug-in (in REAPER: a MIDI item or a track sent to the FX's MIDI input).
- **OUTPUT**: **Dry/Wet**, **Output** and **Movement**:
  - *Free*: the filters move independently.
  - *Vocal*: the filter you moved last leads. When it crosses the other, the other is pushed along
    to its cutoff and fades out (to -inf an octave past), so one filter sweeps alone: a low-pass
    swept up takes the high-pass with it, a resonant high-pass swept down fades the low-pass out.
- **SMACHERATR** (end of the chain): the optional saturator every plug-in here has (off, Drive 0 dB).

The display shows the high-pass (orange), the low-pass (blue) and what you hear (white), over live
spectra of the input (grey) and the output (light). The curves are the digital filters as they
are (they bend near the top of the spectrum). A handle sits at its cutoff, as high as its resonant
peak: drag it sideways for the cutoff and up/down for the **resonance**; with **Drag Gain** on (the
button on the display) the gain moves with it; Alt-drag moves the gain alone (to the bottom: -inf);
double-click resets it. With audio running the display adds everything the engine does to the
settings (tracking, envelope, glide, Vocal), so edits show at once; the handles glow with the
envelope and the ENV meter shows it.

Para is also built into Smempler, where it tracks the sampler's own notes and root note.
