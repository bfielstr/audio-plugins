// Presets: .vstpreset files (the standard VST3 format, so hosts can read them too) in the user's
// preset folder, and the messages that carry the processor's state between the processor and the
// controller for saving and loading them from the editor.
#pragma once

#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/vst/ivstmessage.h"

#include <string>
#include <vector>

namespace Steinberg::Vst {
class AudioEffect;
}

namespace pk::presets {

struct Entry
{
    std::string name, path;
};

// The user's preset folder for a plug-in (created if missing; empty if that fails):
//   Windows  Documents\VST3 Presets\bfielstr\<plugin>
//   macOS    ~/Library/Audio/Presets/bfielstr/<plugin>
//   Linux    ~/.vst3/presets/bfielstr/<plugin>
std::string userFolder (const char* pluginName);
std::vector<Entry> list (const std::string& folder); // sorted by name
std::string nameOf (const std::string& path);        // file name without the extension
std::string withExtension (const std::string& path); // adds .vstpreset if missing

bool write (const std::string& path, const Steinberg::FUID& classId, const std::vector<char>& component,
            const std::vector<char>& controller);
bool read (const std::string& path, const Steinberg::FUID& classId, std::vector<char>& component,
           std::vector<char>& controller);

// Messages between the two halves of a plug-in (and from a host test to the controller).
constexpr const char* kMsgGetState = "pk.preset.getState"; // controller -> processor
constexpr const char* kMsgState = "pk.preset.state";       // processor -> controller: "data"
constexpr const char* kMsgSetState = "pk.preset.setState"; // controller -> processor: "data"
constexpr const char* kMsgSave = "pk.preset.save";         // -> controller: "path"
constexpr const char* kMsgLoad = "pk.preset.load";         // -> controller: "path"
constexpr const char* kAttrData = "data";
constexpr const char* kAttrPath = "path";

// Processors call this from notify(): answers the state request and applies a pushed state.
bool handleProcessorMessage (Steinberg::Vst::AudioEffect& fx, Steinberg::Vst::IMessage* message);

} // namespace pk::presets
