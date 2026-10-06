#pragma once

#include "Params.h"

#include "pluginterfaces/base/ibstream.h"

#include <array>

namespace ciphr {

struct State
{
    std::array<double, kNumParams> norm {};
    std::array<bool, kNumParams> has {};
};

// The state's version (kStateVersion in State.cpp) says how to read a state written by an older build:
// every change to what a saved value means gets a new version and a conversion in readState.
bool writeState (Steinberg::IBStream* stream, const State& s);
bool readState (Steinberg::IBStream* stream, State& s);

} // namespace ciphr
