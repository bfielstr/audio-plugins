# Wubr

Two bands whose gain and centre move with shapes you draw: wubs, pumps and sweeps, tempo-synced or free,
or run once as envelopes. Then Smacheratr at the end of the chain.

- **Bands**: two bell bands (1 green, 2 blue), each with Frequency, Width (octaves), Gain, and a Target:
  - **Gain**: the band's level is Gain + Depth x the shape (dB): +Depth at the top, -Depth at the bottom.
  - **Frequency**: the band sits at Gain and its centre moves Sweep octaves (half up at the top, half down).
  - **Both**: both at once.
- **Shape** (one per band): up to 8 points. Drag a point to move it, drag a line up or down to bend it,
  double-click to add or remove a point, right-click a line to straighten it. A dot shows where the band is.
- **Rate** (per band): **Sync** (1/32 to 4 bars, dotted and triplet; locked to the song position while the
  host plays) or **Free** (Hz), and a Phase offset.
- **Mode**:
  - **LFO**: the shapes run over and over.
  - **Envelope**: a trigger starts the shapes from the beginning; each runs to its **hold point**
    (Alt-click a point, or the Hold box) and stays. **MIDI**: a note starts them and letting go of the
    last note plays the rest of the shape. **Transient**: a hit in the audio (Sensitivity: how far it
    must jump over the recent level) starts them; they hold until the next hit.
- **Dry/Wet**, **Output**, then the end-of-chain **Smacheratr** with all its controls and displays.

The band display shows both bands as they are right now; drag a band's handle for its frequency (sideways)
and gain (up/down), the wheel for its width.
