// The preset control in an editor's header: shows the current preset name; click for the Presets
// menu (PresetStore.h's buildMenu): Init, the factory presets and the user's presets (categories as
// sub-menus), the Tags filter, Save / Save As / Rename / Edit Tags / Delete, Save as Default / Load
// Default / Reset Default, and saving or loading a .vstpreset file anywhere. Names and tags are typed
// into a small prompt drawn over the editor.
#pragma once

#include "pluginkit/vst/ControllerBase.h"

#include "vstgui/lib/cview.h"

#include <functional>
#include <string>
#include <vector>

namespace pk {

class PresetBar : public VSTGUI::CView
{
public:
    PresetBar (const VSTGUI::CRect& r, ControllerBase* controller);
    void draw (VSTGUI::CDrawContext* ctx) override;
    void onMouseDownEvent (VSTGUI::MouseDownEvent& e) override;

    // A modal prompt over the editor: a title, text fields (label, initial text) and OK / Cancel.
    // onOk gets the fields' texts and returns an error to show (the prompt stays open) or "" to close.
    struct Field
    {
        std::string label, text;
    };
    using PromptOk = std::function<std::string (const std::vector<std::string>& values)>;
    void prompt (const std::string& title, std::vector<Field> fields, const std::string& okLabel, PromptOk onOk);

private:
    void showMenu ();
    void saveAs ();
    void renameCurrent ();
    void editTags ();
    void deleteCurrent ();
    void savePresetFile ();
    void loadPresetFile ();

    ControllerBase* ctl;
};

} // namespace pk
