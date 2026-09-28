// The two filters and their sum, in the style of a morphing EQ display: the high-pass in red, the
// low-pass in green, the sum in white, with a handle at each cutoff.
//   drag a handle sideways   cutoff
//   drag a handle up / down  resonance
//   double-click a handle    reset both
#pragma once

#include "Params.h"

#include "pluginkit/ui/Widgets.h"

#include "vstgui/lib/cview.h"

namespace perrera {

class Controller;

class FilterView : public VSTGUI::CView
{
public:
    static constexpr double kMinHz = 20.0, kMaxHz = 20000.0, kMinDb = -36.0, kMaxDb = 18.0;

    FilterView (const VSTGUI::CRect& r, pk::ParamHost* host, Controller* controller);
    void draw (VSTGUI::CDrawContext* ctx) override;
    void onMouseDownEvent (VSTGUI::MouseDownEvent& e) override;
    void onMouseMoveEvent (VSTGUI::MouseMoveEvent& e) override;
    void onMouseUpEvent (VSTGUI::MouseUpEvent& e) override;
    void onMouseExitEvent (VSTGUI::MouseExitEvent& e) override;
    void idle ();

    double xOfHz (double hz) const;
    double hzOfX (double x) const;
    double yOfDb (double db) const;
    // effective cutoffs (tracking and envelope included) and the handles
    void cutoffs (double& hp, double& lp) const;
    VSTGUI::CPoint hpHandle () const;
    VSTGUI::CPoint lpHandle () const;

private:
    enum class Drag { None, Hp, Lp };
    Drag hit (const VSTGUI::CPoint& p) const;

    pk::ParamHost* host;
    Controller* controller;
    Drag drag = Drag::None;
    VSTGUI::CPoint down;
    double startFreq = 0.0, startRes = 0.0;
    float shownHp = 0.0f, shownLp = 0.0f, shownEnv = 0.0f;
    int shownNote = -1;
};

} // namespace perrera
