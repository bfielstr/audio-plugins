// The presets of a rack slot's effect: a slot lists and saves the same presets as the effect's own
// plug-in (its factory presets, the user's presets in its folder, Init, its saved default) and a new slot
// starts as a new instance of that plug-in would (its Save as Default preset, then its Menu > Defaults
// switches). This is the mapping between the plug-in's parameters and the slot's, without any files (the
// plug-in's state format and the files are plugin/RackPresetIO.h).
//
// A slot carries every parameter of the effect that has a place in its block (fxBlockOf): loading a
// preset sets each of them from the plug-in's value of the same ID; a parameter without a place (Widr's
// cinema stage, Multidyn's and Wubr's own saturators, Para's and the others' later end-saturator blocks)
// is ignored. Saving writes every parameter of the plug-in: the carried ones from the slot, the others at
// the plug-in's defaults. The end-saturator fields some effects carry in their block (Para's, Levlr's,
// Gentlr's, Widr's and Smoothr's first blocks) are loaded and saved like any other, so a preset keeps
// them when it goes through a slot, although the rack does not run them (a Smacheratr slot does that).
#pragma once

#include "Params.h"

#include "pluginkit/GentlrDefaults.h"
#include "pluginkit/ParamTable.h"
#include "pluginkit/SettingsText.h"

#include <cstdint>
#include <functional>
#include <utility>
#include <vector>

namespace smemplr {

// The plug-in a slot's effect is. nullptr for Empty and the M/S EQ (it has no plug-in of its own, so no
// presets).
struct HostedPlugin
{
    const char* name;            // its preset folder's name and its presets' PlugInName ("Para")
    const char* formerName;      // the name it had before a rename ("Gently"), whose folder it takes over; "" for none
    const pk::ParamTable* table; // its controller's table, by its own IDs (Widr's with the cinema stage)
    pk::GentlrIds gentlrIds;     // what its Menu > Defaults switches set
};
const HostedPlugin* hostedPlugin (int type);

using SlotEdits = std::vector<std::pair<uint32_t, double>>; // (Smemplr ID, normalized)

// The plug-in's values (normalized, by its own IDs; a shorter vector's missing ones at their defaults) as
// edits of slot `slot`'s carried parameters. Values outside 0 .. 1 are clamped; values for IDs the
// effect has no place for are ignored.
SlotEdits slotEditsFor (int slot, int type, const std::vector<double>& values);
// The plug-in's values from a slot: the carried ones from norm (Smemplr ID -> normalized), the others
// at the plug-in's defaults.
std::vector<double> pluginValuesOf (int slot, int type, const std::function<double (uint32_t)>& norm);
// Init: every parameter of the plug-in at its default.
std::vector<double> pluginDefaults (int type);
// A factory preset: Init plus its values (by the plug-in's own IDs; unknown IDs ignored).
std::vector<double> pluginValuesWith (int type, const pk::SettingValues& values);
// A new slot of the effect, as a new instance of its plug-in starts: its saved default (nullptr: none,
// the factory defaults), then the Menu > Defaults switches over it (pk::gentlrDefaultValues).
std::vector<double> newSlotValues (int type, const std::vector<double>* savedDefault, const pk::GentlrDefaults& switches);

} // namespace smemplr
