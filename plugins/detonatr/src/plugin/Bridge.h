// Shared, in-process state between the processor (audio thread) and the controller/editor (UI
// thread): the displays' levels and the latency. The processor owns it and hands a pointer to the
// controller in a connection message, so both must live in the same process.
#pragma once

#include "Engine.h"

#include <atomic>

namespace detonatr {

class Bridge
{
public:
    void retain () { refs.fetch_add (1); }
    void release ()
    {
        if (refs.fetch_sub (1) == 1)
            delete this;
    }

    std::atomic<int> latency {0}; // what the processor reports
    Meters meters;

private:
    ~Bridge () = default;
    std::atomic<int> refs {1};
};

} // namespace detonatr
