// The colour EQ curve (applied before the shaper and undone after it) with two handles:
//   left handle, up / down     Amt Lo
//   right handle, up / down    Amt Hi
//   right handle, sideways     Freq
//   double-click a handle      reset its parameters
#pragma once

#include "Params.h"

#include "pluginkit/ui/Widgets.h"

#include "vstgui/lib/cview.h"

namespace smatcheratr {

class Controller;

class ColorView : public VSTGUI::CView
{
public:
    static constexpr double kMinHz = 20.0, kMaxHz = 20000.0, kMaxDb = 24.0;

    ColorView (const VSTGUI::CRect& r, pk::ParamHost* host, Controller* controller);
    void draw (VSTGUI::CDrawContext* ctx) override;
    void onMouseDownEvent (VSTGUI::MouseDownEvent& e) override;
    void onMouseMoveEvent (VSTGUI::MouseMoveEvent& e) override;
    void onMouseUpEvent (VSTGUI::MouseUpEvent& e) override;
    void onMouseExitEvent (VSTGUI::MouseExitEvent& e) override;

    double xOfHz (double hz) const;
    double yOfDb (double db) const;
    VSTGUI::CPoint loHandle () const;
    VSTGUI::CPoint hiHandle () const;

private:
    enum class Drag { None, Lo, Hi };
    Drag hit (const VSTGUI::CPoint& p) const;
    double sampleRate () const;

    pk::ParamHost* host;
    Controller* controller;
    Drag drag = Drag::None;
    VSTGUI::CPoint down;
    double startLo = 0.0, startHi = 0.0, startFreq = 0.0;
    bool movedH = false, movedV = false;
};

} // namespace smatcheratr
