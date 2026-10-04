// The engine's level snapshot, shared from the processor to the editor (same process only).
#pragma once

#include "Engine.h"

#include <atomic>

namespace smacheratr {

struct SharedMeters
{
    Meters meters; // (its latency: the engine's, as it runs)
    std::atomic<double> sampleRate {48000.0};
    void retain () { refs.fetch_add (1); }
    void release ()
    {
        if (refs.fetch_sub (1) == 1)
            delete this;
    }

private:
    std::atomic<int> refs {1};
};

} // namespace smacheratr
