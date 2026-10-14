// The Cut EQ's shape, in one place: three fixed bells, measured from Scream 4's Cut section (impulse
// responses of every fourth step of each slider through the Reason Rack Plugin at 48 kHz, each fitted to
// within 0.02 dB by a bilinear-prewarped bell):
//
//   Lo at 126.3 Hz, Mid at 982.6 Hz, Hi at 9826 Hz, each with a Q of 1 (its poles' damping 1, widened by
//   w0 / sin (w0) as PEQ-2's bells are: sweepr's BellShape.h).
//   A boost moves the zeros (the poles' damping stays), a cut the poles (the zeros' stays): the cut is the
//   boost's exact mirror.
//   -18.2 to +17.9 dB over the slider (0 dB in the middle). Here the knobs are in dB, -18 to +18.
//
// Cut sits after the tape in Scream (a Lo boost raises the output and reshapes the harmonics as an EQ
// after the curve would); Cut Place puts it before the tape too, into the saturation.
#pragma once

#include "sweepr/src/core/BellShape.h"

#include <cmath>

namespace tapr::cut {

constexpr int kBands = 3;
constexpr double kCentreHz[kBands] = {126.32, 982.6, 9826.2};
constexpr double kGainMin = -18.0, kGainMax = 18.0;

// A band's bell at gain dB, for a filter at rate sr.
inline sweepr::bell::Prototype prototype (int band, double gainDb, double sr)
{
    const double fc = sweepr::bell::centreAt (kCentreHz[band], sr);
    const double w0 = 2.0 * 3.14159265358979323846 * fc / sr;
    const double damp = w0 / std::sin (w0); // Q 1
    const double g = std::pow (10.0, gainDb / 20.0);
    return gainDb >= 0.0 ? sweepr::bell::Prototype {fc, damp, damp * g} : sweepr::bell::Prototype {fc, damp / g, damp};
}

} // namespace tapr::cut
