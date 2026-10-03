#pragma once

#include "Params.h"

namespace dropr::help {

inline const char* forParam (uint32_t id)
{
    switch (id)
    {
        case kSensitivity:
            return "How far (in dB) the sound has to jump above its recent level to count as a hit. "
                   "Low: softer notes and ghost hits start the shape too. High: only the strongest hits do.";
        case kRetrigger:
            return "The shortest time between two hits. Hits closer together than this are ignored, so fast rolls or a "
                   "buzzing note don't restart the shape over and over.";
        case kLength: return "How long the drawn shape takes, from the hit to its last point (the time axis of the display).";
        case kDepth:
            return "How far the bottom of the drawn shape turns the sound down (the top is always 0 dB). "
                   "0 dB: nothing changes. More: the drawn dips get deeper.";
        case kPre:
            return "Starts the shape up to 5 ms before the hit (the plug-in looks ahead), so the shape's first part "
                   "lands on the hit's attack itself. 0: the shape starts as the hit is detected.";
        case kMix: return "Blend of the shaped and the untouched signal.";
        case kOutput: return "Overall output level.";
        default: return nullptr;
    }
}

constexpr const char* kDisplay =
    "The level shape every hit starts: left to right over the Length, top 0 dB, bottom -Depth dB. "
    "After the shape the level stays at its last point until the next hit. "
    "Drag a point to move it; drag a line to bend it; double-click to add a point (up to 8) or remove one; "
    "right-click a line to straighten it. The yellow dot shows where the shape is now, HIT flashes on each hit.";

} // namespace dropr::help
