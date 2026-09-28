// The shaping curves: each maps one input value to one output value (unity slope at zero, so
// quiet signals pass unchanged). Shared with the editor, which draws them.
#pragma once

#include "Params.h"

#include <algorithm>
#include <cmath>

namespace smacheratr {

struct WaveshaperSettings
{
    double drive = 1.0, curve = 0.5, depth = 0.0, linear = 0.5, damp = 0.0, period = 0.0; // 0 .. 1
};

struct ShaperSettings
{
    int curve = kAnalogClip;
    double bassThresholdDb = -20.0;
    WaveshaperSettings ws;
};

inline double sgn (double x) { return x < 0.0 ? -1.0 : 1.0; }

// Linear up to +-0.5, then a parabola that reaches +-1 with zero slope at +-1.5 (a smooth knee
// around the clipping point). Also the Soft Clip of the post-clip stage.
inline double analogClip (double x)
{
    const double a = std::fabs (x);
    if (a <= 0.5)
        return x;
    if (a >= 1.5)
        return sgn (x);
    const double u = a - 0.5;
    return sgn (x) * (0.5 + u - 0.5 * u * u);
}

// Linear below the threshold, a tanh above it that never exceeds +-1. Threshold 0 dB is a hard
// clip, -50 dB is a smooth saturation of the whole range.
inline double bassShaper (double x, double thresholdDb)
{
    const double t = std::clamp (std::pow (10.0, thresholdDb / 20.0), 0.001, 1.0);
    const double a = std::fabs (x);
    if (a <= t)
        return x;
    if (t >= 0.999)
        return sgn (x);
    return sgn (x) * (t + (1.0 - t) * std::tanh ((a - t) / (1.0 - t)));
}

inline double softSine (double x) { return std::fabs (x) >= M_PI_2 ? sgn (x) : std::sin (x); }
inline double mediumCurve (double x) { return std::tanh (x); }
inline double hardCurve (double x)
{
    const double a2 = x * x, a6 = a2 * a2 * a2;
    return x / std::pow (1.0 + a6, 1.0 / 6.0);
}
inline double sinoidFold (double x) { return std::sin (x); } // folds over past +-pi/2
inline double digitalClip (double x) { return std::clamp (x, -1.0, 1.0); }

// The Waveshaper curve: a linear region (Linear), then hard clipping blended with a cubic soft
// clipper (Curve: third-order harmonics), a sine superimposed on the curve (Depth, with Period
// setting its density) and an ultra-fast gate around zero (Damp). Drive blends the result with a
// plain digital clip, so at 0 % the other controls do nothing.
inline double waveshaper (double x, const WaveshaperSettings& ws)
{
    const double clipped = digitalClip (x);
    const double D = std::clamp (ws.drive, 0.0, 1.0);
    if (D <= 0.0)
        return clipped;
    const double a = std::fabs (x);
    const double L = std::clamp (ws.linear, 0.0, 0.99);
    double y = a;
    if (a > L)
    {
        const double u = std::min (1.0, (a - L) / (1.0 - L));
        const double soft = u + u * u - u * u * u; // unity slope at the knee, flat at 1
        y = L + (1.0 - L) * ((1.0 - ws.curve) * u + ws.curve * soft);
    }
    y *= sgn (x);
    if (ws.depth > 0.0)
        y += 0.4 * ws.depth * std::sin (2.0 * M_PI * (0.25 + 4.0 * ws.period) * x);
    if (ws.damp > 0.0)
    {
        const double th = 0.5 * ws.damp;
        if (a < th)
            y *= (a / th) * (a / th);
    }
    return clipped + D * (y - clipped);
}

inline double shape (double x, const ShaperSettings& s)
{
    switch (s.curve)
    {
        case kAnalogClip: return analogClip (x);
        case kSoftSine: return softSine (x);
        case kBassShaper: return bassShaper (x, s.bassThresholdDb);
        case kMediumCurve: return mediumCurve (x);
        case kHardCurve: return hardCurve (x);
        case kSinoidFold: return sinoidFold (x);
        case kDigitalClip: return digitalClip (x);
        case kWaveshaper: return waveshaper (x, s.ws);
        default: return x;
    }
}

} // namespace smacheratr
