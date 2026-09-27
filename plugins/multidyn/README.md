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

**Default settings** are a four-band upward-compression preset (Live's "OTT" pushed further):
bands split at 88.3 Hz, 2.5 kHz and 8 kHz, Below -40.8 / -41.8 / -40.8 / -40.8 dB at 1 : inf (the top
band 1 : 4.17), Above -33.8 / -30.2 / -35.5 / -35.5 dB at 1 : 66.7 / 1 : 66.7 / 1 : inf / 1 : inf,
input +5.2 dB, output +24.0 / +9.1 / +11.3 / +11.7 dB, OTT's attack/release times, Soft Knee and RMS on,
and Output -7 dB to leave room for a saturator after it. Use Amount to dial it back.

Bands are split with Linkwitz-Riley (24 dB/oct) crossovers, phase-aligned so they sum back flat
(within 0.05 dB, for any band count) when nothing is processed.

## Controls

The layout follows Live's device: the **Split** column on the left holds the band names, On
(bypasses the band's dynamics and gains) and Solo, with the crossover frequency fields between the
lanes; then the per-band **Input** knobs, the lanes, the per-band **Output** knobs and the global
controls on the right.

- **Bands** (top): 1, 2, 3 or 4 bands; 1 makes Multidyn a single full-range processor.
- **Value fields** beside each lane: Below threshold and ratio (left), Above threshold and ratio and
  Att/Rel (right). Drag a field up/down (Shift: fine), double-click to reset.
- **Display**: thin bars = input level, thick bars = output level. Drag a block edge to move a
  threshold; drag inside a block up (louder) or down (quieter) to set its ratio. **Cmd/Ctrl** = all
  bands, **Alt/Option** = Above and Below together, **Shift** = fine, **double-click** = 1:1. The
  number in a block is the gain it applies at its extreme (silence for Below, 0 dB for Above).
- **Global**: Output, Amount (0% = every ratio acts as 1:1), Time (scales all attack/release
  times), Soft Knee, Peak/RMS detection.
- **Side-chain**: route another track into inputs 3/4 in REAPER; On, Gain, Dry/Wet (detector blend)
  and Listen.

Every control is an automatable parameter; hover any control for help (**?** toggles tooltips).
