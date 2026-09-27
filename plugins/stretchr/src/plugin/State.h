// Processor state: parameters, then the clip (audio, markers, pitch envelope).
#pragma once

#include "Clip.h"
#include "Params.h"

#include "pluginterfaces/base/ibstream.h"

#include <array>

namespace stretchr {

struct State
{
    std::array<double, kNumParams> norm {};
    std::array<bool, kNumParams> has {};
    bool hasClip = false;
    Clip clip;
};

bool writeState (Steinberg::IBStream* stream, const State& s);
// withClip = false stops after the parameters (the controller only needs those).
bool readState (Steinberg::IBStream* stream, State& s, bool withClip = true);

} // namespace stretchr
