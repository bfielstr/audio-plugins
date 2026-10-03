# Dropr

A transient designer you draw: every hit in the audio starts a level shape you draw, which drops the
hit and brings it back over the Length. Install instructions are in the
[top-level README](../../README.md).

## What it does

Dropr listens for hits (drums, plucks, the attack of any note) and, on each one, plays the shape
drawn in its display as a level: the top of the display is 0 dB (untouched), the bottom is
**-Depth** dB. After the shape the level stays at the shape's last point until the next hit, so a
shape ending at the top lets everything between the hits through, and one ending lower keeps the
tails down.

- **The shape**: up to 8 points from the hit (left) to the end of the Length (right), each segment
  straight or bent. Drag a point to move it (the first and last stay at the ends), drag a line to
  bend it, double-click to add a point or remove one, right-click a line to straighten it. The
  default drops the hit to the bottom and brings it back up over the Length. While a shape runs, a
  dot shows where it is, the display reads the gain applied, and **HIT** flashes on every hit.
- **Sensitivity** (2–24 dB, default 6 dB): how far the sound has to jump over its recent level to
  count as a hit. The detector compares a fast peak level (0.1 ms up, 10 ms down) with a slow one
  (50 ms), on both channels together; sounds below -50 dBFS never trigger.
- **Retrigger** (10–1000 ms, default 50 ms): the shortest time between two hits. A hit during the
  shape starts it again; hits closer than this are ignored.
- **Length** (5–2000 ms, default 150 ms): how long the shape takes.
- **Depth** (0–60 dB, default 30 dB): the level at the bottom of the display. At 0 dB nothing
  changes, whatever the shape.
- **Pre** (0–5 ms, default 2 ms): starts the shape up to 5 ms before the hit, so its first part
  lands on the hit's attack itself instead of just after it.
- **Mix** (dry / wet) and **Output** (±24 dB).

The gain is eased over 0.5 ms, so a shape restarting on a new hit never clicks.

**Smacheratr** (bottom panel): the optional saturator at the end of the chain (off, Drive 0 dB).

## Latency

The audio (and the dry signal, so any Mix lines up) goes through a fixed 5 ms look-ahead: the hit is
seen 5 ms before it is heard, which is what lets Pre start the shape early. With the end saturator's
latency (about 1.7 ms, always in the path) that is about 6.7 ms in all. It never changes and is
reported to the host for automatic compensation.
