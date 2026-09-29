// Binary plug-in state: parameters (by ID, normalized) + the sample reference and edits.
#pragma once

#include "Params.h"
#include "SampleData.h"
#include "Slices.h"

#include "pluginterfaces/base/ibstream.h"

#include <array>
#include <string>

namespace smemplr {

struct PluginState
{
    std::array<double, kNumParams> norm {};
    std::array<bool, kNumParams> has {};
    std::string samplePath;
    SampleOps ops;
    SliceEdits edits;
    bool constantPowerFade = true;
};

bool writeState (Steinberg::IBStream* stream, const PluginState& s);
bool readState (Steinberg::IBStream* stream, PluginState& s);
// Moves the fixed effects of an old state into the rack (readState does it).
void migrateToRack (PluginState& s);

} // namespace smemplr
