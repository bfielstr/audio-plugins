// Dropr's static gain law, shared by the engine, the editor's display and the tests. All levels in dB;
// `level` is a band's detector level (its peak, after the Input gain).
//
// Downward (threshold Td, knee width W, e = level - Td):
//   the output's excess over Td is  c(e) = e                                  for e <= -W/2
//                                         = e + (s - 1) (e + W/2)^2 / (2 W)    for |e| < W/2
//                                         = s e                                for e >= W/2
//   with the slope s = 1/r for a normal ratio 1 : r (r = 1 .. inf: s = 1 .. 0; 1 : inf is a brickwall at Td),
//   and in Negative mode s = -x for a negative ratio 1 : -x (x = 0.1 .. inf): above the threshold every dB
//   the level rises takes the output DOWN by x dB (x = 1 mirrors the level at the threshold). Negative
//   mode has a floor: the output never goes below Td - Range (unless the level itself is lower), so
//       c(e) = max (c(e), min (e, -Range))
//   1 : -inf takes everything over the threshold straight down to that floor (s = -10000 here, so the
//   law stays continuous: the drop happens within Range / 10000 dB). In the usual in : out notation a
//   negative ratio 1 : -x is a ratio of -1/x : 1.
//   downward gain = c(e) - e  (<= 0)
// Upward (threshold Tu, ratio 1 : ru, u = 1/ru, e = level - Tu): below the threshold quiet gets louder,
//   gain = (1 - u) (-e)                      for e <= -W/2
//        = (1 - u) (W/2 - e)^2 / (2 W)       for |e| < W/2
//        = 0                                 for e >= W/2
//   at most kMaxUpDb, and levels below kLevelFloorDb count as kLevelFloorDb (silence is not lifted forever).
// The two add up.
#pragma once

#include "Params.h"

#include <algorithm>
#include <cmath>

namespace dropr {

constexpr double kMaxUpDb = 30.0;       // the most upward compression lifts
constexpr double kLevelFloorDb = -150.0; // detector levels below this count as this
constexpr double kNegInfSlope = -10000.0;

struct GainLaw
{
    double downT = -72.0, downS = 1.0, range = 12.0, knee = 0.0;
    bool negative = false;
    double upT = -48.0, upU = 1.0;

    // from the parameters' plain values
    template <typename Get>
    static GainLaw from (Get get)
    {
        GainLaw g;
        g.downT = get (kDownThreshold);
        g.negative = get (kNegative) >= 0.5;
        if (g.negative)
        {
            const double x = get (kNegRatio);
            g.downS = x >= kRatioInf * 0.999 ? kNegInfSlope : -std::max (0.0, x);
        }
        else
        {
            const double r = std::max (1.0, get (kDownRatio));
            g.downS = r >= kRatioInf * 0.999 ? 0.0 : 1.0 / r;
        }
        g.range = std::max (0.0, get (kRange));
        g.knee = std::max (0.0, get (kKnee));
        g.upT = get (kUpThreshold);
        const double ru = std::max (1.0, get (kUpRatio));
        g.upU = ru >= kRatioInf * 0.999 ? 0.0 : 1.0 / ru;
        return g;
    }

    double down (double level) const
    {
        const double e = level - downT;
        double c;
        if (2.0 * e <= -knee)
            c = e;
        else if (2.0 * e >= knee)
            c = downS * e;
        else
        {
            const double k = e + 0.5 * knee;
            c = e + (downS - 1.0) * k * k / (2.0 * knee);
        }
        if (negative)
            c = std::max (c, std::min (e, -range));
        return std::min (0.0, c - e);
    }

    double up (double level) const
    {
        if (upU >= 1.0)
            return 0.0;
        const double e = std::max (level, kLevelFloorDb) - upT;
        double g;
        if (2.0 * e >= knee)
            g = 0.0;
        else if (2.0 * e <= -knee)
            g = (1.0 - upU) * -e;
        else
        {
            const double k = 0.5 * knee - e;
            g = (1.0 - upU) * k * k / (2.0 * knee);
        }
        return std::min (kMaxUpDb, g);
    }

    double gain (double level) const { return down (level) + up (level); }
};

// The bands: the crossovers in use (ascending, at least kMinXoverGap apart) from their parameters.
constexpr double kMinXoverGap = 1.12; // about a sixth of an octave
template <typename Get>
void xoversFrom (Get get, double* f)
{
    for (int j = 0; j < kNumXovers; ++j)
        f[j] = std::clamp (get (kXover1 + (uint32_t)j), kMinXoverHz, kMaxXoverHz);
    for (int j = 1; j < kNumXovers; ++j)
        f[j] = std::max (f[j], f[j - 1] * kMinXoverGap);
}

// The lower and upper edge of band k of n bands (the outer edges at 20 Hz and 20 kHz), and its centre
// (their geometric mean). With fewer than 6 bands the top band takes everything above crossover n - 2.
inline double bandLow (const double* f, int k) { return k <= 0 ? kMinXoverHz : f[k - 1]; }
inline double bandHigh (const double* f, int k, int n) { return k >= n - 1 ? kMaxXoverHz : f[k]; }
inline double bandCentre (const double* f, int k, int n) { return std::sqrt (bandLow (f, k) * std::max (bandLow (f, k), bandHigh (f, k, n))); }

// Tilt: dB per octave from 1 kHz, at a band's centre.
inline double tiltDb (double tilt, double centreHz) { return tilt * std::log2 (centreHz / 1000.0); }

} // namespace dropr
