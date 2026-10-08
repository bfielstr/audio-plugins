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

// Version 4: the OTT gain staging (3 and older are migrated with migrateOldBaked, Params.h); 6: no Sub
// and High buttons in the saturator; 7: the saturator's Gentlr Slope; 8: its Oversampling (State.cpp).
constexpr Steinberg::int32 kStateVersion = 9;
// version: what the stream says it is (the tests write an older one)
bool writeState (Steinberg::IBStream* stream, const State& s, Steinberg::int32 version = kStateVersion);
bool readState (Steinberg::IBStream* stream, State& s);

} // namespace multidyn
