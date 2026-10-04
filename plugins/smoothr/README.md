# Smoothr

A limiter for the master bus or a bus that puts a smooth, clean low end before the last dB of loudness.
A plain limiter pulls its gain down in a millisecond or two for every peak and lets it back up in tens
of milliseconds, well inside one period of a bass note (20 ms at 50 Hz): the bass comes out multiplied
by a wobbling gain, rough on held notes and pumping under every kick and snare. Smoothr gives the lows
a slow gain of their own and lets the highs take the fast peaks.

The chain: **Input** -> **Smacheratr** (saturation first, on and mild) -> **Character** (a dip in the
low mids when they get loud) -> the **limiter** -> the ceiling.

## The limiter

- **Input**: gain into the chain, -12 to +24 dB. The output sits at the ceiling, so this is how much
  louder it gets.
- **Ceiling**: -12 to 0 dB, -1 dB by default. No sample ever goes over it, and the peaks between the
  samples are held too (true peak, checked at 8 points a sample): programme material stays within
  about 0.01 dB of it on a 16x meter (full-band white noise, with as much energy at the very top of
  the spectrum as anywhere, can read up to 0.9 dB over). Hard-limited material peaks
  about 0.09 dB under the ceiling (the room left for the peaks between the checked points). There is no
  gain after it: the ceiling is the output level.
- **Release**: 5 ms to 1 s, 80 ms by default: how fast the highs let go. The lows let go slower on
  their own: never faster than 60 ms (three periods of a 50 Hz bass), at least twice the Release, and
  longer with Smooth.
- **Auto**: program-dependent release (on by default): what has been limited for a while lets go
  slowly, a lone peak quickly, so dense material does not pump.
- **Smooth**: 0 to 100 %, 50 % by default: how much the lows are kept out of the limiting. The lows
  always move slowly; Smooth sets how much they give way for the highs. At 0 % they follow the whole
  signal (slowly), so the balance never moves but the lows take their full part. At 50 % a kick click
  or a snare is left to the highs (they may take up to 12 dB more on a peak), and when the highs are
  held down for a while the lows come down three quarters as far, slowly, so the balance holds. At
  100 % the lows are left alone: louder and smoother still, with the highs taking all of the
  limiting (the mix gets bass-heavier the harder it is pushed).

How it works:

- **A linear-phase split** near 200 Hz (6 dB down there, 0.6 dB down at 100 Hz, 99 % of a 55 Hz bass
  in the lows): the lows are the input through eight moving averages (four, twiced: 2B - B^2), the
  highs are the input minus the lows. Both are real (no phase shift), so with the two gains equal the
  output is the input exactly, only delayed: a limiter that is not limiting passes the signal bit for
  bit, and there is no phase turn around the split.
- **The lows' gain**: a 10 ms look-ahead (the gain glides into a peak along an S-curve half a period of
  a 50 Hz bass long), a 2 dB soft knee, the slow release. Its window is longer than half a bass
  period, so on a held note it sees every crest and holds still instead of riding the waveform. It
  turns the lows down for their own level, for the whole signal with the highs counted at Smooth's
  weight, and at least by Smooth's share of what the highs have been turned down lately.
- **The highs' gain**: a 1.5 ms look-ahead and the Release, worked out from what the lows already do:
  the most the highs may keep with the lows at their gain, checked at the samples and 7 points between
  each two (the highs interpolated with a 24-tap windowed sinc per point).
- Each gain comes from a look-ahead window with a minimum hold, a release that only ever lowers it and
  averages that only mix values from inside the window, so the gain on each sample is never above what
  that sample needs. A last clamp at the ceiling would catch anything left over; in the tests it never
  has to.
- **Latency**: constant, 1051 samples at 48 kHz (21.9 ms): the split 404, the lows' look-ahead 479,
  the highs' 71 and the interpolation 12, plus the saturator's 85. Reported to the host.

What the tests measure: a 55 Hz sine pushed 10 dB over a -1 dB ceiling comes out with 0.02 % THD,
against 2.4 % for a plain 1.5 ms look-ahead limiter with the same release (40 Hz: 0.07 % against
3.5 %). A 56 Hz bass with kick clicks pushed to +12 dB: the sidebands the clicks put around the bass
are 29 - 30 dB under it at Smooth 50 % against 14 - 15 dB for the plain limiter turned to the same
loudness (60 - 76 dB under at 100 %). The price: on dense material pushed hard Smoothr is quieter than
a plain limiter (a loud little mix pushed 9 dB in: 1.7 dB quieter at 50 %, the same at 100 %).

## Character

A dip in the low mids, somewhere in 80 - 250 Hz, just before the limiter, that only opens when the low
mids get loud. It is Smacheratr's **Gently** (Clarity) at work: it listens through Gently's band (run
twice, so only the low mids open it, not a loud bass or a loud upper mid), sets its cut with Gently's
law (3 dB for every 5 dB the band is over -18 dBFS), and cuts the way Gently cuts, through a resonant
band-pass so the dip has no bump either side of it (Gently's own band, subtracted, would lift the bass
under it). Turned up, the dip gets deeper (up to 6 dB), wider (0.9 to 1.5 octaves) and slides down
(175 to 140 Hz). At 0 it is off and the signal passes bit for bit; 30 % by default.

Why there: the low mids carry a lot of peak level for how little they add. Turned down when they pile
up, they stop eating the limiter's headroom, so the limiter has to pull the lows down less, and less
often. It sits after the saturator, so it also catches the harmonics the saturator adds there (a 55 Hz
bass's third is at 165 Hz).

## Smacheratr

Saturation first, then limiting: the Analog curve rounds the tops of the peaks, so the limiter has less
to catch. All of Smacheratr's controls and displays, before the limiter. On by default and mild:
**Drive** 0 dB (the curve only shapes what goes over half scale), **Dry/Wet** 50 %, **Pre-Limit** off
(it is a fast full-band limiter, the very thing that roughens the lows; the limiter after it does that
job smoothly). Off, it only delays the signal, so the latency never changes.

## The display

The last five seconds, scrolling: the output (a copper body) and above it what the limiter took off its
input (lighter), filled up from the bottom, and the gain reduction hanging from the top on the same dB scale: the lows' (cinnabar,
solid) apart from the highs' (pale copper, dashed). The dashed line is the ceiling. At the right: the input and output meters
(the input lights its peak colour over the ceiling) with their peak holds, and the reduction now on the lows and
the highs; click them to clear the holds.
