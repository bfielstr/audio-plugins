// The engine's values, shared from the processor to the editor (same process only).
#pragma once

#include "pluginkit/Capture.h"

#include "Engine.h"

#include <atomic>

namespace sweepr {

struct SharedMeters
{
    Meters meters;                 // the swept frequencies, for the display
    std::atomic<int> latency {0}; // as the host is told
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

} // namespace sweepr
