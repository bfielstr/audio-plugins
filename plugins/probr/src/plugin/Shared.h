// What the processor shares with the editor (same process only): the probe's status (state, take,
// elapsed time, levels, free space, problems) and its Label and Folder.
#pragma once

#include "Status.h"

#include <atomic>

namespace probr {

struct Shared
{
    Status status;
    Settings settings;
    void retain () { refs.fetch_add (1); }
    void release ()
    {
        if (refs.fetch_sub (1) == 1)
            delete this;
    }

private:
    std::atomic<int> refs {1};
};

} // namespace probr
