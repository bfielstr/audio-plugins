// The stage, seen from above: the listener at the bottom, the speakers at +-30 degrees, and every
// Widr of the group as an arc whose angle is its width and whose distance is its depth (Space):
// this one lit in cinnabar, the others dashed copper ("Widr 2 · Wide"). Beyond 100 % the arc passes the
// speakers. Under it, this instance's gain per band after the Mono Guard and the group (outlined where
// it gives way to the others), dim below Mono Below.
//   drag an end of the lit arc      Width
//   drag up / down elsewhere        Space
//   double-click                    reset Width and Space
//   Shift                           fine
#pragma once

#include "Mix.h"
#include "Params.h"

#include "pluginkit/ui/CachedLayer.h"
#include "pluginkit/ui/Widgets.h"

#include "vstgui/lib/cview.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace widr {

class Controller;

class StageView : public VSTGUI::CView
{
public:
    static constexpr double kStrip = 52.0; // the band strip at the bottom

    StageView (const VSTGUI::CRect& r, pk::ParamHost* host, Controller* controller);
    void draw (VSTGUI::CDrawContext* ctx) override;
    void onMouseDownEvent (VSTGUI::MouseDownEvent& e) override;
    void onMouseMoveEvent (VSTGUI::MouseMoveEvent& e) override;
    void onMouseUpEvent (VSTGUI::MouseUpEvent& e) override;
    void onMouseExitEvent (VSTGUI::MouseExitEvent& e) override;
    void idle ();

    // geometry, public for the tests
    VSTGUI::CPoint listener () const;
    double radiusFor (double space) const;
    // the arc's half-angle in degrees: 100 % spans the speakers (+-30), beyond it goes past them
    // (+-80 at 200 %)
    static double angleFor (double width)
    {
        width = width < 0.0 ? 0.0 : (width > 2.0 ? 2.0 : width);
        const double a = width <= 1.0 ? 30.0 * width : 30.0 + 50.0 * (width - 1.0);
        return a < 1.5 ? 1.5 : a;
    }
    static double widthFor (double a)
    {
        a = a < 0.0 ? 0.0 : (a > 80.0 ? 80.0 : a);
        return a <= 30.0 ? a / 30.0 : 1.0 + (a - 30.0) / 50.0;
    }
    VSTGUI::CPoint arcEnd (bool right) const; // this instance's arc ends

    // the group as the editor sees it
    struct Member
    {
        uint64_t id = 0;
        int role = kSupport, number = 1;
        float width = 0.0f, space = 0.0f;
        bool self = false;
    };
    const std::vector<Member>& members () const { return shown; }
    int groupSize () const { return (int)shown.size (); }

private:
    enum class Drag { None, Width, Space };
    Drag hit (const VSTGUI::CPoint& p) const;
    VSTGUI::CRect field () const;
    VSTGUI::CRect strip () const;

    pk::ParamHost* host;
    Controller* controller;
    Drag drag = Drag::None;
    VSTGUI::CPoint down;
    double startWidth = 0.0, startSpaceN = 0.0, widthAtDown = 0.0;
    std::vector<Member> shown;
    std::array<float, kBands> gains {}, yields {};
    uint64_t shownKey = 0; // what the last repaint showed (idle repaints when it changes)
    // editor-side liveness of the registry slots (heartbeat changes, in idle calls)
    std::array<uint32_t, Registry::kSlots> beats {};
    std::array<int, Registry::kSlots> still {};
    std::array<uint64_t, Registry::kSlots> ids {};
};

} // namespace widr
