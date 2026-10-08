// The files behind a rack slot's presets (core/RackPresets.h has the mapping): the effect's own plug-in's
// folder, its .vstpreset files (in its state format: HostedState.h, so the plug-in loads what a slot saves
// and the other way round), its saved default and its Menu > Defaults file, and its factory presets.
// Everything here reads or writes files: the controller and the editor call it on the UI thread, never
// the audio thread (the processor only sees the parameter changes that follow).
#pragma once

#include "HostedState.h"

#include "pluginkit/PresetStore.h"

#include <string>
#include <vector>

namespace smemplr::rackio {

const hosted::Codec* codecOf (int type); // nullptr for Empty and the M/S EQ
// The plug-in's user folder, as the plug-in itself makes it (made if missing; "" when that fails).
std::string folderOf (int type);
// The plug-in's folder without making it, and its saved default there (Save as Default).
std::string folderPathOf (int type);
std::string defaultPathOf (int type);
bool hasDefault (int type);
// Its factory presets, parsed against its table and sorted as its own menu lists them.
const std::vector<pk::presets::FactoryPreset>& factoryPresetsOf (int type);

// A .vstpreset of the plug-in (its class ID) as every parameter's value (normalized, by its own IDs).
// False when the file is missing, unreadable, another plug-in's or not a state the plug-in reads.
bool readValues (int type, const std::string& path, std::vector<double>& values);
// Writes the plug-in's values as its .vstpreset (component state only: the plug-in keeps its editor as it
// is when it loads one), meta as its metadata (PlugInName set to the plug-in's). The folder is made.
bool writeValues (int type, const std::string& path, const std::vector<double>& values, const pk::presets::Meta& meta);

// A new slot of the effect, as a new instance of its plug-in starts: its saved default when there is one
// (and it reads), then the Menu > Defaults switches in its folder; else its factory defaults.
// appliedDefault: whether the saved default was applied.
std::vector<double> newSlotValues (int type, bool* appliedDefault = nullptr);

} // namespace smemplr::rackio
