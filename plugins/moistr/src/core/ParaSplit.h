// Moistr's PARA stage (0.30): the signal split into a low-pass and a high-pass path in parallel, as para does (para's
// two filters summed with the polarity that makes them meet flat), each path moving on the motion clock.
//
//   x -> LP (LP Freq, Linkwitz-Riley 4th order) x its level   -+
//     -> HP (its corner moving up from HP Freq, LR4) x its level -+-> Mix against x
//
// The paths are Linkwitz-Riley 4th order (two 2nd-order Butterworth TPT state-variable sections each: the corners can
// move every sample without zipper or instability, like para's own Svf), so with LP Freq = HP Freq and both levels
// at 0 dB they add up to an all-pass: flat. HP Freq above LP Freq opens a hollow between them (para's notch). Para's
// slope bank (6 .. 96 dB, Brickwall, crossfades between slopes) is more than a moving split needs; the 24 dB pair
// meets flat and moves cleanly, so it is used here.
//
// The movement, at phase ph (cycles of Rate, on the motion clock: the song position, or Loop Lock's segment):
//   the low-pass path's level  1 - LP Move x (0.5 - 0.5 cos (2 pi ph))            (in at the cycle's start, out in the middle)
//   the high-pass path's corner HP Freq x 2^(HP Move x (0.5 - 0.5 cos (2 pi (ph + 1/4))))  (up and down, a quarter ahead)
//   the high-pass path's level 1 - HP Level Move x (0.5 + 0.5 cos (2 pi ph))      (out when the low-pass path is in)
// A gesture lane may pull any of the three (Split LP, Split HP Freq, Split HP Level). Both channels share every corner
// and level: a mono input stays mono.
//
// Sub Guard: the low-pass path's level moves only above the guard's corner (its lows below Sub Guard Freq, split off
// with a Linkwitz-Riley 8th-order pair, stay at 0 dB), and the high-pass path's lows below it are left out (its corner
// never goes below HP Freq, 100 Hz at the least, so that is only its skirt). The guard fades over 20 ms.
//
// Off (Split On off and faded out) the stage is not run: the signal passes untouched.
#pragma once

#include "Dsp.h"
#include "Params.h"

namespace moistr {

class ParaSplit
{
public:
    static constexpr int kMaxTick = 16;

    void prepare (double sampleRate);
    void reset (const double* p, double phase);
    // where the three moving values are now (0 .. 1) before the gestures pull them, at phase ph (cycles of Rate)
    struct Shape
    {
        double lpLevel = 1.0, hpPos = 0.0, hpLevel = 1.0;
    };
    static Shape shapeAt (const double* p, double ph);
    // one tick of m samples, in place: the shape at the tick's end (the gestures' pulls included), the guard's corner
    // (Hz) and whether it is on
    void tick (const double* p, double* l, double* r, int m, const Shape& to, double guardHz, bool guard);
    bool running (const double* p) const { return p[kParaOn] >= 0.5 || fade > 0.0; }

    // for the tests and the display: now (at the last tick's end)
    double amount () const { return fade; }
    double lpGain () const { return lpNow; }
    double hpGain () const { return hpNow; }
    double hpHz () const { return hpHzNow; }
    double guardAmount () const { return guardFade; }

private:
    void targets (const double* p, const Shape& to, double guardHz, bool snap);

    double sr = 48000.0, tickSmooth = 0.1, fadeStep = 0.01;
    double fade = 0.0, guardFade = 0.0;
    double logLp = 0.0, logHp = 0.0, hpMove = 0.0, mix = 1.0, logGuard = 0.0;
    // at the tick's start and end: the corners' g, the levels and the mix
    double lpGPrev = 0.0, lpGNow = 0.0, hpGPrev = 0.0, hpGNow = 0.0, guardGPrev = 0.0, guardGNow = 0.0;
    double lpPrev = 1.0, lpNow = 1.0, hpPrev = 1.0, hpNow = 1.0, mixPrev = 1.0, mixNow = 1.0, hpHzNow = 250.0;
    dsp::Svf lp1[2], lp2[2], hp1[2], hp2[2];
    dsp::Lr8Split guardLp[2], guardHp[2];
};

} // namespace moistr
