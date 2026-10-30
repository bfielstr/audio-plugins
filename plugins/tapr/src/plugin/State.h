#pragma once

#include "Params.h"

#include "pluginterfaces/base/ibstream.h"

#include <array>

namespace tapr {

struct State
{
    std::array<double, kNumParams> norm {};
    std::array<bool, kNumParams> has {};
};

// A state's Drive saved before version 2 (0 .. 100 %) at the same percent on today's 0 .. 200 % (readState
// does it).
void convertDrive (State& s);
bool writeState (Steinberg::IBStream* stream, const State& s);
bool readState (Steinberg::IBStream* stream, State& s);

} // namespace tapr
