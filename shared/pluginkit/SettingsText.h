// An effect's settings as plain text, for copying them between a plug-in and the same effect in
// Smemplr's rack (through the system clipboard). One line per parameter: its own ID, its normalized
// value and, for the reader, its name:
//
//   bfielstr settings: Para
//   0 0.5213000000000001 High-Pass Frequency
//   ...
//
// A paste takes only the lines of the same effect (the name on the first line, case aside), and only
// the IDs the receiver knows; everything it does not find stays as it is.
#pragma once

#include "pluginkit/ParamTable.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace pk {

using SettingValues = std::vector<std::pair<uint32_t, double>>; // (own ID, normalized value)

// names: the effect's table, for the names after the values (nullptr: no names)
std::string settingsToText (const std::string& effect, const SettingValues& values, const ParamTable* names = nullptr);
// The values of a text written by settingsToText for `effect`; false when it is not one (another
// effect's, or not settings at all). Values are clamped to 0 .. 1.
bool settingsFromText (const std::string& text, const std::string& effect, SettingValues& out);
// The effect a settings text is for ("" when it is not one).
std::string settingsEffect (const std::string& text);

} // namespace pk
