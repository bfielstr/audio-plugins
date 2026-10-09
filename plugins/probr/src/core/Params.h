// Probr parameters. IDs are persisted in projects: only ever append.
#pragma once

#include "pluginkit/ParamTable.h"

#include <cstdint>

namespace probr {

enum ParamId : uint32_t
{
    kRecord = 0, // Off / Armed: whether the probe writes takes (Recording is shown while one is written)
    kMode,       // While Playing (a take per play start) / Always (from arming until Off)
    kNumParams
};

// pinned: these numbers are in saved projects
static_assert (kRecord == 0 && kMode == 1 && kNumParams == 2, "Probr's parameter IDs are fixed");

enum Record { kRecordOff = 0, kRecordArmed };
enum Mode { kModeWhilePlaying = 0, kModeAlways };

// Hidden parameter that receives MIDI pitch bend through IMidiMapping (VST3 delivers pitch bend as a
// parameter, not as an event); outside the table, not saved.
enum MidiParamId : uint32_t { kMidiPitchBend = 1000 };

const pk::ParamTable& paramTable ();
inline double toPlain (uint32_t id, double n) { return paramTable ().toPlain (id, n); }
inline double toNormalized (uint32_t id, double p) { return paramTable ().toNormalized (id, p); }
inline double defaultNormalized (uint32_t id) { return paramTable ().defaultNormalized (id); }

} // namespace probr
