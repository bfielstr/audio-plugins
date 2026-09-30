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

// Version 2: the Multiband stage's OTT gain staging (1 is migrated with multidyn::migrateOldBakedNorm).
constexpr Steinberg::int32 kStateVersion = 2;
// version: what the stream says it is (the tests write an older one)
bool writeState (Steinberg::IBStream* stream, const State& s, Steinberg::int32 version = kStateVersion);
// withRecordings false: only the parameters (the controller's copy of the state).
bool readState (Steinberg::IBStream* stream, State& s, bool withRecordings = true);

} // namespace detonatr
