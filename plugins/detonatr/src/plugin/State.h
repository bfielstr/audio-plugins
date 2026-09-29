#pragma once

#include "Params.h"
#include "Tone.h"

#include "pluginterfaces/base/ibstream.h"

#include <array>
#include <memory>
#include <string>

namespace detonatr {

// The parameters, then the Tone stage's recordings (their audio, so a project keeps them).
struct State
{
    std::array<double, kNumParams> norm {};
    std::array<bool, kNumParams> has {};
    struct Recording
    {
        std::shared_ptr<const Carrier> audio; // null: empty
        std::string name;
    };
    std::array<Recording, kCarrierSlots> recordings;
    bool hasRecordings = false; // the stream had the recordings' section
};

bool writeState (Steinberg::IBStream* stream, const State& s);
// withRecordings false: only the parameters (the controller's copy of the state).
bool readState (Steinberg::IBStream* stream, State& s, bool withRecordings = true);

} // namespace detonatr
