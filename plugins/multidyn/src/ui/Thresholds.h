// A band's Below threshold never goes above its Above threshold: when an edit moves one past the
// other, the other is pushed along with it, so neither can be dragged past the other. Applied to
// every edit made in the editor (the display, the value fields), in Multidyn and in Smempler.
#pragma once

#include "../core/Params.h"

#include "pluginkit/ui/Widgets.h"

namespace multidyn {

// After `id` (a Multidyn parameter) was set to the normalized value `norm` through `host`.
inline void pushThresholds (pk::ParamHost& host, uint32_t id, double norm)
{
    if (id < kBandBase || id >= kBandBase + kMaxBands * kBandBlock)
        return;
    const int band = (int)(id - kBandBase) / kBandBlock, field = (int)(id - kBandBase) % kBandBlock;
    if (field != kBelowThresh && field != kAboveThresh)
        return;
    const double v = toPlain (id, norm);
    const uint32_t other = bandParam (band, field == kBelowThresh ? kAboveThresh : kBelowThresh);
    const double o = host.plainValue (other);
    if (field == kBelowThresh ? v > o : v < o)
        host.setOnce (other, toNormalized (other, v));
}

} // namespace multidyn
