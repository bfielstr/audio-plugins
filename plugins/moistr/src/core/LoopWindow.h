// Loop Lock's window (0.30): the span of motion time Loop Lock loops in, and what moves in it.
//
// The window is as long as the slowest cycle of everything that moves now (windowParts: each active modulator's
// period in beats, an Hz rate turned into beats at the tempo): the bells (Sweep on, the bell on, its gain not 0), the
// High Shelf's orbit, the bands' pattern (one movement cycle x the most cycles a band's pattern takes to repeat; with
// Seed Blend, Seed B's too) and Liquid, the gesture (Loop: its length; Walk: there and back), the 0.27 slots, Wobble and
// PARA. With nothing moving it is a bar. Every modulator's phase is measured from the window's start (motion beats 0:
// each one at its own start, its Phase where it has one), so at the window's start the whole movement starts again,
// whatever its rate is in.
//
// Loop Start and Loop End pick a region of it (fractions, as a sampler's loop on a sample): the region from
// Start x window to End x window is what Loop Lock plays (regionMotion), again at every Length of the song (normalized:
// the region time-scaled to fit Length), or with Length Natural at its own speed, again at the first 1/16 of a beat
// after it ends.
//
// motionCurves: every active modulator's value (0 .. 1) at motion beats, for the display (the same functions the
// engine's parts run on).
#pragma once

#include "Gesture.h"
#include "Movement.h"
#include "Params.h"

#include <cmath>
#include <string>
#include <vector>

namespace moistr {

// Drift (Drift Seed, Start Drift, Speed Drift): each modulator on the motion clock (each bell, the shelf's orbit, the
// bands' pattern and Liquid, the one gesture, Wobble, PARA) started up to Start Drift of its cycle later and run up to
// +- Speed Drift faster or slower, both drawn from the seed (SplitMix64, a stream per modulator): the same seed always
// gives the same, playing or rendering. A modulator's phase is then (its phase as set) x factor + offset, in cycles
// (the gesture's offset is of its length). Seed 0: off, exactly as set.
enum DriftPart { kDriftBell = 0, kDriftShelf = kDriftBell + kNumBells, kDriftBands, kDriftGesture, kDriftWobble, kDriftPara, kDriftParts };
struct MotionDrift
{
    bool on = false;
    double factor[kDriftParts] {}, offset[kDriftParts] {};
};
MotionDrift makeDrift (int seed, double startDrift, double speedDrift);
inline MotionDrift driftOf (const double* p)
{
    return makeDrift ((int)std::lround (p[kDriftSeed]), p[kStartDrift], p[kSpeedDrift]);
}

// Length's choice: 1/16 .. 4 bars (kLoopLengthBeats), then Natural
constexpr int kLoopNatural = kNumLoopLengths;
// the shortest region (a fraction of the window), the longest window (beats), Natural's grid (beats) and Wrap's glide
// back with Natural (s)
constexpr double kLoopMinRegion = 1.0 / 256.0, kLoopMaxWindowBeats = 256.0, kLoopNaturalGrid = 1.0 / 16.0, kLoopNaturalGlideSec = 0.06;

struct WindowPart
{
    std::string name;
    double periodBeats = 0.0;
};
// what moves now and each one's period (beats) at `bpm`; scene: the one gesture playing (nullptr: none); a, b: Seed's and
// Seed B's patterns (first pass; b nullptr: Seed Blend 0)
std::vector<WindowPart> windowParts (const double* p, double bpm, const Scene* scene, const Pattern* a, const Pattern* b,
                                     const MotionDrift* drift = nullptr);
// the window's length (beats): the longest period, a bar with nothing moving, at most kLoopMaxWindowBeats
double windowBeats (const double* p, double bpm, const Scene* scene, const Pattern* a, const Pattern* b, const MotionDrift* drift = nullptr);

struct MotionCurve
{
    std::string name;
    double value = 0.0; // 0 .. 1
};
std::vector<MotionCurve> motionCurves (const double* p, double bpm, const Scene* scene, const Pattern* a, double tau,
                                      const MotionDrift* drift = nullptr);

// Where the region puts the motion clock (beats from the window's start) at song beats `song`: start, end (fractions),
// the window (beats), Length's choice, Shape, the tempo (for Wrap's glide back). And the song beats a pass of the region
// takes (Length, or Natural's).
double regionMotion (double song, double start, double end, double window, int lengthChoice, int shape, double bpm);
double regionLength (double start, double end, double window, int lengthChoice, int shape, double bpm);
// Natural: where a pass `len` song beats long puts the motion clock `t` beats into it (the engine keeps a pass's length
// until it ends, so moving Start or End never jumps it)
double naturalMotion (double t, double len, double start, double end, double window, int shape, double bpm);

// The display's mouse: the region slid by `delta` (a fraction of the window), its length kept, inside the window; a
// handle (Start or End) moved to `to`, the region at least kLoopMinRegion long
struct LoopRegion
{
    double start = 0.0, end = 1.0;
};
LoopRegion slideRegion (LoopRegion r, double delta);
LoopRegion moveHandle (LoopRegion r, bool endHandle, double to);

} // namespace moistr
