// The Analog curve (input left to right, output bottom to top) with the driven signal's current
// reach highlighted on it, and the pre-limiter's ceiling (after the drive) when it is on.
//   drag up / down     set Drive
//   double-click       reset Drive
// Used by Smacheratr and, through a pk::MappedParamHost, by the saturators built into Multidyn and
// Smemplr; the levels come from a function so it does not depend on a controller.
#pragma once

#include "../core/Engine.h"
#include "../core/Params.h"

#include "pluginkit/ui/CachedLayer.h"
#include "pluginkit/ui/Widgets.h"

#include "vstgui/lib/cview.h"

#include <functional>

namespace smacheratr {

class ShaperView : public VSTGUI::CView
{
public:
    static constexpr double kRange = 2.0; // input and output range shown, +-
    using MeterSource = std::function<const Meters* ()>;

    ShaperView (const VSTGUI::CRect& r, pk::ParamHost* host, MeterSource meters);
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
    MeterSource meters;
    bool dragging = false;
    VSTGUI::CPoint down;
    double startDriveN = 0.0;
    float shownIn = 0.0f, shownOut = 0.0f, shownClarity = 0.0f, shownClarity2 = 0.0f;
    // The well, the grid and the pre-limiter's ceiling, under the signal's reach: a cached layer
    // (pk::CachedLayer). The rest is drawn over it on every repaint (the reach shades the curve and
    // the labels, so they stay on top of it).
    pk::CachedLayer baseLayer;
    void paintBase (VSTGUI::CDrawContext* ctx, bool limiting, double ceil);
    void curve (VSTGUI::CDrawContext* ctx, double from, double to, const VSTGUI::CColor& c, double width);
    uint64_t shownKey = 0; // what the last repaint asked for showed (idle repaints only when it changes)
};

} // namespace smacheratr
