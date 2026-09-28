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

**Default settings** are a heavy upward-compression preset (Live's "OTT" pushed further) in
Character mode: 3 bands split at 88.3 Hz and 2.5 kHz (8 kHz for a fourth), Below -40.8 / -41.8 /
-40.8 dB at 1 : inf (a fourth band at 1 : 4.17), Above -33.8 / -30.2 / -35.5 dB at 1 : 66.7 / 1 : 66.7 /
1 : inf, OTT's attack/release times, Soft Knee and RMS on. The preset's gain staging (input +5.2 dB,
band outputs +24 / +9.1 / +11.3 / +11.7 dB, output -7 dB) is **baked into the processing**, so every
Input, Output and band gain control reads 0 dB at the default and trims around the preset. Use Amount
to dial the processing back.

**Mode**: *Base* is the plain device. *Character* detects more slowly (a 50 ms RMS window and a
rounded onset instead of 20 ms), has a wider knee (12 dB) and a release that slows down up to 3x the
deeper the gain change, so it moves like a character compressor rather than grabbing peaks.

**Smacheratr** (bottom panel, end of the chain): the Analog curve after the Output gain, with its
optional pre-limiter, Drive, Post Clip and Dry/Wet. Off by default, Drive 0 dB; its latency is
constant whether it is on or off.

**Pre-Limit** (off by default): a 1 ms look-ahead limiter on each band's input (after the band's
Input gain) with its **Ceiling** relative to the band's Above threshold (0 dB = right at it). When you
push hard into the thresholds, a transient would otherwise pass through at full level until the
attack catches up and then be squared by whatever follows (a saturator); the pre-limiter holds it
where the compressor settles anyway, so it reaches the saturator at the same level as the body and
gets the same rounding. Raise the Ceiling to let more of the transient through. Every band
always runs through the 1 ms look-ahead, and the saturator's oversampling adds its own, so the
latency (85 samples at 48 kHz) never changes; it is reported to the host.

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
  times), Mode (Base / Character), Soft Knee, Peak/RMS detection, Pre-Limit and its Ceiling.
- **Side-chain**: route another track into inputs 3/4 in REAPER; On, Gain, Dry/Wet (detector blend)
  and Listen.

Every control is an automatable parameter; hover any control for help (**?** toggles tooltips).
