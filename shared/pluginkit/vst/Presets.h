// Presets: .vstpreset files (the standard VST3 format, so hosts can read them too) in the user's
// preset folder, the factory presets compiled into the plug-in, the saved default a new instance
// starts from, and the messages that carry the processor's state between the processor and the
// controller for saving and loading them from the editor. The store itself (folders, metadata, tags,
// the menu) is pluginkit/PresetStore.h, which documents the formats.
#pragma once

#include "pluginkit/PresetStore.h"

#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/vst/ivstmessage.h"

#include <string>
#include <vector>

namespace Steinberg::Vst {
class AudioEffect;
}

namespace pk::presets {

// meta: written as the file's MetaInfo chunk (nullptr: none, as files saved before 0.13 are)
bool write (const std::string& path, const Steinberg::FUID& classId, const std::vector<char>& component,
            const std::vector<char>& controller, const Meta* meta = nullptr);
bool read (const std::string& path, const Steinberg::FUID& classId, std::vector<char>& component,
           std::vector<char>& controller);
// A preset file's metadata (empty fields when it has none).
Meta readMeta (const std::string& path);
// Writes the file again with new metadata (its states unchanged).
bool rewriteMeta (const std::string& path, const Steinberg::FUID& classId, const Meta& meta);

// The factory presets compiled into this module (cmake/EmbedPresets.cmake generates the call).
bool registerFactory (const FactoryFile* files, int count);
const std::vector<FactoryFile>& factoryFiles ();

// Processors call this at the end of initialize(): when the user saved a default for the plug-in
// (Save as Default), the processor starts from it. A host loading a project calls setState after
// initialize, so the project's state replaces it. True when a default was applied.
bool applyDefault (Steinberg::Vst::AudioEffect& fx, const Steinberg::FUID& classId, const char* pluginName);

// Messages between the two halves of a plug-in (and from a host test to the controller).
constexpr const char* kMsgGetState = "pk.preset.getState"; // controller -> processor
constexpr const char* kMsgState = "pk.preset.state";       // processor -> controller: "data"
constexpr const char* kMsgSetState = "pk.preset.setState"; // controller -> processor: "data"
constexpr const char* kMsgSave = "pk.preset.save";         // -> controller: "path"
constexpr const char* kMsgLoad = "pk.preset.load";         // -> controller: "path"
// -> controller, which answers on the same message: "items" (binary, UTF-8), the Presets menu's
// titles one per line, sub-menu entries as "Parent/Child" (PresetStore's menuTitles)
constexpr const char* kMsgMenu = "pk.preset.menu";
constexpr const char* kAttrData = "data";
constexpr const char* kAttrPath = "path";
constexpr const char* kAttrItems = "items";

// Processors call this from notify(): answers the state request and applies a pushed state.
bool handleProcessorMessage (Steinberg::Vst::AudioEffect& fx, Steinberg::Vst::IMessage* message);

} // namespace pk::presets
