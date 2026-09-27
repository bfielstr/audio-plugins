# Multidyn

A multiband dynamics processor modelled on Ableton Live's **Multiband Dynamics** (Live manual §29.26):
upward and downward compression *and* expansion on **1 to 4** independent frequency bands, each
with an upper (Above) and lower (Below) threshold. Install instructions are in the [top-level README](../../README.md).

![Multidyn](../../docs/multidyn/ui_multidyn.png)

## How it works

For each band the detector level drives two gain computers:

| Block | Ratio > 1 ("quieter") | Ratio < 1 ("louder") |
|---|---|---|
| **Above** the upper threshold | downward compression | upward expansion |
| **Below** the lower threshold | downward expansion | upward compression |

Bands are split with Linkwitz-Riley (24 dB/oct) crossovers, phase-aligned so they sum back flat
(within 0.05 dB, for any band count) when nothing is processed.

## Controls

- **Bands** (top): 1, 2, 3 or 4 bands; 1 makes Multidyn a single full-range processor. The
  crossover knobs X1–X3 sit between the band columns (defaults 120 Hz, 1.2 kHz, 6 kHz).
- **Per band** (columns below the display): On (bypasses the band's dynamics and gains), Solo, Input
  and Output gain, plus the attack/release or threshold/ratio pair selected with T / B / A.
- **Display**: thin bars = input level, thick bars = output level. Drag a block edge to move a
  threshold; drag inside a block up (louder) or down (quieter) to set its ratio. **Cmd/Ctrl** = all
  bands, **Alt/Option** = Above and Below together, **Shift** = fine, **double-click** = 1:1.
- **T / B / A** (top right): choose which pair each band column shows: attack/release, Below
  threshold/ratio or Above threshold/ratio.
- **Global**: Output, Amount (0% = every ratio acts as 1:1), Time (scales all attack/release
  times), Soft Knee, Peak/RMS detection.
- **Side-chain**: route another track into inputs 3/4 in REAPER; On, Gain, Dry/Wet (detector blend)
  and Listen.

Every control is an automatable parameter; hover any control for help (**?** toggles tooltips).
