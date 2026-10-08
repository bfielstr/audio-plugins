// Probr's saved state: the parameters (Record is never saved: a project or a preset always opens with
// the probe off), then the Label and the Folder ("" the default folder) as UTF-8 text.
#pragma once

#include "Params.h"

#include "pluginterfaces/base/ibstream.h"

#include <array>
#include <string>

namespace probr {

struct State
{
    std::array<double, kNumParams> norm {};
    std::array<bool, kNumParams> has {};
    std::string label;
    std::string folder;
};

// The label a new probe starts with.
constexpr const char* kDefaultLabel = "probe";

bool writeState (Steinberg::IBStream* stream, const State& s);
bool readState (Steinberg::IBStream* stream, State& s);

} // namespace probr
