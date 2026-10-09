// The engine's meters and history, shared from the processor to the editor (same process only).
#pragma once

#include "pluginkit/Capture.h"

#include "Engine.h"

#include <atomic>

namespace smoothr {

struct SharedMeters
{
    Meters meters;
    std::atomic<double> sampleRate {48000.0};
    smacheratr::Meters tailMeters; // the saturator before the limiter
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

} // namespace smoothr
