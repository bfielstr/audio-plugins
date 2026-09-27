// Binary state: parameters by ID (normalized). Unknown IDs are skipped, missing ones default.
#pragma once

#include "Params.h"

#include "pluginterfaces/base/ibstream.h"

#include <array>

namespace multidyn {

struct State
{
    std::array<double, kNumParams> norm {};
    std::array<bool, kNumParams> has {};
};

bool writeState (Steinberg::IBStream* stream, const State& s);
bool readState (Steinberg::IBStream* stream, State& s);

} // namespace multidyn
