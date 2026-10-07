// The user's Gentlr defaults for new instances of a plug-in (Menu > Defaults): Gentlr On by Default
// and Advanced On by Default. The parameters are the plug-in's GentlrIds (TailParams.h).
//
// A small text file per plug-in beside its presets and its layouts (<preset folder>/.defaults.txt):
//   # comment
//   gentlr = on
//   advanced = off
// Each switch is on, off or not set (no line for it). Not set, a new instance keeps what it starts with
// (the saved default preset's value, else the factory default); set, a new instance gets it: Gentlr on
// switches on the end saturator (its Saturator switch) and its Gentlr, Gentlr off switches Gentlr off
// (the saturator stays as it was); Advanced sets Gentlr's Advanced mode (in gentlr, its own Advanced).
// A missing or unreadable file, or a line it does not understand, leaves a switch not set; keys it does
// not know are kept when it is written again.
//
// They apply to a new instance only (an inserted plug-in), after the user's saved default preset (Save as
// Default), since the user set them explicitly. A project or preset being loaded replaces them, as it
// replaces every value of a new instance; Init and Load Default leave them out.
#pragma once

#include "pluginkit/TailParams.h"

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace pk {

struct GentlrDefaults
{
    std::optional<bool> gentlrOn, advancedOn;                // (nullopt: not set)
    std::vector<std::pair<std::string, std::string>> other; // keys this version does not know (kept)
};

GentlrDefaults parseGentlrDefaults (const std::string& text);
std::string gentlrDefaultsText (const GentlrDefaults& d);
std::string gentlrDefaultsPath (const std::string& presetFolder); // "" for no folder
GentlrDefaults readGentlrDefaults (const std::string& presetFolder);
bool writeGentlrDefaults (const std::string& presetFolder, const GentlrDefaults& d);

// The values (ID, normalized) the switches set in a new instance of a plug-in with parameters ids (none
// for a switch that is not set or a parameter the plug-in does not have).
std::vector<std::pair<uint32_t, double>> gentlrDefaultValues (const GentlrIds& ids, const GentlrDefaults& d);

} // namespace pk
