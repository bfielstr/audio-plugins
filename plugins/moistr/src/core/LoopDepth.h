// Loop Depth (0.30): with Loop Lock on, each modulated value's movement inside the loop region stretched toward that
// modulator's full range, so a short region can still sweep all the way.
//
// Every modulated value the motion clock drives goes through here on its way to the sound (a channel each: the bells'
// sweeps, the High Shelf's orbit, the bands' lifts and the crossovers' drift, Liquid's place, the gesture's lanes and
// the slots, Wobble, PARA's three paths). While Loop Lock is on, the lowest and highest value each channel went through
// in a pass of the region are kept; at the pass's end they become the range the next passes stretch from (the region
// plays the same values every pass, so after one pass the range is the region's). A value v in that range lo .. hi is
// taken toward the same place in the channel's full range: v + depth x (full (v) - v), full (v) = fullLo + (fullHi -
// fullLo) x (v - lo) / (hi - lo). The ranges glide to a new pass's (kGlideSec), from the full range (no change) the
// first time, and Depth glides too, so nothing steps; a channel that does not move in the region is left as it is.
// The values are the same for both channels of the stereo signal (one modulation for both), so the stretch is linked.
//
// Depth 0 (and Loop Lock off once it has glided out): every value passed through untouched, bit for bit.
#pragma once

#include "Gesture.h"
#include "Movement.h"
#include "Params.h"

#include <algorithm>
#include <cmath>

namespace moistr {

class LoopDepth
{
public:
    // the channels
    enum : int
    {
        kChBell = 0,                                         // the bells' sweeps (0 .. 1), A .. H
        kChShelfU = kChBell + kNumBells,                     // the shelf's orbit: its corner (0 .. 1)
        kChShelfV,                                           //   and its gain (0 .. 1)
        kChLift,                                             // the bands' lifts (0 .. 1): pass x band x (rises, the Low band's dips)
        kChXDrift = kChLift + kMaxPasses * kMaxBands * 2,    // Mid X's and High X's drift (-1 .. 1), per pass
        kChLiquid = kChXDrift + kMaxPasses * 2,              // Liquid's place (0 .. 1)
        kChLane,                                             // the gesture's lanes (0 .. 1)
        kChSlot = kChLane + kMaxSceneLanes,                  // the 0.27 slots (0 .. 1)
        kChWobble = kChSlot + kNumGestureSlots,              // Wobble's dip (0 .. 1)
        kChParaLp,                                           // PARA: the low-pass path's level (0 .. 1)
        kChParaHpPos,                                        //   the high-pass path's corner (0 .. 1)
        kChParaHpLevel,                                      //   and level (0 .. 1)
        kChannels
    };
    static int liftChannel (int pass, int band, bool dips) { return kChLift + (pass * kMaxBands + band) * 2 + (dips ? 1 : 0); }
    static int xDriftChannel (int pass, int x) { return kChXDrift + pass * 2 + (x - 1); }
    static constexpr double kGlideSec = 0.05;  // the ranges and Depth glide
    static constexpr double kStill = 1e-4;     // a range narrower than this (of the full one): not moving, left alone

    void reset (double depthNow, bool on)
    {
        recording = on;
        depth = depthNow;
        for (auto& c : ch)
            c = {};
    }

    // this tick: Loop Lock on or off, Depth's setting; m samples at sr
    void tick (bool on, double setting, int m, double sr)
    {
        recording = on;
        const double target = on ? std::clamp (setting, 0.0, 1.0) : 0.0;
        const double k = 1.0 - std::exp (-(double)m / (kGlideSec * sr));
        depth += (target - depth) * k;
        if (std::fabs (depth - target) < 1e-6)
            depth = target; // (exactly 0 again: untouched)
        if (depth <= 0.0)
            return;
        for (auto& c : ch)
            if (c.valid)
            {
                c.lo += (c.loTarget - c.lo) * k;
                c.hi += (c.hiTarget - c.hi) * k;
            }
    }

    // a pass of the region has ended: what each channel went through is the range from now on
    void endPass ()
    {
        for (auto& c : ch)
        {
            if (c.seenLo <= c.seenHi)
            {
                if (!c.valid)
                {
                    // (the first time: from the full range, where the stretch changes nothing)
                    c.lo = c.fullLo;
                    c.hi = c.fullHi;
                    c.valid = true;
                }
                c.loTarget = c.seenLo;
                c.hiTarget = c.seenHi;
            }
            c.seenLo = 1e300;
            c.seenHi = -1e300;
        }
    }

    // a pass cut short (Loop Lock just on, a reset): what it went through forgotten, the ranges kept
    void restart ()
    {
        for (auto& c : ch)
        {
            c.seenLo = 1e300;
            c.seenHi = -1e300;
        }
    }

    // a value of channel `i` (its full range fullLo .. fullHi): kept while Loop Lock is on, stretched by Depth
    double operator() (int i, double v, double fullLo = 0.0, double fullHi = 1.0)
    {
        if (!recording && depth <= 0.0)
            return v;
        Channel& c = ch[i];
        c.fullLo = fullLo;
        c.fullHi = fullHi;
        if (recording)
        {
            c.seenLo = std::min (c.seenLo, v);
            c.seenHi = std::max (c.seenHi, v);
        }
        return apply (i, v);
    }
    // stretched only (a value between two the channel was given, as Wobble's sample by sample)
    double apply (int i, double v) const
    {
        if (depth <= 0.0)
            return v;
        const Channel& c = ch[i];
        const double full = c.fullHi - c.fullLo;
        if (!c.valid || c.hi - c.lo <= kStill * std::fabs (full))
            return v;
        const double u = std::clamp ((v - c.lo) / (c.hi - c.lo), 0.0, 1.0);
        return v + depth * (c.fullLo + full * u - v);
    }
    bool active () const { return depth > 0.0; }
    double depthNow () const { return depth; }

private:
    struct Channel
    {
        double seenLo = 1e300, seenHi = -1e300; // this pass
        double lo = 0.0, hi = 1.0, loTarget = 0.0, hiTarget = 1.0;
        double fullLo = 0.0, fullHi = 1.0;
        bool valid = false;
    };
    Channel ch[kChannels];
    double depth = 0.0;
    bool recording = false;
};

} // namespace moistr
