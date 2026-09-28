// The mid/side EQ: the mid level (blue line) and the side high-pass with its level (orange), with
// live mid and side level bars on the right.
//   drag the handle sideways   side high-pass frequency
//   drag the handle up / down  side gain
//   double-click               reset both
#pragma once

#include "../core/Params.h"

#include "pluginkit/ui/Widgets.h"

#include "vstgui/lib/cview.h"

#include <functional>

namespace smempler {

class MsView : public VSTGUI::CView
{
public:
    static constexpr double kMinHz = 20.0, kMaxHz = 20000.0, kMinDb = -30.0, kMaxDb = 12.0, kMeterW = 44.0;
    using LevelSource = std::function<void (float& mid, float& side)>;

    MsView (const VSTGUI::CRect& r, pk::ParamHost* host, LevelSource levels);
    void draw (VSTGUI::CDrawContext* ctx) override;
    void onMouseDownEvent (VSTGUI::MouseDownEvent& e) override;
    void onMouseMoveEvent (VSTGUI::MouseMoveEvent& e) override;
    void onMouseUpEvent (VSTGUI::MouseUpEvent& e) override;
    void idle ();

    VSTGUI::CRect plot () const;
    double xOfHz (double hz) const;
    double yOfDb (double db) const;
    VSTGUI::CPoint handle () const;

private:
    pk::ParamHost* host;
    LevelSource levels;
    bool dragging = false;
    VSTGUI::CPoint down;
    double startHz = 0.0, startDb = 0.0;
    float shownMid = 0.0f, shownSide = 0.0f;
};

} // namespace smempler
