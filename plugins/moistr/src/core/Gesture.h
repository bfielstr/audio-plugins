// Moistr's gestures (0.27): breakpoint curves played in time with the song, each pulling one target.
//
// A Gesture is a curve over a length in beats: points (beat, value), values 0 .. 1, joined by straight lines;
// two points at the same beat are a jump (the curve takes the later one from that beat on). Before the first
// point it holds the first value, after the last the last one. Evaluating it (at ()) searches the points (a
// binary search) and never allocates; a Gesture is a fixed-size object, so the audio thread holds copies or
// pointers to immutable ones only.
//
// A slot plays its gesture in one of two ways (gesturePosition: where in the gesture it is, 0 .. 1):
//   Loop  locked to the song position: pos = frac (beats / Length - Position). Length is the gesture's own or
//         1/2 .. 32 beats (the curve is stretched to it); Position moves the loop's start (a share of it).
//   Walk  back and forth through the gesture: pos = triangle (beats x Speed / Length + Position), so it runs
//         forwards over one Length and backwards over the next. At Speed Hold it stays at Position (scrub it by
//         hand or with automation).
// While the host is stopped (or gives no song position) the beats run on at the last tempo (120 at first).
// Smooth: the value follows the curve through a one-pole smoother of 2 ms (0: steps stay steps, without the
// click) up to a 1/16 beat (1: glides).
//
// The factory gestures are simple generic shapes on straight and triplet grids (factoryGesture). A user gesture
// is a JSON file (GestureFile.h).
#pragma once

#include <cmath>
#include <cstddef>

namespace moistr {

constexpr int kMaxGesturePoints = 512;

struct Gesture
{
    double length = 1.0; // beats (> 0)
    int count = 0;
    double beat[kMaxGesturePoints] {};
    double value[kMaxGesturePoints] {};

    // the curve at `b` beats from its start (clamped to 0 .. length)
    double at (double b) const;
    // Sets the curve from n points (beats non-decreasing, within 0 .. length; values clamped to 0 .. 1). False (and
    // the gesture left as it was) when there are none, too many, or they are out of order.
    bool set (const double* beats, const double* values, int n, double lengthBeats);
    // a flat line at v
    void flat (double v, double lengthBeats = 1.0);
};

// the factory gestures (append only: the Gesture choice's index is saved in projects)
enum FactoryGesture
{
    kGestureCellFade = 0, // 8 beats: held, then fades out over 3 beats; back in on the downbeat
    kGestureSwell,        // 8 beats: silent, then swells in over 3 beats (Cell Fade the other way round)
    kGestureStutter16,    // 1 beat: a gate in 16th notes (on for half of each)
    kGestureStutter12,    // 1 beat: a gate in 8th-note triplets
    kGestureTripletGate,  // 2 beats: quarter-note triplets, on for three quarters of each
    kGestureQuarterGate,  // 1 beat: on for the first half
    kGestureOffBeatGate,  // 2 beats: off for the first beat, on for the second
    kGestureRateRise,     // 4 beats: low, rising over the last two beats, snapping back
    kGestureBuzzTail,     // 8 beats: low, jumping to the top for the last two beats
    kGesturePluck8,       // 1/2 beat: instantly open, falling over a 16th
    kGestureResonantClose, // 8 beats: open, closing over beats 5 .. 7, jumping open on the downbeat
    kGestureTalk,         // 2 beats: glides between places on a triplet grid
    kGestureCrossfade,    // 4 beats: down over a beat, held, back up over a beat
    kGestureScan,         // 4 beats: two slow ramps up, each starting from a jump down
    kGestureRampUp,       // 1 beat: 0 to 1
    kNumFactoryGestures
};
constexpr int kUserGesture = kNumFactoryGestures; // the Gesture choice's last entry: the slot's user gesture

const char* factoryGestureName (int g);
const Gesture& factoryGesture (int g);

// 0 .. 1 .. 0 over u = 0 .. 1 .. 2 (period 2)
inline double triangle (double u)
{
    const double f = 0.5 * u - std::floor (0.5 * u);
    return 1.0 - std::fabs (1.0 - 2.0 * f);
}
// where in its gesture (0 .. 1) a slot is at `beats` (mode: GestureMode; lengthBeats > 0; speed: Walk's, 0 holds)
double gesturePosition (int mode, double beats, double lengthBeats, double speed, double position);

} // namespace moistr
