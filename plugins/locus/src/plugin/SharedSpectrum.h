// The engine's spectrum snapshot, shared from the processor to the editor (same process only).
#pragma once

#include "Engine.h"

#include <atomic>

namespace locus {

struct SharedSpectrum
{
    Spectrum spectrum;
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

} // namespace locus
