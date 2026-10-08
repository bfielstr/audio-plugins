// A rack slot's Presets control (a pk::PresetBar on the slot's row): the presets of the slot's effect's
// own plug-in (RackPresetIO.h), loaded into and saved from the slot's parameters (RackPresets.h). The
// whole Presets menu of the plug-in: Init, its factory presets, the user's presets in its folder (with
// their categories and tags, the Tags filter), Save / Save As / Rename / Edit Tags / Delete, Save as
// Default / Load Default / Reset Default (its saved default: a new instance of the plug-in and a new slot
// of the effect start from it) and Save / Load Preset File.
#pragma once

#include "pluginkit/vst/PresetBar.h"

#include <memory>

namespace smemplr {

class Controller;

std::shared_ptr<pk::PresetSource> makeSlotPresets (Controller* controller, int slot);

} // namespace smemplr
