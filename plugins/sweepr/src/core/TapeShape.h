// The tape stage's shape, in one place (as BellShape.h is the bells'): what Drive, Speed and Compression do.
// Calibrated against Scream 4's Tape mode, measured through the Reason Rack Plugin at 48 kHz (sines from
// -72 to 0 dBFS, noise, level steps, every knob swept): Drive is its Damage, Speed its P1, Compression its
// P2, each the same knob position. Its chain, and so the stage's:
//
//   x Drive's gain -> a leveller -> a sine clipper -> (the oversampler down) -> a high shelf (Speed)
//
//   Drive:       -10 dB to +50.2 dB (60.2 dB over the knob, linear in dB), the curve's input.
//   Compression: a stereo-linked peak follower (2.2 ms attack, 21 ms release) on the driven signal sets a
//                gain 1 / (1 + 3.21 x Compression x the level): a soft knee towards a ceiling 1 / (3.21 x
//                Compression) (0: no levelling at all, so a high Drive clips to a square wave).
//   The curve:   L sin (x / L) up to x = L pi / 2, then L (a sine clipper: slope 1 at 0, flat at +-L, odd
//                harmonics, H3 -9.5 dB when fully clipped), L 0.498, with a bias of 0.0012 at its input (a
//                little H2, about -60 dB) taken out again at its output.
//   Speed:       a second-order high shelf (H(s) = (g s^2 + b1 s + 1) / (s^2 + a1 s + 1), s = j f / corner)
//                cutting the highs: -48 dB above 2.1 kHz at 0 to -2.3 dB above 11.3 kHz (with a little
//                lift below it) at 100 %, its fitted settings at 18 knob positions, kShelf.
//
// Measured against sweepr: the gain and the odd harmonics of a 100 Hz sine within about 1 dB from -48 to
// 0 dBFS at Drive 75 %, Speed 80 %, Compression 35 %; the shelf within 0.1 dB from Speed 55 % up (lower,
// up to 4 dB off where it is already 40 dB down). Scream's output is the other way up and its Master knob
// follows (-4.4 dB in its default patch); here the polarity stays and Output is the Master.
#pragma once

#include <algorithm>
#include <cmath>

namespace sweepr::tape {

inline double driveDb (double drive) { return -10.02 + 60.196 * drive; }

constexpr double kLevelAttack = 0.00216, kLevelRelease = 0.0208; // s
constexpr double kLevelAmount = 3.21;                            // the gain is 1 / (1 + this x Compression x level)

constexpr double kClip = 0.4983, kBias = 0.0012;
inline double curve (double x)
{
    constexpr double edge = kClip * 1.57079632679489662;
    return std::fabs (x) < edge ? kClip * std::sin (x / kClip) : std::copysign (kClip, x);
}
// its antiderivative (for the anti-aliasing)
inline double curveIntegral (double x)
{
    constexpr double edge = kClip * 1.57079632679489662;
    const double a = std::fabs (x);
    return a < edge ? kClip * kClip * (1.0 - std::cos (x / kClip)) : kClip * kClip + kClip * (a - edge);
}

// The curve with first-order antiderivative anti-aliasing: its mean between the last input and this one.
inline double curveAa (double x, double last)
{
    const double d = x - last;
    if (std::fabs (d) < 1e-7)
        return curve (0.5 * (x + last));
    return (curveIntegral (x) - curveIntegral (last)) / d;
}

// the shelf at a Speed (0 .. 1): its corner (Hz), its gain above it (dB) and the numerator's and the
// denominator's s terms
struct Shelf
{
    double corner, gainDb, b1, a1;
};
// (Speed, then the shelf)
constexpr double kShelfAt[][5] = {
    {0.000000, 2083.1, -47.855, 0.06327, 0.73679},
    {0.062992, 2399.6, -45.098, 0.07913, 0.78453},
    {0.125984, 2758.4, -42.336, 0.09917, 0.83601},
    {0.188976, 3164.1, -39.560, 0.12442, 0.88974},
    {0.251969, 3621.8, -36.763, 0.15612, 0.94382},
    {0.314961, 4135.8, -33.938, 0.19577, 0.99595},
    {0.377953, 4708.4, -31.083, 0.24516, 1.04381},
    {0.440945, 5338.4, -28.194, 0.30649, 1.08532},
    {0.503937, 6017.9, -25.275, 0.38252, 1.11906},
    {0.566929, 6729.9, -22.328, 0.47682, 1.14482},
    {0.629921, 7446.2, -19.359, 0.59404, 1.16457},
    {0.692913, 8130.5, -16.376, 0.74011, 1.18408},
    {0.755906, 8756.1, -13.391, 0.92145, 1.21411},
    {0.803150, 9192.5, -11.159, 1.08288, 1.25128},
    {0.818898, 9336.2, -10.419, 1.14138, 1.26720},
    {0.881890, 9935.2, -7.491, 1.39476, 1.34746},
    {0.944882, 10618.5, -4.652, 1.66754, 1.44406},
    {1.000000, 11289.3, -2.288, 1.90901, 1.52806}};

inline Shelf shelfAt (double speed)
{
    constexpr int n = (int)(sizeof (kShelfAt) / sizeof (kShelfAt[0]));
    const double s = std::clamp (speed, 0.0, 1.0);
    int i = 0;
    while (i < n - 2 && s > kShelfAt[i + 1][0])
        ++i;
    const double t = (s - kShelfAt[i][0]) / (kShelfAt[i + 1][0] - kShelfAt[i][0]);
    auto lerp = [&] (int k) { return kShelfAt[i][k] + t * (kShelfAt[i + 1][k] - kShelfAt[i][k]); };
    return {std::exp (std::log (kShelfAt[i][1]) + t * (std::log (kShelfAt[i + 1][1]) - std::log (kShelfAt[i][1]))), lerp (2), lerp (3),
            lerp (4)};
}

} // namespace sweepr::tape
