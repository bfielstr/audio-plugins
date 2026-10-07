#pragma once

#include "Params.h"

#include "pluginterfaces/base/ibstream.h"

#include <array>

namespace widr {

struct State
{
    std::array<double, kNumPluginParams> norm {};
    std::array<bool, kNumPluginParams> has {};
};

bool writeState (Steinberg::IBStream* stream, const State& s);
bool readState (Steinberg::IBStream* stream, State& s);

} // namespace widr
