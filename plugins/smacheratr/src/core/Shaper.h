// The shaping curve, "Analog": linear up to +-0.5, then a smooth knee that reaches +-1 with zero
// slope at +-1.5 (Live's Analog Clip). Also the Soft Clip of the post-clip stage. Shared with the
// editor, which draws it.
#pragma once

#include <algorithm>
#include <cmath>

namespace smacheratr {

inline double analogClip (double x)
{
    const double a = std::fabs (x);
    if (a <= 0.5)
        return x;
    const double s = x < 0.0 ? -1.0 : 1.0;
    if (a >= 1.5)
        return s;
    const double u = a - 0.5;
    return s * (0.5 + u - 0.5 * u * u);
}

inline double digitalClip (double x) { return std::clamp (x, -1.0, 1.0); }

} // namespace smacheratr
