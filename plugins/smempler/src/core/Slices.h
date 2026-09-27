// Slice computation for Slicing mode. Pure function of (sample, params, user edits) so the
// audio thread and the editor compute identical slices. Allocation-free.
#pragma once

#include "SampleData.h"

#include <vector>

namespace smempler {

// User edits, stored as normalized positions (0..1 of the sample length) so they survive
// sample-rate independent reloads.
struct SliceEdits
{
    std::vector<double> manual;     // user-created slices (drawn white)
    std::vector<double> suppressed; // automatic slices the user deleted or moved
};
using SliceEditsPtr = std::shared_ptr<const SliceEdits>;

struct SliceSettings
{
    int sliceBy = 0;
    double sensitivity = 0.5;
    int division = 0;
    int regions = 2;
    double regionStart = 0; // frames
    double regionEnd = 0;   // frames
    double warpBeats = 16;  // beats spanned by the region
    bool operator== (const SliceSettings& o) const
    {
        return sliceBy == o.sliceBy && sensitivity == o.sensitivity && division == o.division &&
               regions == o.regions && regionStart == o.regionStart && regionEnd == o.regionEnd &&
               warpBeats == o.warpBeats;
    }
    bool operator!= (const SliceSettings& o) const { return !(*this == o); }
};

constexpr int kMaxAutoSlices = 64;
constexpr int kMaxSlices = 128;

struct SliceList
{
    int count = 0;
    int pos[kMaxSlices] {};
    bool manual[kMaxSlices] {};

    int startOf (int i) const { return pos[i]; }
};

void computeSlices (const SampleData& sample, const SliceEdits* edits, const SliceSettings& s, SliceList& out);

// Tolerance (in frames) used when matching suppressed/manual positions with auto slices.
int sliceMatchTolerance (const SampleData& sample);

} // namespace smempler
