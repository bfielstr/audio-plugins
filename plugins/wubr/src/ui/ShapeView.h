// A band's drawn shape (one cycle, left to right; top +1, bottom -1), with where the band is in it
// right now and, in Envelope mode, the hold point.
//   drag a point                     move it (the first and last stay at the ends)
//   drag a line between points       bend it (up / down)
//   double-click                     add a point there (up to 8); on a point: remove it
//   Alt-click a point                make it the hold point (Envelope)
//   right-click a line               straighten it
#pragma once

#include "../core/Engine.h"
#include "../core/Params.h"
#include "../core/Shape.h"

#include "pluginkit/ui/Widgets.h"

#include "vstgui/lib/cview.h"

#include <functional>

namespace wubr {

class ShapeView : public VSTGUI::CView
{
public:
    using MeterSource = std::function<const Meters* ()>;

    ShapeView (const VSTGUI::CRect& r, pk::ParamHost* host, int band, MeterSource meters);
    void draw (VSTGUI::CDrawContext* ctx) override;
    void onMouseDownEvent (VSTGUI::MouseDownEvent& e) override;
    void onMouseMoveEvent (VSTGUI::MouseMoveEvent& e) override;
    void onMouseUpEvent (VSTGUI::MouseUpEvent& e) override;
    void idle ();

    double xOf (double x) const;
    double yOf (double y) const;
    Shape shape () const;

private:
    int pointAt (const VSTGUI::CPoint& p) const;   // a point near p, or -1
    int segmentAt (const VSTGUI::CPoint& p) const; // the segment under p
    void setPlain (uint32_t id, double v);
    void insertPoint (double x, double y);
    void removePoint (int i);

    pk::ParamHost* host;
    int band;
    MeterSource meters;
    enum class Drag { None, Point, Bend } drag = Drag::None;
    int dragIndex = -1;
    VSTGUI::CPoint down;
    double startCurve = 0.0;
    float shownPos = 0.0f, shownValue = 0.0f;
};

} // namespace wubr
