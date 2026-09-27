// The Multidyn display: one lane per band (highest band on top). Each lane shows the
// Below block (left of the below threshold) and the Above block (right of the above threshold),
// the input level (thin bar) and output level (thick bar).
//   drag a block edge          move that threshold          (Shift: fine)
//   drag inside a block        up = louder, down = quieter  (changes the ratio)
//   Cmd/Ctrl while dragging    same change on every band
//   Alt/Option while dragging  above and below together for this band
//   double-click a block       reset its ratio to 1:1
#pragma once

#include "Params.h"

#include "pluginkit/ui/Widgets.h"

#include "vstgui/lib/cview.h"

#include <vector>

namespace multidyn {

class Controller;

class DynDisplay : public VSTGUI::CView
{
public:
    static constexpr double kMinDb = -70.0, kMaxDb = 6.0;
    static constexpr double kScaleHeight = 16.0;

    DynDisplay (const VSTGUI::CRect& r, pk::ParamHost* host, Controller* controller);

    void draw (VSTGUI::CDrawContext* ctx) override;
    void onMouseDownEvent (VSTGUI::MouseDownEvent& e) override;
    void onMouseMoveEvent (VSTGUI::MouseMoveEvent& e) override;
    void onMouseUpEvent (VSTGUI::MouseUpEvent& e) override;
    void onMouseExitEvent (VSTGUI::MouseExitEvent& e) override;
    void idle (); // animates the meters

    enum class Hit { None, BelowEdge, AboveEdge, BelowBlock, AboveBlock };
    VSTGUI::CRect laneRect (int band) const;
    int bands () const;
    double xOf (double db) const;
    Hit hitTest (const VSTGUI::CPoint& p, int& band) const;

private:
    struct Target
    {
        uint32_t id;
        double start;
    };
    std::vector<uint32_t> targetsFor (Hit hit, int band, const VSTGUI::Modifiers& mods) const;

    pk::ParamHost* host;
    Controller* controller;
    Hit dragHit = Hit::None;
    std::vector<Target> targets;
    VSTGUI::CPoint downPoint;
    float shownIn[kNumBands] {}, shownOut[kNumBands] {}, shownGain[kNumBands] {};
};

} // namespace multidyn
