// The engine's meters, shared from the processor to the editor (same process only). The editor
// reads the other Widrs straight from the registry (Mix.h); `meters.slot` says which one is this.
#pragma once

#include "Engine.h"

#include <atomic>

namespace widr {

struct SharedMeters
{
    Meters meters;
    std::atomic<int> latency {0};
    void retain () { refs.fetch_add (1); }
    void release ()
    {
        if (refs.fetch_sub (1) == 1)
            delete this;
    }

private:
    std::atomic<int> refs {1};
};

} // namespace widr
