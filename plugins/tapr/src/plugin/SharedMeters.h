// The engine's values, shared from the processor to the editor (same process only).
#pragma once

#include "pluginkit/Capture.h"

#include "Engine.h"

#include <atomic>

namespace tapr {

struct SharedMeters
{
    Meters meters;                 // the leveller and the curve, for the display
    smacheratr::Meters tailMeters; // the saturator at the end of the chain
    std::atomic<int> latency {0};    // all of it, as the host is told
    std::atomic<int> ownLatency {0}; // without the end saturator's
    std::atomic<double> sampleRate {48000.0};
    pk::CaptureBuffer capture; // the output, for the Basic page's capture band
    void retain () { refs.fetch_add (1); }
    void release ()
    {
        if (refs.fetch_sub (1) == 1)
            delete this;
    }

private:
    std::atomic<int> refs {1};
};

} // namespace tapr
