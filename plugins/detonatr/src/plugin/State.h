#pragma once

#include "Params.h"

#include "pluginterfaces/base/ibstream.h"

#include <array>

namespace detonatr {

// The parameters (each one's ID and normalized value).
struct State
{
    std::array<double, kNumParams> norm {};
    std::array<bool, kNumParams> has {};
    bool fromOldDetonatr = false; // the stream was a version 1 or 2 state: it loaded as the defaults
};

// Version 3: the rebuilt Detonatr (the user's explosion chain), a new parameter table from ID 0.
// Versions 1 and 2 (the Clean / Tone / Multiband / Transient / Saturator Detonatr, whose stream also
// held recordings) load as the defaults: the old stages are gone, by the user's choice.
constexpr Steinberg::int32 kStateVersion = 3;
constexpr Steinberg::int32 kFirstRebuiltVersion = 3;
// version: what the stream says it is (the tests write an older one)
bool writeState (Steinberg::IBStream* stream, const State& s, Steinberg::int32 version = kStateVersion);
bool readState (Steinberg::IBStream* stream, State& s);

} // namespace detonatr
