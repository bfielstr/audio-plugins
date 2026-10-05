// Orbitr's display geometry, the pure part (no drawing, so the tests check it): where the swarm's
// centre is for a Distance and an Angle, the display's metres-to-pixels mapping (and back), what a
// click grabs, and what a drag of the ball or the listener makes of Distance and Angle.
//
// Metres: x right, y ahead, the listener at the origin. Angle: degrees, 0 straight ahead, + to the
// right, +-180 behind. Pixels: x right, y down.
#pragma once

#include <algorithm>
#include <cmath>

namespace orbitr::geo {

constexpr double kMinDistance = 0.5, kMaxDistance = 20.0; // the Distance parameter's range
constexpr double kPi = 3.14159265358979323846;
constexpr double kBallMargin = 1.15; // the view keeps the ball's radius x this in sight
constexpr double kListenerGrab = 13.0, kBallEdgeGrab = 5.0; // px: how close a click must be

// -180 .. 180
inline double wrapDegrees (double a)
{
    a = std::fmod (a, 360.0);
    if (a > 180.0)
        a -= 360.0;
    else if (a < -180.0)
        a += 360.0;
    return a;
}

struct Polar
{
    double distance = 3.0, angle = 0.0; // m, degrees
};
struct Point
{
    double x = 0.0, y = 0.0;
};

inline Point centreOf (Polar p)
{
    const double a = p.angle * kPi / 180.0;
    return {p.distance * std::sin (a), p.distance * std::cos (a)};
}

// the Distance and Angle of a centre (Distance kept in its range; right on the listener: straight ahead)
inline Polar polarOf (Point c)
{
    const double d = std::hypot (c.x, c.y);
    return {std::clamp (d, kMinDistance, kMaxDistance), d > 1e-9 ? std::atan2 (c.x, c.y) * 180.0 / kPi : 0.0};
}

// metres to pixels and back: the listener at (ox, oy), `scale` pixels per metre
struct Map
{
    double ox = 0.0, oy = 0.0, scale = 1.0;
    double px (double mx) const { return ox + mx * scale; }
    double py (double my) const { return oy - my * scale; }
    double mx (double px) const { return (px - ox) / scale; }
    double my (double py) const { return (oy - py) / scale; }
};

// The mapping for a view (its rectangle in pixels): the listener and the ball (its radius x
// kBallMargin) in sight inside a margin (12 px at the sides, 26 at the top and bottom), at least
// 1 m either side of the listener and 1 m deep; the listener always in the middle across. With the
// ball ahead (Angle 0) and not round the listener, the listener sits at the bottom, as the display
// always showed it.
inline Map fit (double left, double top, double right, double bottom, Polar p, double radius)
{
    const Point c = centreOf (p);
    const double m = radius * kBallMargin;
    const double half = std::max (1.0, std::fabs (c.x) + m);
    double y0 = std::min (0.0, c.y - m), y1 = std::max (0.0, c.y + m);
    if (y1 - y0 < 1.0) // (deepened on the ball's side)
    {
        if (y1 >= -y0)
            y1 = y0 + 1.0;
        else
            y0 = y1 - 1.0;
    }
    const double areaTop = top + 26.0, areaBottom = bottom - 26.0;
    const double areaH = std::max (1.0, areaBottom - areaTop), areaHalfW = std::max (1.0, (right - left) / 2.0 - 12.0);
    Map map;
    map.scale = std::min (areaH / (y1 - y0), areaHalfW / half);
    map.ox = (left + right) / 2.0;
    // the depth centred in the area (it fills it unless the width limits the scale)
    map.oy = areaBottom + y0 * map.scale - (areaH - (y1 - y0) * map.scale) / 2.0;
    return map;
}

enum class Grab { None, Ball, Listener };

// What a click at (x, y) pixels takes: the listener (within kListenerGrab px of it: it wins where the
// ball covers it), else the ball (inside its circle or within kBallEdgeGrab px of its edge).
inline Grab grabAt (const Map& map, Polar p, double radius, double x, double y)
{
    if (std::hypot (x - map.ox, y - map.oy) <= kListenerGrab)
        return Grab::Listener;
    const Point c = centreOf (p);
    if (std::hypot (x - map.px (c.x), y - map.py (c.y)) <= std::max (radius * map.scale, 6.0) + kBallEdgeGrab)
        return Grab::Ball;
    return Grab::None;
}

// A drag by (dx, dy) pixels from where it started (Distance and Angle `start`): the ball follows the
// mouse (the listener staying), or the listener does (the ball staying: the ball moves the other way
// relative to it). The result: the new Distance (in its range) and Angle.
inline Polar dragTo (const Map& map, Grab g, Polar start, double dx, double dy)
{
    if (g == Grab::None)
        return start;
    const double s = g == Grab::Ball ? 1.0 : -1.0;
    Point c = centreOf (start);
    c.x += s * dx / map.scale;
    c.y -= s * dy / map.scale;
    return polarOf (c);
}

// The drag (pixels) that takes `start` to `target` (the inverse of dragTo, for the tests).
inline Point dragFor (const Map& map, Grab g, Polar start, Polar target)
{
    const double s = g == Grab::Ball ? 1.0 : -1.0;
    const Point a = centreOf (start), b = centreOf (target);
    return {s * (b.x - a.x) * map.scale, -s * (b.y - a.y) * map.scale};
}

} // namespace orbitr::geo
