// The preset store, without the VST3 SDK (so the core tests can run it): where presets live, their
// metadata (category, tags), the factory presets compiled into each plug-in, and the Presets menu's
// layout. The .vstpreset files themselves are read and written by vst/Presets.h.
//
// Kinds of preset
//   Init     every parameter at its default: a fresh instance with no saved default. Always first in
//            the menu; not a file, so it cannot be overwritten or deleted.
//   Factory  shipped with the plug-in: text files in plugins/<plugin>/presets/<Category>/<Name>.txt,
//            compiled in at build time (cmake/EmbedPresets.cmake). Read-only.
//   User     .vstpreset files in the user's folder (userFolder); a sub-folder is a category.
//   Default  <user folder>/.default.vstpreset, written by Save as Default: a new instance (and Load
//            Default) starts from it. Reset Default deletes it.
//
// User preset metadata: the standard MetaInfo XML chunk ('Info') of the .vstpreset file:
//
//   <?xml version="1.0" encoding="utf-8"?>
//   <MetaInfo>
//     <Attr id="MediaType" value="VstPreset" type="string" flags="writeProtected"/>
//     <Attr id="PlugInName" value="Gentlr" type="string" flags="writeProtected"/>
//     <Attr id="Name" value="My vocal" type="string"/>
//     <Attr id="bfielstr.Category" value="Vocals" type="string"/>
//     <Attr id="bfielstr.Tags" value="vocal, bright" type="string"/>
//     <Attr id="bfielstr.Comment" value="" type="string"/>
//   </MetaInfo>
//
// Every field is optional: files saved before the metadata (or by a host) have no Info chunk and load
// as before (no tags; the category is the sub-folder they are in).
//
// Factory preset text format (one file per preset, UTF-8):
//
//   # a comment
//   name: De-mud                 (optional: the file name without .txt)
//   category: Mixing             (optional: the sub-folder)
//   tags: mix, low mids          (comma separated)
//   comment: what it is for      (optional)
//   Band 1 Range = -4 dB         <parameter name> = <value as the plug-in shows or accepts it>
//   #12 = 0.5                    (or #<parameter ID>)
//
// Parameters a file does not name are at their defaults (a factory preset is Init plus its lines).
// Unknown names, values the parameter does not accept and values outside its range are errors (the
// factory preset test fails on them).
#pragma once

#include "pluginkit/ParamTable.h"
#include "pluginkit/SettingsText.h"

#include <string>
#include <vector>

namespace pk::presets {

inline constexpr const char* kInitName = "Init";
inline constexpr const char* kDefaultName = "Default";
inline constexpr const char* kExtension = ".vstpreset";
inline constexpr const char* kDefaultFile = ".default.vstpreset";

// ---- metadata
struct Meta
{
    std::string name, plugin, category, comment;
    std::vector<std::string> tags;
};

// "a, b ,c" -> {"a", "b", "c"}: commas or semicolons, trimmed, empty ones dropped, duplicates (case
// aside) dropped, the first spelling kept.
std::vector<std::string> parseTags (const std::string& text);
std::string joinTags (const std::vector<std::string>& tags); // "a, b, c"
bool hasTag (const std::vector<std::string>& tags, const std::string& tag); // case aside
std::string metaToXml (const Meta& m);
// false when the text is not MetaInfo XML; fields it does not have stay as they are
bool metaFromXml (const std::string& xml, Meta& m);
// The XML of a .vstpreset's Info chunk (read without the SDK); false when it has none.
bool readMetaXml (const std::string& path, std::string& xml);

// ---- folders and names
// <presets>/bfielstr: $PK_PRESETS_DIR when set (the tests), else
//   Windows  Documents\VST3 Presets\bfielstr     macOS  ~/Library/Audio/Presets/bfielstr
//   Linux    ~/.vst3/presets/bfielstr
std::string suiteFolder ();
// The user's preset folder for a plug-in (created if missing; empty if that fails).
std::string userFolder (const char* pluginName);
// The same, for a plug-in that was renamed: the first time its folder is made, the presets saved under
// its former name are copied into it (the old folder is left as it is).
std::string userFolder (const char* pluginName, const char* formerName);
// Where Save as Default writes (the folder is not created).
std::string defaultPresetPath (const std::string& pluginName);
std::string nameOf (const std::string& path);        // file name without the extension
std::string withExtension (const std::string& path); // adds .vstpreset if missing
std::string trim (const std::string& s);
// A name a user preset (or category) may have: not empty, no path separators or characters file
// systems refuse, does not start with a dot, and (for a preset) not "Init".
bool validName (const std::string& name, bool category = false);

// ---- the presets a menu lists
struct Item
{
    std::string name, category, path; // path: the file (user presets), the source path (factory)
    std::vector<std::string> tags;
    bool factory = false;
    int factoryIndex = -1;
};
// The .vstpreset files in a user folder and its sub-folders (one level: the categories), with their
// tags; dot files (the saved default) are left out. Sorted by category (none first), then name.
std::vector<Item> listUser (const std::string& folder);
std::vector<std::string> allTags (const std::vector<Item>& items); // sorted, case aside
bool matches (const Item& item, const std::string& tagFilter);    // an empty filter matches all

// ---- factory presets
struct FactoryFile
{
    const char* path; // relative to the plug-in's presets folder, e.g. "Mixing/De-mud.txt"
    const char* text;
};
struct FactoryPreset
{
    std::string name, category, path, comment;
    std::vector<std::string> tags;
    SettingValues values; // (ID, normalized) for the parameters the file names
};
bool parseFactoryPreset (const std::string& text, const std::string& relPath, const ParamTable& table, FactoryPreset& out,
                         std::string& error);

// ---- the Presets menu
enum class Action
{
    None,
    Init,
    Factory,      // index: factory preset
    User,         // index: user preset (in the list given)
    FilterTag,    // tag
    ClearFilter,
    Save,         // overwrite the current user preset
    SaveAs,
    Rename,
    EditTags,
    Delete,
    SaveDefault,
    LoadDefault,
    ResetDefault,
    SaveFile,
    LoadFile,
};

struct MenuEntry
{
    std::string title;
    Action action = Action::None;
    int index = -1;
    std::string tag;
    bool separator = false, heading = false, checked = false, enabled = true;
    std::vector<MenuEntry> sub; // a sub-menu when not empty
};

enum class Kind
{
    None,
    Init,
    Factory,
    User,
    Default,
    File,
};

struct MenuState
{
    Kind kind = Kind::None;
    std::string path;      // the current preset: user file, or factory source path
    std::string tagFilter; // only presets with this tag ("" all)
    bool hasDefault = false;
};

std::vector<MenuEntry> buildMenu (const std::vector<Item>& factory, const std::vector<Item>& user, const MenuState& state);
// Every entry's title, sub-menus as "Parent/Child" (headings and separators left out): host tests.
std::vector<std::string> menuTitles (const std::vector<MenuEntry>& menu);

} // namespace pk::presets
