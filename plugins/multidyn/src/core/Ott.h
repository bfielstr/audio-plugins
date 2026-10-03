// Multidyn's OTT style: a model of Xfer Records' free OTT (measured: version 1.3.1), the plug-in that
// Live's "OTT" Multiband Dynamics preset became and that Serum's multiband compressor follows. The
// constants and laws are David Braun's fit to measurements of the OTT binary (Faust's compressors.lib,
// co.xfer_ott, MIT licence: https://github.com/grame-cncm/faustlibraries/pull/257); static curves
// match the plug-in to 0.03-0.04 dB (median), program material to about 0.1 dB (median) / 1 dB (95 %).
//
// Per band (K: 0 low, 1 mid, 2 high), on the band's stereo mean square (L^2 + R^2) / 2:
//   envelope: a one-pole with a fixed attack and a release that follows Time (fitted up to 100 %, then
//             in proportion), both per band
//   gain (dB) = makeup(Depth) + min(cap, Depth x Upward x slope x soft(knee, Tu - E))
//             - min(1, dnshape(Depth) x Downward) x soft(knee, E - Td),  floored per band
// where soft() is a softplus hinge (a soft knee), the upward branch rises at about 4:1 below its
// threshold up to a cap of about 36 dB and the downward branch is an infinite ratio above its own.
// Not affiliated with or endorsed by Xfer Records.
#pragma once

#include <algorithm>
#include <cmath>

namespace multidyn::ott {

constexpr double kXover1 = 88.3, kXover2 = 2500.0;
constexpr double kUpThresh[3] = {-46.0171, -46.5885, -47.4393};
constexpr double kUpKnee[3] = {0.0552, 0.4509, 0.0338};
constexpr double kUpSlope[3] = {0.74786, 0.75601, 0.74738};
constexpr double kDownThresh[3] = {-38.9401, -34.8204, -41.6879};
constexpr double kDownKnee[3] = {0.5, 0.530023, 0.5};
constexpr double kMakeup[3] = {14.0348, 9.05203, 14.064};
constexpr double kUpCap[3] = {35.901, 36.285, 35.872};
constexpr double kFloor[3] = {-26.26, -31.70, -25.95};
constexpr double kAttack[3] = {0.00621537, 0.006, 0.0096577}; // s

// the release time constant (s) at Time tp (%): fitted from 0 to 100 %, then in proportion
inline double releaseSec (int k, double tp)
{
    static constexpr double r0[3] = {0.0100376, 0.0118089, 0.00702632}, r50[3] = {0.0148167, 0.0174314, 0.00702632},
                            r100[3] = {0.030877, 0.030877, 0.0103717};
    const double t = std::clamp (tp, 0.0, 100.0);
    const double r = t <= 50.0 ? r0[k] + (r50[k] - r0[k]) * t / 50.0 : r50[k] + (r100[k] - r50[k]) * (t - 50.0) / 50.0;
    return r * std::max (1.0, tp / 100.0);
}

// Depth laws (cubics through the origin, 1 at Depth 1)
inline double makeupShape (int k, double d)
{
    static constexpr double a[3] = {1.83819, 1.63851, 1.84440}, b[3] = {-1.21424, -0.83224, -1.22723}, c[3] = {0.37606, 0.19373, 0.38284};
    return d * (a[k] + d * (b[k] + d * c[k]));
}
inline double downShape (int k, double d)
{
    static constexpr double a[3] = {0.96509, 0.96161, 0.97286}, b[3] = {0.03154, 0.02205, 0.05778}, c[3] = {0.00337, 0.01634, -0.03064};
    return d * (a[k] + d * (b[k] + d * c[k]));
}

// softplus hinge: 0 below, slope 1 above, rounded over about w dB
inline double soft (double w, double z) { return std::max (z, 0.0) + w * std::log1p (std::exp (-std::fabs (z) / w)); }

// The band's gain (dB) at detector level e (dB). upScale / downScale: how strong the branches are
// relative to OTT's (1, 1 at the defaults; Multidyn's Below / Above ratios set them), upShift /
// downShift: how far the thresholds moved from OTT's (dB).
inline double gainDb (int k, double e, double depth, double upScale, double downScale, double upShift, double downShift)
{
    const double d = std::clamp (depth, 0.0, 1.0);
    const double ups = d * upScale;
    const double dns = std::min (1.0, downShape (k, d) * downScale);
    const double g = makeupShape (k, d) * kMakeup[k] + std::min (kUpCap[k], ups * kUpSlope[k] * soft (kUpKnee[k], kUpThresh[k] + upShift - e)) -
                     dns * soft (kDownKnee[k], e - kDownThresh[k] - downShift);
    return std::max (kFloor[k], g);
}

// Which of OTT's three bands Multidyn's band b of n plays: the lowest is OTT's low band, the top its
// high band, any between its mid band (one band alone: the mid band).
inline int bandKind (int b, int n) { return n <= 1 ? 1 : (b == 0 ? 0 : (b == n - 1 ? 2 : 1)); }

} // namespace multidyn::ott
