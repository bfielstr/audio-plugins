// The engine's analyser feed and band meters, shared from the processor to the editor (same process only).
#pragma once

#include "Engine.h"

#include <atomic>

namespace gently {

struct SharedMeters
{
    Meters meters;
    std::atomic<double> sampleRate {48000.0};
    smacheratr::Meters tailMeters; // the saturator at the end of the chain
    void retain () { refs.fetch_add (1); }
    void release ()
    {
        if (refs.fetch_sub (1) == 1)
            delete this;
    }

private:
    std::atomic<int> refs {1};
};

} // namespace gently
