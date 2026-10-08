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
//
// 0.28: ONE gesture moving many targets together (a Scene). A scene is one timeline in beats with a lane per
// target it moves (any of the slots' targets: Mid / High / Air Level, Wobble Rate / Amount, Close, Liquid Pos,
// Dirt, Bells, Mid X, High X, Seed Blend, Shift). Every lane is a curve as above over the scene's length, with
// its own range: the curve's 0 and 1 are two values in the target's units (targetNorm), so the scene alone sets
// how far each target goes. All lanes play on one clock (Loop or Walk, Length, Speed, Position, Smooth: the
// GESTURE section), so they move in tandem; Amount scales them all. The 0.27 slots keep playing beside it (a
// 0.27 project keeps its sound); a new instance has neither.
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

// ---- the one gesture (0.28): a Scene

// a level target's range (dB) before it fades to silence; Close's travel (octaves) below its open corner, and the
// open corner with Tone off (Hz)
constexpr double kGestureLevelDb = 48.0, kCloseOctaves = 6.0, kCloseOpenHz = 20000.0;
constexpr int kMaxSceneLanes = 16;

struct SceneLane
{
    int target = 0;    // GestureTarget (Off: a lane that does nothing)
    Gesture curve;     // 0 .. 1 over the scene's length
    double lo = 0.0, hi = 1.0; // where the curve's 0 and 1 put the target (its own 0 .. 1, targetNorm)
    // Close with a range in Hz: lo and hi follow the open corner (Tone's), worked out as it plays
    bool closeHz = false;
    double hzLo = 0.0, hzHi = 0.0;
};

struct Scene
{
    double length = 8.0; // beats
    int count = 0;       // lanes
    SceneLane lane[kMaxSceneLanes];
};

// The Gesture choice (kScene): None, the factory gestures, then User. (Append only: the index is saved.)
constexpr int kSceneNone = 0;
enum FactoryScene
{
    kSceneReeseCell = 0, // 8 beats: mids fade out as highs swell, Close shuts on beat 6, Wobble ramps then buzzes, Dirt against the mids
    kSceneStutterCell,   // 4 beats: High and Air stutter in 16ths, then 8th-note triplets; the mids duck, Dirt up in the bursts
    kSceneTalkingCell,   // 2 beats: Close and Liquid Pos step and glide on a triplet grid (a talking filter)
    kSceneCrossoverWalk, // 2 beats: Mid X and High X ramp against each other in quarter- and third-beat moves
    kSceneSlowPhrase,    // 32 beats: Seed Blend scans, the bells fade, Close slowly shuts and opens on the downbeat
    kScenePluck,         // 1 beat: 8th-note plucks: Close and High Level open at once and fall over a 16th
    kSceneBuzzTail,      // 8 beats: a steady wobble, then an audio-rate buzz on the last two beats
    kSceneTripletWobble, // 4 beats: Wobble Rate steps through 3, 6, 9 cycles per beat on a triplet grid; Air gated
    kSceneScanCell,      // 4 beats: Seed Blend and Liquid Pos jump and scrub per note; Mid X steps
    kSceneGateSwap,      // 2 beats: Mid and High swap places every beat (Dirt follows the High band)
    kNumFactoryScenes
};
constexpr int kSceneUser = kNumFactoryScenes + 1; // the Gesture choice's last entry
const char* factorySceneName (int s);               // s: 0 .. kNumFactoryScenes - 1
const Scene& factoryScene (int s);

// A target's own 0 .. 1 (what a slot or a lane pulls it to) from a value in its units:
//   Mid / High / Air Level   dB from the band's Level: 0 at it, -48 silent
//   Wobble Rate              cycles per beat (1 .. 40)
//   Close                    Hz: the low-pass's corner (openHz and above: open; kCloseOctaves below: shut)
//   Mid X, High X, Shift     Hz, as the controls
//   Wobble Amount, Liquid Pos, Dirt, Bells, Seed Blend    0 .. 1 (Dirt 1: the saturated sound, 0: clean)
double targetNorm (int target, double unit, double openHz = kCloseOpenHz);
// a target's whole range in its units (a lane without "min" or "max" takes these)
void targetUnits (int target, double& lo, double& hi);
// whether the target's units are spaced on a log scale (Hz, rates): a lane's values between min and max are
// then on that scale (so straight lines in the curve are straight in the target's own 0 .. 1)
bool targetLog (int target);
// a target by name (kTargetNames; case and spaces do not matter); -1 when there is none
int targetByName (const char* name);
// Sets a lane from its curve (values 0 .. 1) and its range in units (lo at 0, hi at 1); hasRange false: the
// target's whole own range (0 .. 1)
void setLaneRange (SceneLane& lane, int target, bool hasRange, double lo, double hi);

// 0 .. 1 .. 0 over u = 0 .. 1 .. 2 (period 2)
inline double triangle (double u)
{
    const double f = 0.5 * u - std::floor (0.5 * u);
    return 1.0 - std::fabs (1.0 - 2.0 * f);
}
// where in its gesture (0 .. 1) a slot is at `beats` (mode: GestureMode; lengthBeats > 0; speed: Walk's, 0 holds)
double gesturePosition (int mode, double beats, double lengthBeats, double speed, double position);

} // namespace moistr
