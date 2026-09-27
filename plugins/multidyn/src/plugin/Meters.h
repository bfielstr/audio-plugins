// Live meter values shared from the processor (audio thread) to the editor. The processor owns
// it and passes a pointer to the controller in a connection message (same process only).
#pragma once

#include "Params.h"

#include <array>
#include <atomic>

namespace multidyn {

struct Meters
{
    std::array<std::atomic<float>, kNumBands> inputDb {};
    std::array<std::atomic<float>, kNumBands> outputDb {};
    std::array<std::atomic<float>, kNumBands> gainDb {};
    std::atomic<bool> sidechainConnected {false};

    Meters ()
    {
        for (int b = 0; b < kNumBands; ++b)
        {
            inputDb[(size_t)b] = -100.0f;
            outputDb[(size_t)b] = -100.0f;
            gainDb[(size_t)b] = 0.0f;
        }
    }
    void retain () { refs.fetch_add (1); }
    void release ()
    {
        if (refs.fetch_sub (1) == 1)
            delete this;
    }

private:
    std::atomic<int> refs {1};
};

} // namespace multidyn
