// The shaping curve (input left to right, output bottom to top) with the driven signal's current
// reach highlighted on it.
//   drag up / down     set Drive
//   double-click       reset Drive
#pragma once

#include "Params.h"
#include "Shaper.h"

#include "pluginkit/ui/Widgets.h"

#include "vstgui/lib/cview.h"

namespace smatcheratr {

class Controller;

ShaperSettings shaperSettingsFrom (pk::ParamHost* host);

class ShaperView : public VSTGUI::CView
{
public:
    static constexpr double kRange = 2.0; // input and output range shown, +-

    ShaperView (const VSTGUI::CRect& r, pk::ParamHost* host, Controller* controller);
    void draw (VSTGUI::CDrawContext* ctx) override;
    void onMouseDownEvent (VSTGUI::MouseDownEvent& e) override;
    void onMouseMoveEvent (VSTGUI::MouseMoveEvent& e) override;
    void onMouseUpEvent (VSTGUI::MouseUpEvent& e) override;
    void onMouseExitEvent (VSTGUI::MouseExitEvent& e) override;
    void idle ();

    double xOf (double in) const;
    double yOf (double out) const;

private:
    pk::ParamHost* host;
    Controller* controller;
    bool dragging = false;
    VSTGUI::CPoint down;
    double startDriveN = 0.0;
    float shownIn = 0.0f, shownOut = 0.0f;
};

} // namespace smatcheratr
