# Detonatr

An explosion / impact designer, built on a sound designer's chain: clean the source, make it tonal
(a vocoder of household items and a disperser), squash it with a multiband, cut it to an extremely short
spike and a much quieter body, then drive the body back up loud with a saturator. Good impacts are tonal
rather than noisy; tonal can still be loud.

The five stages run in the order of the **strip** at the top: drag a stage sideways to move it, click it to
show its controls, click its light to turn it on or off. A stage that is off only delays, so the latency
(about 50 ms at 48 kHz, reported to the host) never changes, whatever the order or what is on.

- **Clean**: a spectral denoiser and dereverber (in the spirit of RX's De-noise and De-reverb).
  - **Denoise** learns the noise floor from the quiet parts and turns down what does not stand out of it, so the tones stay.
  - **Dereverb** turns down the part of each frequency that is only the decaying tail of what came before; the hit stays.
- **Tone**: makes the sound tonal.
  - **Resonators**: tuned modes of a **Material** (Glass, Metal Pot, Pipe, Wood, Bell, Bottle) on a **Root**
    (82.4 Hz, E1, by default: where most designed impacts sit), ringing for **Decay**, played by the input.
  - **Recordings**: up to four recordings of household items (drop an audio file on a slot, or click it). Each
    loops, and the input's bands play it (a vocoder), so its tone takes the input's shape. The project keeps
    their audio (up to 10 seconds each).
  - **Dry**: the input through the stage as it is (0 by default, so the output stays tonal; raise it for the raw grit).
  - **Disperse** / **Disperse Frequency**: a chain of all-pass filters that smears the phase into the chirpy
    disperser sound without changing the level.
- **Multiband**: Multidyn (every control and its display; no side-chain and no saturator of its own), with
  Live's OTT preset as its defaults. Its crossovers' **Slope** (6 dB/oct to Brickwall, 24 dB by default)
  sits beside the Splits, Soften's **Color** under Soften, and the **Sub** band's On and frequency under the
  RMS window; with the Sub band on, the display gets a lane for it at the bottom (its threshold, ratio,
  attack, release and output). See [Multidyn's README](../multidyn/README.md) for how they work. A project
  saved before the OTT defaults (Detonatr 0.6 and earlier) loads with the stage's gains moved so it sounds
  the same.
- **Transient**: at each hit, a **Spike** (0.1 to 20 ms) at full level, then down by **Drop** (0 to 48 dB)
  over **Fall**, held until the next hit. Hits are found with a 5 ms look-ahead, so the spike starts right
  on the hit. **Sensitivity**: how far the level must jump for a new hit.
- **Saturator**: Smacheratr with all its controls and displays, on and driven (+18 dB, soft clip) by default,
  to raise the dropped body back up dense and loud.
- **Dry/Wet** (the input lined up in time) and **Output**.

The **hit display** shows the input (grey) and output (orange) levels of the last second; click it to show
a quarter, half, one or two seconds.
