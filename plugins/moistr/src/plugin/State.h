#pragma once

#include "GestureFile.h"
#include "Params.h"

#include "pluginterfaces/base/ibstream.h"

#include <array>

namespace moistr {

struct State
{
    std::array<double, kNumParams> norm {};
    std::array<bool, kNumParams> has {};
    // (version 6) each gesture slot's user gesture (empty: none), so a project plays it without the file
    std::array<GestureData, kNumGestureSlots> user {};
};

bool writeState (Steinberg::IBStream* stream, const State& s);
bool readState (Steinberg::IBStream* stream, State& s);

} // namespace moistr
