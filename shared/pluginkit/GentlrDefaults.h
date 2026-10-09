// The user's Gentlr defaults for new instances of a plug-in (Menu > Defaults): Gentlr On by Default
// and Advanced On by Default. The parameters are the plug-in's GentlrIds (TailParams.h).
//
// A small text file per plug-in beside its presets and its layouts (<preset folder>/.defaults.txt):
//   # comment
//   gentlr = on
//   advanced = off
// Each switch is on, off or not set (no line for it). Not set, a new instance keeps what it starts with
// (the saved default preset's value, else the factory default: the end saturator and its Gentlr on, Advanced
// off, gentlr's own Advanced on; the menu shows a switch not set as that); set, a new instance gets it: Gentlr on
// switches on the end saturator (its Saturator switch) and its Gentlr, Gentlr off switches Gentlr off
// (the saturator stays as it was); Advanced sets Gentlr's Advanced mode (in gentlr, its own Advanced).
// A missing or unreadable file, or a line it does not understand, leaves a switch not set; keys it does
// not know are kept when it is written again.
//
// They apply to a new instance only (an inserted plug-in), after the user's saved default preset (Save as
// Default), since the user set them explicitly. A project or preset being loaded replaces them, as it
// replaces every value of a new instance; Init and Load Default leave them out.
//
// Glue Bands on Touch (Menu > Defaults, in every plug-in) is an editor behaviour, not a parameter: with
// it, a band edge dragged in a Gentlr display (gentlr's, smacheratr's, every end saturator's, smemplr's
// slots) snaps onto a neighbour's facing edge within a few pixels and the two are glued when the drag
// ends (smacheratr/src/ui/BandPush.h); without it (the default) edges move freely and nothing glues by
// itself (the glue switches and the link icons still work, and glued bands stay glued). One setting for
// the whole suite, in the same format in a file of the suite's (<suite folder>/.defaults.txt, beside the
// plug-ins' folders):
//   glue on touch = on
// read each time a drag begins, so a change applies at once to every open editor of every plug-in.
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
    std::optional<bool> glueOnTouch;                         // (the suite's file only: glueOnTouch ())
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

// Glue Bands on Touch: the suite's file (presets::suiteFolder ()), off when it is not set or the file is
// missing. write keeps the file's other lines.
bool glueOnTouch ();
bool writeGlueOnTouch (bool on);

} // namespace pk
