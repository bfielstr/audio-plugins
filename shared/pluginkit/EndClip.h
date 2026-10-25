// The end of every plug-in's chain: Soft Clip and Hard Clip, each a switch (off by default), Soft first. Soft
// Clip is smacheratr's Analog curve (linear up to +-0.5, then a smooth knee reaching +-1 with zero slope at
// +-1.5); Hard Clip holds +-1 (0 dBFS). Plain curves on the samples: no oversampling, no latency, and below
// +-0.5 (Soft) or +-1 (Hard) the sound passes bit for bit. (They replace the smacheratr end saturator every
// plug-in had, whose parameters stay in the tables, retired: pk::retire.)
#pragma once

#include "pluginkit/ParamTable.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace pk {

inline void addEndClipParams (std::vector<ParamInfo>& v, uint32_t softId, uint32_t hardId)
{
    v.push_back (make::toggle (softId, "Soft Clip", "Soft Clip", false));
    v.push_back (make::toggle (hardId, "Hard Clip", "Hard Clip", false));
}

// the switches' help (the same in every plug-in)
constexpr const char* kSoftClipHelp = "Soft Clip at the very end: a smooth knee from -6 dBFS that rounds peaks off up to 0 dBFS. "
                                      "Off: the sound passes untouched.";
constexpr const char* kHardClipHelp = "Hard Clip at the very end: nothing leaves above 0 dBFS (after Soft Clip when both are on). "
                                      "Off: the sound passes untouched.";

inline float softClip (float x)
{
    const float a = std::fabs (x);
    if (a <= 0.5f)
        return x;
    const float s = x < 0.0f ? -1.0f : 1.0f;
    if (a >= 1.5f)
        return s;
    const float u = a - 0.5f;
    return s * (0.5f + u - 0.5f * u * u);
}

inline float hardClip (float x) { return std::clamp (x, -1.0f, 1.0f); }

// In place on a stereo block.
inline void endClip (float* l, float* r, int n, bool soft, bool hard)
{
    if (soft)
        for (int i = 0; i < n; ++i)
        {
            l[i] = softClip (l[i]);
            r[i] = softClip (r[i]);
        }
    if (hard)
        for (int i = 0; i < n; ++i)
        {
            l[i] = hardClip (l[i]);
            r[i] = hardClip (r[i]);
        }
}

} // namespace pk
