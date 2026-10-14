// The shape of sweepr's bells, in one place: what a band's Freq, Q and Gain make of the filter. Everything
// that decides how a bell sounds is here (the knobs' ranges, how Q and Gain combine, the analog prototype
// both the filter and the display are made from), so it can be calibrated against a reference EQ (renders
// of it compared with sweepr's by sweepr_analyze, tools/analyze.cpp) or swapped for another definition
// without touching the filter, the LFOs or the display.
//
// The prototype is a second-order analog bell, s normalised to the centre (s = j f / centreHz):
//
//   H(s) = (s^2 + zeroDamp s + 1) / (s^2 + poleDamp s + 1)
//
// At the centre |H| = zeroDamp / poleDamp (the gain); far from it 1 (0 dB). Any symmetric (on a log
// frequency axis) bell with a flat skirt is this shape for some centre and pair of dampings, so a fit to a
// reference needs only prototype () changed: which centre and dampings a setting gives.
//
// Now: the constant-Q bell (the cookbook's peaking EQ): A = 10^(Gain / 40), poleDamp = 1 / (Q A), zeroDamp
// = A / Q. Q is the bandwidth of the poles at 0 dB gain; a boost narrows and a cut widens in proportion, the
// same both ways (a boost and the cut of the same Q and size undo each other exactly). kProportionalQ adds
// the "proportional Q" behaviour some analog EQs have (Q grows with the size of the boost or cut); 0 (off)
// keeps the cookbook's bell.
//
// The filter (dsp::SvfCoefs::bell) maps this prototype with the bilinear transform prewarped at the centre,
// at 4x the sample rate, so the response follows the prototype to within a few hundredths of a dB up to
// 20 kHz (sweepr_core's bells_match_the_analog_bell test).
//
// The knobs' tapers are the parameters' curves in Params.cpp (Freq and Q log, Gain linear), built from the
// ranges below. Note that the parameters' normalized values are saved in projects: changing a range or a
// taper changes what saved projects sound like (a state conversion in plugin/State.cpp would be needed).
#pragma once

#include <algorithm>
#include <cmath>
#include <complex>

namespace sweepr::bell {

// the knobs' ranges (Params.cpp)
constexpr double kFreqMin = 20.0, kFreqMax = 20000.0; // Hz, log taper
constexpr double kQMin = 0.1, kQMax = 10.0;           // log taper
constexpr double kGainMin = -18.0, kGainMax = 18.0;   // dB, linear taper

// Proportional Q: the Q used is Q x (1 + kProportionalQ x |Gain| / 18). 0: off (the cookbook's bell).
constexpr double kProportionalQ = 0.0;

struct Prototype
{
    double centreHz = 1000.0;
    double poleDamp = 1.0, zeroDamp = 1.0;
};

// A band's settings as the prototype bell.
inline Prototype prototype (double freqHz, double q, double gainDb)
{
    const double A = std::pow (10.0, gainDb / 40.0);
    const double qq = std::max (0.01, q * (1.0 + kProportionalQ * std::fabs (gainDb) / kGainMax));
    return {freqHz, 1.0 / (qq * A), A / qq};
}

// The prototype's response at f (Hz).
inline std::complex<double> response (const Prototype& p, double f)
{
    const std::complex<double> s (0.0, f / p.centreHz);
    return (s * s + s * p.zeroDamp + 1.0) / (s * s + s * p.poleDamp + 1.0);
}

} // namespace sweepr::bell
