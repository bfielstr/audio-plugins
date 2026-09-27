# Multidyn

A multiband dynamics processor modelled on Ableton Live's **Multiband Dynamics** (Live manual §29.26):
upward and downward compression *and* expansion on **1 to 4** independent frequency bands, each
with an upper (Above) and lower (Below) threshold. Install instructions are in the [top-level README](../../README.md).

![Multidyn](../../docs/multidyn/ui_multidyn.png)

## How it works

Ratios are written the way Live writes them, **1 : x**. x > 1 always *compresses* (shrinks the
dynamic range), x < 1 expands, and **1 : inf** limits:

| Block | 1 : x with x > 1 | 1 : x with x < 1 |
|---|---|---|
| **Above** the upper threshold | downward compression / limiting (loud gets quieter) | upward expansion (loud gets louder) |
| **Below** the lower threshold | upward compression (quiet gets louder, up to +36 dB) | downward expansion (quiet gets quieter) |

Each block has its own level envelope that feeds the static curve: Above uses Attack when the level
rises and Release when it falls, Below the other way round (as described in Live's manual). Attack
and Release are the time to *reach* the new amount of compression (about 95 % of the change), and
because the envelope works on the level, how fast the gain moves also depends on how far the signal
is past the threshold. Upward compression never lifts a signal past the Below threshold, so hits
after silence don't jump.

**Default settings are Live's "OTT" preset**: 3 bands split at 88.3 Hz and 2.5 kHz, Below
-40.8 / -41.8 / -40.8 dB at 1 : 4.17, Above -33.8 / -30.2 / -35.5 dB at 1 : 66.7 / 1 : 66.7 / 1 : inf,
input +5.2 dB, output +10.3 / +5.7 / +10.3 dB, attack/release 47.8/282, 22.4/282, 13.5/132 ms,
Soft Knee and RMS on. Use Amount to dial it back.

Bands are split with Linkwitz-Riley (24 dB/oct) crossovers, phase-aligned so they sum back flat
(within 0.05 dB, for any band count) when nothing is processed.

## Controls

- **Bands** (top): 1, 2, 3 or 4 bands; 1 makes Multidyn a single full-range processor. The
  crossover knobs X1–X3 sit between the band columns (defaults 120 Hz, 1.2 kHz, 6 kHz).
- **Per band** (columns below the display): On (bypasses the band's dynamics and gains), Solo, Input
  and Output gain, plus the attack/release or threshold/ratio pair selected with T / B / A.
- **Display**: thin bars = input level, thick bars = output level. Drag a block edge to move a
  threshold; drag inside a block up (louder) or down (quieter) to set its ratio. **Cmd/Ctrl** = all
  bands, **Alt/Option** = Above and Below together, **Shift** = fine, **double-click** = 1:1. The
  number in a block is the gain it applies at its extreme (silence for Below, 0 dB for Above).
- **T / B / A** (top right): choose which pair each band column shows: attack/release, Below
  threshold/ratio or Above threshold/ratio.
- **Global**: Output, Amount (0% = every ratio acts as 1:1), Time (scales all attack/release
  times), Soft Knee, Peak/RMS detection.
- **Side-chain**: route another track into inputs 3/4 in REAPER; On, Gain, Dry/Wet (detector blend)
  and Listen.

Every control is an automatable parameter; hover any control for help (**?** toggles tooltips).
