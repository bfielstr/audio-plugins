// Dropr's shape: the level every hit starts, left to right over the Length (top 0 dB, bottom -Depth
// dB), with where the shape is right now, the gain applied and a flash on each hit.
//   drag a point                     move it (the first and last stay at the ends)
//   drag a line between points       bend it (up / down)
//   double-click                     add a point there (up to 8); on a point: remove it
//   right-click a line               straighten it
#pragma once

#include "Engine.h"
#include "Params.h"
#include "Shape.h"

#include "pluginkit/ui/Widgets.h"

#include "vstgui/lib/cview.h"

#include <functional>

namespace dropr {

class ShapeView : public VSTGUI::CView
{
public:
    using MeterSource = std::function<const Meters* ()>;
    static constexpr double kAxisLeft = 44.0, kAxisBottom = 16.0, kTop = 24.0, kPad = 10.0;

    ShapeView (const VSTGUI::CRect& r, pk::ParamHost* host, MeterSource meters);
    void draw (VSTGUI::CDrawContext* ctx) override;
    void onMouseDownEvent (VSTGUI::MouseDownEvent& e) override;
    void onMouseMoveEvent (VSTGUI::MouseMoveEvent& e) override;
    void onMouseUpEvent (VSTGUI::MouseUpEvent& e) override;
    void onMouseCancelEvent (VSTGUI::MouseCancelEvent& e) override;
    void onMouseExitEvent (VSTGUI::MouseExitEvent& e) override;
    void idle ();

    VSTGUI::CRect plot () const; // the shape's area: time 0..1 across, level 0..1 up
    double xOf (double x) const;
    double yOf (double y) const;
    Shape shape () const;

private:
    int pointAt (const VSTGUI::CPoint& p) const;   // a point near p, or -1
    int segmentAt (const VSTGUI::CPoint& p) const; // the segment under p
    double timeAt (const VSTGUI::CPoint& p) const;
    double levelAt (const VSTGUI::CPoint& p) const;
    void setPlain (uint32_t id, double v);
    void insertPoint (double x, double y);
    void removePoint (int i);

    pk::ParamHost* host;
    MeterSource meters;
    enum class Drag { None, Point, Bend } drag = Drag::None;
    int dragIndex = -1;
    VSTGUI::CPoint down;
    double startCurve = 0.0;
    float shownPos = 1.0f, shownGain = 0.0f, flash = 0.0f;
    int lastHits = -1;
};

} // namespace dropr
