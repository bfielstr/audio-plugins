// The preset control in an editor's header: shows the current preset name; click for the Presets
// menu (PresetStore.h's buildMenu): Init, the factory presets and the user's presets (categories as
// sub-menus), the Tags filter, Save / Save As / Rename / Edit Tags / Delete, Save as Default / Load
// Default / Reset Default, and saving or loading a .vstpreset file anywhere. Names and tags are typed
// into a small prompt drawn over the editor.
#pragma once

#include "pluginkit/vst/ControllerBase.h"

#include "vstgui/lib/cview.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace pk {

// What a preset control works on: a plug-in's own presets (ControllerBase, the header's bar), or the
// presets of another plug-in shown on part of this one's parameters (a slot of smemplr's rack lists the
// presets of the effect's own plug-in). The bar draws the name, builds the menu and runs its prompts;
// the source does the loading and saving.
class PresetSource
{
public:
    virtual ~PresetSource () = default;
    virtual std::string presetName () = 0; // shown in the bar ("" for none: "Presets")
    virtual presets::Kind presetKind () = 0;
    virtual std::string presetPath () = 0;   // user file, or factory source path
    virtual std::string presetFolder () = 0; // the user folder (where Save As writes; made if missing)
    virtual std::string& tagFilter () = 0;
    virtual std::vector<presets::MenuEntry> presetMenu (std::vector<presets::Item>* factoryOut, std::vector<presets::Item>* userOut) = 0;
    virtual const std::vector<presets::FactoryPreset>& factoryPresets () = 0;
    virtual bool loadInit () = 0;
    virtual bool loadFactory (int index) = 0;
    virtual bool loadPreset (const std::string& path) = 0;
    virtual bool savePreset (const std::string& path) = 0; // Save (the current user preset) and Save Preset File
    virtual std::string userPresetPath (const std::string& name, const std::string& category) = 0;
    virtual bool saveUserPreset (const std::string& name, const std::string& category, const std::vector<std::string>& tags) = 0;
    virtual bool setPresetTags (const std::string& path, const std::vector<std::string>& tags) = 0;
    virtual bool renamePreset (const std::string& path, const std::string& newName) = 0;
    virtual bool deletePreset (const std::string& path) = 0;
    virtual bool saveAsDefault () = 0;
    virtual bool loadDefault () = 0;
    virtual bool resetDefault () = 0;
};

class PresetBar : public VSTGUI::CView
{
public:
    // the plug-in's own presets
    PresetBar (const VSTGUI::CRect& r, ControllerBase* controller);
    // another source's (the bar keeps it while it lives)
    PresetBar (const VSTGUI::CRect& r, std::shared_ptr<PresetSource> source);
    void draw (VSTGUI::CDrawContext* ctx) override;
    void onMouseDownEvent (VSTGUI::MouseDownEvent& e) override;
    void showMenu ();

    // A modal prompt over the editor: a title, text fields (label, initial text) and OK / Cancel.
    // onOk gets the fields' texts and returns an error to show (the prompt stays open) or "" to close.
    struct Field
    {
        std::string label, text;
    };
    using PromptOk = std::function<std::string (const std::vector<std::string>& values)>;
    void prompt (const std::string& title, std::vector<Field> fields, const std::string& okLabel, PromptOk onOk);

private:
    void saveAs ();
    void renameCurrent ();
    void editTags ();
    void deleteCurrent ();
    void savePresetFile ();
    void loadPresetFile ();

    std::shared_ptr<PresetSource> src;
};

// The same prompt over any editor's content (Menu > Layout > Save Layout As... uses it).
void showPrompt (VSTGUI::CFrame* frame, const std::string& title, std::vector<PresetBar::Field> fields, const std::string& okLabel,
                 PresetBar::PromptOk onOk);

} // namespace pk
