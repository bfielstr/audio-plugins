// The shape of sweepr's bells, in one place: what a band's Freq, Q and Gain make of the filter. Everything
// that decides how a bell sounds is here (the knobs' ranges, how Q and Gain combine, the prototype both the
// filter and the display are made from), so it can be calibrated against a reference EQ (renders of it
// compared with sweepr's by sweepr_analyze, tools/analyze.cpp) or swapped for another definition without
// touching the filter, the LFOs or the display.
//
// The prototype is a second-order analog bell, s normalised to the centre (s = j f / centreHz):
//
//   H(s) = (s^2 + zeroDamp s + 1) / (s^2 + poleDamp s + 1)
//
// At the centre |H| = zeroDamp / poleDamp (the gain); far from it 1 (0 dB). The filter (dsp::SvfCoefs::bell)
// maps it with the bilinear transform prewarped at the centre, at the host's sample rate (Engine runs the
// bells before its oversampler), so the bell is a digital one at that rate: it cramps towards Nyquist
// like the reference does. response () is that digital bell.
//
// Now: Reason's PEQ-2, measured (impulse responses of every Freq, Q and Gain step through the Reason Rack
// Plugin at 48 kHz, each fitted by the prototype to within 0.0001 dB; the knobs have 128 steps, each the
// normalized value step / 127):
//
//   Freq: 16 kHz x 2^((step - 127) / 14), 29.7 Hz to 16 kHz (14 steps an octave): a log taper.
//   Gain: (step - 64) x 36 / 127 dB, -18.14 to +17.86 dB (0 dB at step 64): linear.
//   Q:    the poles' Q of a boost, 0.25 to 16.25: no simple taper, the measured table kPoleQ below.
//   A boost: poleDamp = 1 / Q, zeroDamp = poleDamp x 10^(Gain / 20) (the poles stay, the zeros move).
//   A cut:   poleDamp = |Gain| / 2.4 / Q (wider the deeper), zeroDamp = poleDamp x 10^(Gain / 20).
//   Both:    poleDamp x w0 / sin (w0), w0 = 2 pi Freq / the sample rate (wider towards Nyquist).
//
// The ranges below are PEQ-2's, so a knob at the same position (the same normalized value) gives the same
// bell. The Q knob's number is on a log scale between the two ends (Params.cpp's taper); the bell at each
// position is PEQ-2's at the same position (kPoleQ, interpolated between its steps), so between the ends
// the number is a label rather than the poles' Q (at the middle 2.05 for 2.31).
//
// The knobs' tapers are the parameters' curves in Params.cpp (Freq and Q log, Gain linear), built from the
// ranges below. Note that the parameters' normalized values are saved in projects: changing a range or a
// taper changes what saved projects sound like (a state conversion in plugin/State.cpp would be needed).
#pragma once

#include <algorithm>
#include <cmath>
#include <complex>

namespace sweepr::bell {

constexpr int kSteps = 127; // PEQ-2's knobs: 0 .. 127

// the knobs' ranges (Params.cpp)
constexpr double kFreqMax = 16000.0, kFreqMin = 29.74047353158187;  // Hz, log taper (16 kHz x 2^(-127 / 14))
constexpr double kQMin = 0.25, kQMax = 16.25;                        // log taper
constexpr double kGainStep = 36.0 / 127.0;                           // dB a step
constexpr double kGainMin = -64.0 * kGainStep, kGainMax = 63.0 * kGainStep; // dB, linear taper

// A cut's poles: damping x |Gain| / kCutWidth (a boost's are the Q's alone).
constexpr double kCutWidth = 2.4;

// The poles' Q of a boost at each step of the Q knob (measured).
constexpr double kPoleQ[kSteps + 1] = {
    0.25, 0.258223, 0.266807, 0.27576, 0.285098, 0.294833, 0.304981, 0.315549,
    0.326552, 0.338005, 0.349925, 0.362327, 0.375223, 0.388627, 0.402559, 0.417036,
    0.432072, 0.447684, 0.4639, 0.480725, 0.498187, 0.516303, 0.535094, 0.554577,
    0.574781, 0.595719, 0.617419, 0.63991, 0.663202, 0.687327, 0.712321, 0.738192,
    0.764973, 0.792692, 0.821385, 0.851065, 0.881783, 0.913545, 0.946405, 0.980386,
    1.01551, 1.05183, 1.08937, 1.12818, 1.16827, 1.20971, 1.25252, 1.29674,
    1.34242, 1.3896, 1.43832, 1.48863, 1.54056, 1.59419, 1.64954, 1.70668,
    1.76564, 1.82649, 1.88927, 1.95404, 2.02088, 2.08981, 2.16092, 2.23426,
    2.30988, 2.38791, 2.46832, 2.55122, 2.63674, 2.72486, 2.81571, 2.90933,
    3.00583, 3.1053, 3.20779, 3.31342, 3.42224, 3.53436, 3.64987, 3.76885,
    3.89141, 4.01763, 4.1477, 4.28163, 4.41956, 4.56158, 4.70779, 4.85837,
    5.01344, 5.17302, 5.33742, 5.50653, 5.68069, 5.85992, 6.04437, 6.23433,
    6.42972, 6.6307, 6.83769, 7.05069, 7.26981, 7.49521, 7.72714, 7.96589,
    8.2113, 8.4639, 8.72369, 8.99089, 9.26581, 9.54852, 9.83936, 10.1384,
    10.4459, 10.7622, 11.0874, 11.4218, 11.7659, 12.1195, 12.4831, 12.8569,
    13.2411, 13.6363, 14.0425, 14.46, 14.8892, 15.3304, 15.7839, 16.25};

// The highest centre a bell takes at rate sr (the filter's and the response's).
inline double centreAt (double freqHz, double sr) { return std::clamp (freqHz, 1.0, 0.49 * sr); }

// The poles' Q of a boost for the Q knob at q: the knob's position (its log taper), PEQ-2's at that position
// (between steps the table's log, interpolated).
inline double poleQ (double q)
{
    const double pos = std::clamp (std::log (q / kQMin) / std::log (kQMax / kQMin), 0.0, 1.0) * kSteps;
    const int i = std::min ((int)pos, kSteps - 1);
    const double t = pos - i;
    return std::exp (std::log (kPoleQ[i]) + t * (std::log (kPoleQ[i + 1]) - std::log (kPoleQ[i])));
}

struct Prototype
{
    double centreHz = 1000.0;
    double poleDamp = 1.0, zeroDamp = 1.0;
};

// A band's settings as the prototype bell, for a filter at rate sr.
inline Prototype prototype (double freqHz, double q, double gainDb, double sr)
{
    const double fc = centreAt (freqHz, sr);
    const double w0 = 2.0 * 3.14159265358979323846 * fc / sr;
    const double damp = w0 / std::sin (w0) / poleQ (q);
    // (a cut of almost nothing: kept a little damped, so the poles never sit on the unit circle)
    const double poles = gainDb >= 0.0 ? damp : damp * std::max (-gainDb, 0.01) / kCutWidth;
    return {fc, poles, poles * std::pow (10.0, gainDb / 20.0)};
}

// The bell's response at f (Hz) as the filter at rate sr has it (the bilinear transform prewarped at the
// centre: the prototype at tan (pi f / sr) / tan (pi centre / sr)).
inline std::complex<double> response (const Prototype& p, double f, double sr)
{
    constexpr double kPi = 3.14159265358979323846;
    const double fr = std::clamp (f, 0.0, 0.4999 * sr);
    const std::complex<double> s (0.0, std::tan (kPi * fr / sr) / std::tan (kPi * p.centreHz / sr));
    return (s * s + s * p.zeroDamp + 1.0) / (s * s + s * p.poleDamp + 1.0);
}

} // namespace sweepr::bell
