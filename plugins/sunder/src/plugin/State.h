#pragma once

#include "Params.h"

#include "pluginterfaces/base/ibstream.h"

#include <array>

namespace sunder {

struct State
{
    std::array<double, kNumParams> norm {};
    std::array<bool, kNumParams> has {};
};

// A pinchr state's values (version 1) as sunder's: Drive, Feedback and Ratio to the nearest step (readState
// does it).
void convertPinchr (State& s);
bool writeState (Steinberg::IBStream* stream, const State& s);
bool readState (Steinberg::IBStream* stream, State& s);

} // namespace sunder
