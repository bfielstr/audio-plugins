// The preset control in an editor's header: shows the current preset name; click for a menu of
// the presets in the user's folder, Save Preset..., Load Preset File... and Reset to Defaults.
#pragma once

#include "pluginkit/vst/ControllerBase.h"

#include "vstgui/lib/cview.h"

namespace pk {

class PresetBar : public VSTGUI::CView
{
public:
    PresetBar (const VSTGUI::CRect& r, ControllerBase* controller);
    void draw (VSTGUI::CDrawContext* ctx) override;
    void onMouseDownEvent (VSTGUI::MouseDownEvent& e) override;

private:
    void showMenu ();
    void savePresetAs ();
    void loadPresetFile ();

    ControllerBase* ctl;
};

} // namespace pk
