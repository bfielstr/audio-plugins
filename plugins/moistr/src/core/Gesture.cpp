#include "Gesture.h"

#include "Params.h"

#include <algorithm>
#include <array>

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
