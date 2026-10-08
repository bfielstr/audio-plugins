#include "Gesture.h"

#include "Params.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <string>

namespace moistr {

double Gesture::at (double b) const
{
    if (count <= 0)
        return 1.0;
    b = std::clamp (b, 0.0, length);
    // the last point at or before b (a jump: the later of two points at the same beat)
    const double* end = beat + count;
    const double* it = std::upper_bound (beat, end, b);
    if (it == beat)
        return value[0];
    const int i = (int)(it - beat) - 1;
    if (i >= count - 1)
        return value[count - 1];
    const double t0 = beat[i], t1 = beat[i + 1]; // (t1 > b >= t0)
    return value[i] + (value[i + 1] - value[i]) * (b - t0) / (t1 - t0);
}

bool Gesture::set (const double* beats, const double* values, int n, double lengthBeats)
{
    if (n <= 0 || n > kMaxGesturePoints || !(lengthBeats > 0.0) || !std::isfinite (lengthBeats))
        return false;
    for (int i = 0; i < n; ++i)
        if (!std::isfinite (beats[i]) || !std::isfinite (values[i]) || beats[i] < 0.0 || beats[i] > lengthBeats + 1e-9 ||
            (i > 0 && beats[i] < beats[i - 1]))
            return false;
    length = lengthBeats;
    count = n;
    for (int i = 0; i < n; ++i)
    {
        beat[i] = std::min (beats[i], lengthBeats);
        value[i] = std::clamp (values[i], 0.0, 1.0);
    }
    return true;
}

void Gesture::flat (double v, double lengthBeats)
{
    length = lengthBeats;
    count = 1;
    beat[0] = 0.0;
    value[0] = std::clamp (v, 0.0, 1.0);
}

namespace {
struct Point
{
    double beat, value;
};
// a gate: `cells` cells of `cell` beats, each on (1) for its first `on` beats, then off (0)
template <size_t N>
void gate (std::array<Point, N>& out, int& n, int cells, double cell, double on)
{
    for (int k = 0; k < cells; ++k)
    {
        const double c = k * cell;
        out[(size_t)n++] = {c, 1.0};
        out[(size_t)n++] = {c + on, 1.0};
        out[(size_t)n++] = {c + on, 0.0};
        out[(size_t)n++] = {c + cell, 0.0};
    }
}

struct Library
{
    const char* names[kNumFactoryGestures] = {"Cell Fade", "Swell",     "1/16 Stutter", "1/12 Stutter",  "Triplet Gate",
                                              "1/4 Gate",  "Off-Beat Gate", "Rate Rise", "Buzz Tail",    "Pluck 1/8",
                                              "Resonant Close", "Talk", "Crossfade",    "Scan",          "Ramp Up"};
    Gesture g[kNumFactoryGestures];
    Library ()
    {
        auto make = [this] (int i, std::initializer_list<Point> pts, double length) {
            double b[kMaxGesturePoints], v[kMaxGesturePoints];
            int n = 0;
            for (const Point& p : pts)
            {
                b[n] = p.beat;
                v[n++] = p.value;
            }
            g[i].set (b, v, n, length);
        };
        auto makeGate = [this] (int i, int cells, double cell, double on) {
            std::array<Point, 64> pts {};
            int n = 0;
            gate (pts, n, cells, cell, on);
            double b[kMaxGesturePoints], v[kMaxGesturePoints];
            for (int k = 0; k < n; ++k)
            {
                b[k] = pts[(size_t)k].beat;
                v[k] = pts[(size_t)k].value;
            }
            g[i].set (b, v, n, cells * cell);
        };
        const double third = 1.0 / 3.0;
        make (kGestureCellFade, {{0, 1}, {3, 1}, {6, 0}, {8, 0}}, 8.0);
        make (kGestureSwell, {{0, 0}, {3, 0}, {6, 1}, {8, 1}}, 8.0);
        makeGate (kGestureStutter16, 4, 0.25, 0.125);
        makeGate (kGestureStutter12, 3, third, 0.5 * third);
        makeGate (kGestureTripletGate, 3, 2.0 * third, 0.5);
        makeGate (kGestureQuarterGate, 1, 1.0, 0.5);
        make (kGestureOffBeatGate, {{0, 0}, {1, 0}, {1, 1}, {2, 1}}, 2.0);
        make (kGestureRateRise, {{0, 0.2}, {2, 0.2}, {4, 0.9}}, 4.0);
        make (kGestureBuzzTail, {{0, 0.3}, {6, 0.3}, {6, 1}, {8, 1}}, 8.0);
        make (kGesturePluck8, {{0, 1}, {0.25, 0}, {0.5, 0}}, 0.5);
        make (kGestureResonantClose, {{0, 1}, {5, 1}, {7, 0}, {8, 0}}, 8.0);
        make (kGestureTalk, {{0, 0.2}, {third, 0.8}, {2 * third, 0.5}, {1, 0.9}, {4 * third, 0.3}, {5 * third, 0.6}, {2, 0.2}}, 2.0);
        make (kGestureCrossfade, {{0, 1}, {1, 0}, {2, 0}, {3, 1}, {4, 1}}, 4.0);
        make (kGestureScan, {{0, 0}, {2, 0.8}, {2, 0.2}, {4, 1}}, 4.0);
        make (kGestureRampUp, {{0, 0}, {1, 1}}, 1.0);
    }
};
const Library& library ()
{
    static const Library lib;
    return lib;
}
} // namespace

const char* factoryGestureName (int g)
{
    return g >= 0 && g < kNumFactoryGestures ? library ().names[g] : "User";
}

const Gesture& factoryGesture (int g)
{
    return library ().g[std::clamp (g, 0, kNumFactoryGestures - 1)];
}

// ---- the one gesture (0.28)

double targetNorm (int target, double u, double openHz)
{
    switch (target)
    {
        case kTargetMidLevel:
        case kTargetHighLevel:
        case kTargetAirLevel: return std::clamp (1.0 + u / kGestureLevelDb, 0.0, 1.0);
        case kTargetWobbleRate:
            return std::clamp (std::log2 (std::clamp (u, kWobbleRateMin, kWobbleRateMax) / kWobbleRateMin) / std::log2 (kWobbleRateMax / kWobbleRateMin),
                               0.0, 1.0);
        case kTargetClose: return u > 0.0 ? std::clamp (1.0 - std::log2 (openHz / u) / kCloseOctaves, 0.0, 1.0) : 0.0;
        case kTargetMidX: return std::clamp (paramTable ().toNormalized (kXoverMid, u), 0.0, 1.0);
        case kTargetHighX: return std::clamp (paramTable ().toNormalized (kXoverHigh, u), 0.0, 1.0);
        case kTargetShift: return std::clamp (paramTable ().toNormalized (kShift, u), 0.0, 1.0);
        default: return std::clamp (u, 0.0, 1.0);
    }
}

void targetUnits (int target, double& lo, double& hi)
{
    switch (target)
    {
        case kTargetMidLevel:
        case kTargetHighLevel:
        case kTargetAirLevel: lo = -kGestureLevelDb; hi = 0.0; return;
        case kTargetWobbleRate: lo = kWobbleRateMin; hi = kWobbleRateMax; return;
        case kTargetClose: lo = kCloseOpenHz * std::exp2 (-kCloseOctaves); hi = kCloseOpenHz; return;
        case kTargetMidX: lo = paramTable ().info (kXoverMid).min; hi = paramTable ().info (kXoverMid).max; return;
        case kTargetHighX: lo = paramTable ().info (kXoverHigh).min; hi = paramTable ().info (kXoverHigh).max; return;
        case kTargetShift: lo = paramTable ().info (kShift).min; hi = paramTable ().info (kShift).max; return;
        default: lo = 0.0; hi = 1.0; return;
    }
}

bool targetLog (int target)
{
    return target == kTargetWobbleRate || target == kTargetClose || target == kTargetMidX || target == kTargetHighX;
}

int targetByName (const char* name)
{
    auto squash = [] (const char* t) {
        std::string o;
        for (; t && *t; ++t)
            if (!std::isspace ((unsigned char)*t) && *t != '_' && *t != '-')
                o += (char)std::tolower ((unsigned char)*t);
        return o;
    };
    const std::string want = squash (name);
    for (int t = 0; t < kNumTargets; ++t)
        if (squash (kTargetNames[t]) == want)
            return t;
    return -1;
}

void setLaneRange (SceneLane& lane, int target, bool hasRange, double lo, double hi)
{
    lane.target = target;
    lane.closeHz = false;
    if (target == kTargetMidX || target == kTargetHighX)
    {
        // (in log2 Hz: the engine keeps them clear of Low X and of each other)
        if (!hasRange)
            targetUnits (target, lo, hi);
        lane.lo = std::log2 (std::clamp (lo, 20.0, 20000.0));
        lane.hi = std::log2 (std::clamp (hi, 20.0, 20000.0));
        return;
    }
    if (!hasRange)
    {
        lane.lo = 0.0;
        lane.hi = 1.0;
        return;
    }
    if (target == kTargetClose)
    {
        lane.closeHz = true;
        lane.hzLo = lo;
        lane.hzHi = hi;
    }
    lane.lo = targetNorm (target, lo);
    lane.hi = targetNorm (target, hi);
}

namespace {
constexpr const char* kSceneNames[kNumFactoryScenes] = {"Reese Cell", "Stutter Cell",   "Talking Cell", "Crossover Walk", "Slow Phrase",
                                                        "Pluck 1/8",  "Buzz Tail",      "Triplet Wobble", "Scan Cell",    "Gate Swap"};

// a lane drawn in the target's units: its range is the points' (lowest .. highest), its curve the points placed
// in it (on a log scale for Hz and rates)
void unitLane (Scene& s, int target, const Point* pts, int n)
{
    if (s.count >= kMaxSceneLanes || n <= 0)
        return;
    double lo = 1e300, hi = -1e300;
    for (int i = 0; i < n; ++i)
    {
        lo = std::min (lo, pts[i].value);
        hi = std::max (hi, pts[i].value);
    }
    const bool log = targetLog (target) && lo > 0.0;
    double b[kMaxGesturePoints], v[kMaxGesturePoints];
    for (int i = 0; i < n; ++i)
    {
        b[i] = pts[i].beat;
        v[i] = hi <= lo ? 1.0 : log ? std::log (pts[i].value / lo) / std::log (hi / lo) : (pts[i].value - lo) / (hi - lo);
    }
    SceneLane& lane = s.lane[s.count++];
    lane.curve.set (b, v, n, s.length);
    setLaneRange (lane, target, true, lo, hi);
}
void unitLane (Scene& s, int target, std::initializer_list<Point> pts) { unitLane (s, target, pts.begin (), (int)pts.size ()); }

struct SceneLibrary
{
    Scene s[kNumFactoryScenes];
    SceneLibrary ()
    {
        const double t = 1.0 / 3.0;
        const double open = kCloseOpenHz;
        {
            // Reese Cell: an 8-beat note cell
            Scene& c = s[kSceneReeseCell];
            c.length = 8.0;
            unitLane (c, kTargetMidLevel, {{0, 0}, {3, 0}, {5.5, -48}, {8, -48}});      // the mids fade out over 2.5 beats
            unitLane (c, kTargetHighLevel, {{0, -18}, {3, -18}, {4, 0}, {8, 0}});      // as the highs swell
            unitLane (c, kTargetClose, {{0, open}, {6, open}, {6 + t, 400}, {8, 400}}); // shuts on beat 6, open on the downbeat
            unitLane (c, kTargetWobbleRate, {{0, 3}, {4, 3}, {4 + t, 9}, {5.5, 6}, {7, 6}, {7, 38}, {8, 38}}); // 3 to 9, then a buzz
            unitLane (c, kTargetWobbleAmount, {{0, 0.3}, {4, 0.3}, {4, 0.8}, {8, 0.8}});
            unitLane (c, kTargetDirt, {{0, 0.4}, {3, 0.4}, {5.5, 1}, {8, 1}}); // the other way to the mids
        }
        {
            // Stutter Cell: two beats as they are, a beat of 16th stutters, a beat of 8th-note triplet stutters
            Scene& c = s[kSceneStutterCell];
            c.length = 4.0;
            std::array<Point, 64> hi {}, air {};
            int n = 0;
            hi[(size_t)n] = {0, 0};
            air[(size_t)n++] = {0, -48};
            hi[(size_t)n] = {2, 0};
            air[(size_t)n++] = {2, -48};
            auto burst = [&] (double from, double cell, int cells) {
                for (int k = 0; k < cells; ++k)
                {
                    const double a = from + cell * k;
                    for (Point p : {Point {a, 0}, Point {a + cell / 2, 0}, Point {a + cell / 2, -48}, Point {from + cell * (k + 1), -48}})
                    {
                        hi[(size_t)n] = p;
                        air[(size_t)n++] = p;
                    }
                }
            };
            burst (2.0, 0.25, 4);
            burst (3.0, t, 3);
            unitLane (c, kTargetHighLevel, hi.data (), n);
            unitLane (c, kTargetAirLevel, air.data (), n);
            unitLane (c, kTargetMidLevel, {{0, 0}, {2, 0}, {2, -12}, {4, -12}});
            unitLane (c, kTargetDirt, {{0, 0.5}, {2, 0.5}, {2, 1}, {4, 1}});
        }
        {
            // Talking Cell: stepped glides on a triplet grid (a 12th of a beat into each place)
            Scene& c = s[kSceneTalkingCell];
            c.length = 2.0;
            const double g = 1.0 / 12.0;
            unitLane (c, kTargetClose, {{0, 13000}, {t, 13000}, {t + g, 3700}, {2 * t, 3700}, {2 * t + g, 6300}, {1, 6300}, {1 + g, 550},
                                        {4 * t, 550}, {4 * t + g, 9000}, {5 * t, 9000}, {5 * t + g, 1400}, {2, 1400}});
            unitLane (c, kTargetLiquid, {{0, 0.2}, {t, 0.2}, {t + g, 0.8}, {2 * t, 0.8}, {2 * t + g, 0.5}, {1, 0.5}, {1 + g, 0.9}, {4 * t, 0.9},
                                         {4 * t + g, 0.3}, {5 * t, 0.3}, {5 * t + g, 0.6}, {2, 0.6}});
            unitLane (c, kTargetMidLevel, {{0, 0}, {1, -6}, {1, 0}, {2, -6}});
        }
        {
            // Crossover Walk: the upper crossovers move against each other; the Mid band held 15 dB down, so where Mid X
            // is moves the hollow (with every band at the same level the split sums flat and a crossover is not heard)
            Scene& c = s[kSceneCrossoverWalk];
            c.length = 2.0;
            unitLane (c, kTargetMidX, {{0, 400}, {0.25, 1200}, {0.5, 1200}, {0.5 + t, 600}, {1, 600}, {1.25, 2400}, {1.5, 2400}, {1.5 + t, 400}, {2, 400}});
            unitLane (c, kTargetMidLevel, {{0, -12}, {2, -12}});
            unitLane (c, kTargetHighX,
                      {{0, 8000}, {0.25, 3000}, {0.5, 3000}, {0.5 + t, 6000}, {1, 6000}, {1.25, 4000}, {1.5, 4000}, {1.5 + t, 9000}, {2, 9000}});
        }
        {
            // Slow Phrase: 32 beats
            Scene& c = s[kSceneSlowPhrase];
            c.length = 32.0;
            unitLane (c, kTargetSeedBlend, {{0, 0}, {16, 1}, {32, 0}});
            unitLane (c, kTargetBells, {{0, 1}, {12, 1}, {24, 0.4}, {32, 1}});
            unitLane (c, kTargetClose, {{0, open}, {8, open}, {28, 1500}, {32, 1500}});
            unitLane (c, kTargetHighLevel, {{0, 0}, {16, 0}, {28, -12}, {32, -12}});
            unitLane (c, kTargetDirt, {{0, 0.6}, {24, 1}, {32, 1}});
        }
        {
            // Pluck 1/8: two 8th-note plucks a beat
            Scene& c = s[kScenePluck];
            c.length = 1.0;
            unitLane (c, kTargetClose, {{0, 16000}, {0.25, 600}, {0.5, 600}, {0.5, 16000}, {0.75, 600}, {1, 600}});
            unitLane (c, kTargetHighLevel, {{0, 0}, {0.25, -26}, {0.5, -26}, {0.5, 0}, {0.75, -26}, {1, -26}});
        }
        {
            // Buzz Tail: a steady wobble, then an audio-rate buzz for the last two beats
            Scene& c = s[kSceneBuzzTail];
            c.length = 8.0;
            unitLane (c, kTargetWobbleRate, {{0, 3}, {6, 3}, {6, 38}, {8, 38}});
            unitLane (c, kTargetWobbleAmount, {{0, 0.5}, {6, 0.5}, {6, 1}, {8, 1}});
            unitLane (c, kTargetMidLevel, {{0, 0}, {6, 0}, {6, -9}, {8, -9}});
        }
        {
            // Triplet Wobble: the rate steps on a quarter-note triplet grid, gliding a 6th of a beat; Air gated
            Scene& c = s[kSceneTripletWobble];
            c.length = 4.0;
            const double q = 2.0 * t, g = 1.0 / 6.0;
            unitLane (c, kTargetWobbleRate, {{0, 3}, {q - g, 3}, {q, 6}, {2 * q - g, 6}, {2 * q, 9}, {3 * q - g, 9}, {3 * q, 4.5}, {4 * q - g, 4.5},
                                             {4 * q, 12}, {5 * q - g, 12}, {5 * q, 3}, {4, 3}});
            unitLane (c, kTargetWobbleAmount, {{0, 0.9}, {4, 0.9}});
            unitLane (c, kTargetAirLevel, {{0, 0}, {q, 0}, {q, -48}, {2 * q, -48}, {2 * q, 0}, {3 * q, 0}, {3 * q, -48}, {4 * q, -48}, {4 * q, 0},
                                           {5 * q, 0}, {5 * q, -48}, {4, -48}});
        }
        {
            // Scan Cell: per note a jump, then a slow scrub of the timbre (Bells), the Mid band (12 dB down) stepping
            // through the spectrum with Mid X; Liquid Pos sweeps too (heard with Liquid up)
            Scene& c = s[kSceneScanCell];
            c.length = 4.0;
            unitLane (c, kTargetBells, {{0, 0.1}, {0.5, 0.6}, {1, 0.6}, {1, 0.2}, {1.5, 0.9}, {2, 0.9}, {2, 0.0}, {2 + t, 0.5}, {3, 0.5}, {3, 0.3},
                                        {3.5, 1.0}, {4, 1.0}});
            unitLane (c, kTargetLiquid, {{0, 0.2}, {1, 1.0}, {2, 0.2}, {3, 1.0}, {4, 0.2}});
            unitLane (c, kTargetMidX, {{0, 800}, {1, 800}, {1, 1600}, {2, 1600}, {2, 300}, {3, 300}, {3, 1200}, {4, 1200}});
            unitLane (c, kTargetMidLevel, {{0, -12}, {4, -12}});
        }
        {
            // Gate Swap: Mid on the first beat, High on the second (each fading out over the last 8th)
            Scene& c = s[kSceneGateSwap];
            c.length = 2.0;
            unitLane (c, kTargetMidLevel, {{0, 0}, {0.875, 0}, {1, -48}, {1.875, -48}, {2, 0}});
            unitLane (c, kTargetHighLevel, {{0, -48}, {0.875, -48}, {1, 0}, {1.875, 0}, {2, -48}});
            unitLane (c, kTargetDirt, {{0, 0.5}, {0.875, 0.5}, {1, 1}, {1.875, 1}, {2, 0.5}});
        }
    }
};
} // namespace

const char* factorySceneName (int s) { return s >= 0 && s < kNumFactoryScenes ? kSceneNames[s] : "User"; }

const Scene& factoryScene (int s)
{
    static const SceneLibrary lib;
    return lib.s[std::clamp (s, 0, kNumFactoryScenes - 1)];
}

double gesturePosition (int mode, double beats, double lengthBeats, double speed, double position)
{
    const double len = std::max (lengthBeats, 1e-6);
    if (mode == kModeWalk)
    {
        if (speed <= 0.0)
            return std::clamp (position, 0.0, 1.0);
        return triangle (beats * speed / len + position);
    }
    const double u = beats / len - position;
    return u - std::floor (u);
}

} // namespace moistr
