// Low-end spectrum: input (a faint copper body), output (a text-coloured line) and the gain applied
// per band (bottom strip, cinnabar),
// with the focus range shaded.
//   drag a range edge             move Low / High
//   drag inside the range         left/right moves the whole range, up/down sets Contrast
//   double-click inside the range reset Contrast to 0
#pragma once

#include "Params.h"

#include "pluginkit/ui/Widgets.h"

#include "vstgui/lib/cview.h"

#include <vector>

namespace locus {

class Controller;

class SpectrumView : public VSTGUI::CView
{
public:
    static constexpr double kMinHz = 20.0, kMaxHz = 2000.0;
    static constexpr double kMinDb = -96.0, kMaxDb = 0.0;
    static constexpr double kGainStrip = 44.0;

    SpectrumView (const VSTGUI::CRect& r, pk::ParamHost* host, Controller* controller);
    void draw (VSTGUI::CDrawContext* ctx) override;
    void onMouseDownEvent (VSTGUI::MouseDownEvent& e) override;
    void onMouseMoveEvent (VSTGUI::MouseMoveEvent& e) override;
    void onMouseUpEvent (VSTGUI::MouseUpEvent& e) override;
    void onMouseExitEvent (VSTGUI::MouseExitEvent& e) override;
    void idle ();

    double xOfHz (double hz) const;
    double hzOfX (double x) const;
    VSTGUI::CRect plot () const;

private:
    enum class Drag { None, Low, High, Range };
    Drag hit (const VSTGUI::CPoint& p) const;

    pk::ParamHost* host;
    Controller* controller;
    Drag drag = Drag::None;
    VSTGUI::CPoint down;
    double startLow = 0, startHigh = 0, startContrastN = 0;
    bool movedH = false, movedV = false;
    std::vector<float> shownIn, shownOut, shownGain;
    float binHz = 0.0f;
};

} // namespace locus
