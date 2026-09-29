# Levlr

The spectrum in four bands that touch, each with its own level: lift the lows, dip the low mids, push
the lot into the Smacheratr at the end. The crossovers are minimum-phase Linkwitz-Riley splits, so the
phase turns around each one; that turn is part of the sound (there is no linear-phase mode).

- **Bands**: four (1 red, 2 amber, 3 green, 4 blue), side by side from 20 Hz to 20 kHz: band 1 ends where
  band 2 starts, and so on. Each has a **Gain** (-24 to +24 dB), **Mute** and **Solo** (a soloed band is
  heard even when muted; with several soloed, all of them are).
- **Crossovers**: three, at 120 Hz, 1 kHz and 6 kHz by default. Each stays at least 1/6 octave from its
  neighbours; one set past them pushes the ones above it up. Moving one glides it there (no clicks).
- **Slope**: 12, 24 (default) or 48 dB/oct (Linkwitz-Riley 2, 4, 8). Every slope adds back to a flat
  level with all bands at 0 dB; each turns the phase its own way: 12 gently (and parts the bands softly,
  so a band's lift spills a little into its neighbours), 48 the most (and the cleanest split). Changing
  it fades out and back in over a few milliseconds.
- **Output**, then the end-of-chain **Smacheratr** with all its controls and displays (off by default,
  Pre-Limit on).

The display shows the four bands as coloured columns, each filled to its level, the whole response in
white (the bands' filters added up as the engine adds them, so the steps between levels look as they
sound) and the output's spectrum behind (tilted 4.5 dB/oct around 1 kHz, so a mix reads level).

- Drag a band up or down for its level (Shift: fine); drag the line between two bands sideways to move
  that crossover.
- Double-click or right-click a band to reset its level, a crossover to reset its frequency.
- **M** and **S** at the top of a band mute and solo it.
- The mouse wheel on a band sets its level; on a crossover, the slope.

How it works: the input splits at crossover 1 into band 1 and the rest, the rest at crossover 2, and so
on. Bands 1 and 2 then go through the all-passes of the crossovers above them (the all-pass a split's
two sides add up to), so all four at 0 dB sum to one all-pass of the input: flat in level, shifted in
phase (at 24 dB/oct about 4 ms of delay at 120 Hz). The filters are Multidyn's trapezoidal SVF
sections.
