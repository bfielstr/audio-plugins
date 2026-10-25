// The engine's levels, shared from the processor to the editor (same process only).
#pragma once

#include "pluginkit/Capture.h"

#include "Engine.h"

#include <atomic>

namespace moistr {

struct SharedMeters
{
    Meters meters; // the bands' movement, for the display
    std::atomic<int> latency {0};
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

} // namespace moistr
