// The drawn shape: points from time 0 to 1 with a level (0 at the bottom: -Depth dB, 1 at the top:
// 0 dB) and a curve for the segment after each point (-1 .. 1: 0 is a straight line, above bows it up
// early, below late). Shared by the engine and the editor.
#pragma once

#include "Params.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace dropr {

struct ShapePoint
{
    double x = 0.0, y = 0.0, curve = 0.0;
};

struct Shape
{
    std::array<ShapePoint, kMaxPoints> p {};
    int n = 2;

    // a segment's position t (0..1) bent by its curve
    static double bend (double t, double curve)
    {
        if (std::fabs (curve) < 1e-4)
            return t;
        const double k = 6.0 * curve;
        return (1.0 - std::exp (-k * t)) / (1.0 - std::exp (-k));
    }

    double valueAt (double x) const
    {
        x = std::clamp (x, 0.0, 1.0);
        for (int i = 0; i + 1 < n; ++i)
            if (x <= p[(size_t)i + 1].x || i + 2 == n)
            {
                const ShapePoint& a = p[(size_t)i];
                const ShapePoint& b = p[(size_t)i + 1];
                const double span = b.x - a.x;
                const double t = span > 1e-9 ? std::clamp ((x - a.x) / span, 0.0, 1.0) : 1.0;
                return a.y + (b.y - a.y) * bend (t, a.curve);
            }
        return p[0].y;
    }
};

// The shape from plain parameter values (get (id) -> plain). The first point sits at time 0 and the
// last at 1, and the points are in order.
template <typename Get>
Shape shapeOf (Get get)
{
    Shape s;
    s.n = std::clamp ((int)std::lround (get (kPointCount)), 2, kMaxPoints);
    double last = 0.0;
    for (int i = 0; i < s.n; ++i)
    {
        ShapePoint& q = s.p[(size_t)i];
        q.x = i == 0 ? 0.0 : (i == s.n - 1 ? 1.0 : std::clamp (get (pointParam (i, kPtX)), last, 1.0));
        q.y = std::clamp (get (pointParam (i, kPtY)), 0.0, 1.0);
        q.curve = std::clamp (get (pointParam (i, kPtCurve)), -1.0, 1.0);
        last = q.x;
    }
    return s;
}

} // namespace dropr
