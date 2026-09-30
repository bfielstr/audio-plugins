# Deepr

Makes a bass or a reese sound deeper by contrast, without making it louder and without latency of its
own. Install instructions are in the [top-level README](../../README.md).

## What it does

Deepr splits the signal at **Split** (a Linkwitz-Riley crossover, 24 dB/oct, 40–200 Hz, default
100 Hz) into the sub and the rest. The sub's level is the key: while it plays, the low mids of the
rest are dipped; when it stops, they come back.

- **Depth** (0–12 dB, default 6 dB): how far the low mids are dipped at most.
- **Dip Freq** (80–800 Hz, default 250 Hz) and **Dip Width** (0.5–4 octaves, default 1.5): the
  band that is dipped, a bell exactly Depth deep at its centre.
- **Threshold** (-60–0 dB, default -30 dB): the sub level where the dip starts. It grows in
  proportion to how far the sub is over it and reaches the full Depth 12 dB over the threshold.
- **Attack** (1–100 ms) and **Release** (20–1000 ms): how fast the dip follows the sub coming in
  and lets go after it.
- **Mono Sub** (default 100 %): how much of the sub's side is folded into its mid, so a reese's
  detuned layers stop cancelling each other down there. Above Split the stereo image is untouched.
- **Sub Gain** (±6 dB): the sub band's level.
- **Listen**: **Off** (the result), **Sub** (the sub band alone) or **Cut** (what the dip takes
  out), to set Split, Dip Freq and Threshold by ear.
- **Mix** (dry / wet) and **Output** (±12 dB).

With Depth 0 the output is flat at every frequency: the split and the sum make an all-pass, and the
dry signal goes through the same all-pass so any Mix lines up.

**Smacheratr** (bottom panel): the optional saturator at the end of the chain (off, Drive 0 dB).

## Why it sounds deeper

Low frequencies mask the band just above them, and the ear is least sensitive down in the sub (the
equal-loudness contours), so what a listener hears as a bass's weight is largely the sub against the
low mids. Dipping the low mids only while the sub plays makes the sub the loudest part of each note,
without raising the track's level or driving the sub any harder; between notes the low mids of
everything else are left alone.

Latency: none of Deepr's own (everything runs sample by sample: IIR filters and an envelope
follower). The only latency is the end saturator's, about 1.7 ms, which is always in the path so it
never changes; it is reported to the host for automatic compensation.
