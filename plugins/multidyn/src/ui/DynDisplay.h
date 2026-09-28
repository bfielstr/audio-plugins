// The Multidyn display, laid out like Live's: one lane per band (highest band on top) with the
// Below block (left of the below threshold) and the Above block (right of the above threshold),
// the input level (thin bar) and output level (thick bar). The value fields on either side of the
// graph are separate views placed over the display by the editor.
//   drag a block edge          move that threshold          (Shift: fine)
//   drag inside a block        up = louder, down = quieter  (changes the ratio: Above up = lower
//                              ratio / expansion, Below up = higher ratio / upward compression)
//   Cmd/Ctrl while dragging    same change on every band
//   Alt/Option while dragging  above and below together for this band
//   double-click a block       reset its ratio to 1:1 (no processing)
#pragma once

#include "../core/Params.h"
#include "../plugin/Meters.h"

#include "pluginkit/ui/Widgets.h"

#include "vstgui/lib/cview.h"

#include <functional>
#include <vector>

namespace multidyn {

class DynDisplay : public VSTGUI::CView
{
public:
    static constexpr double kMinDb = -80.0, kMaxDb = 0.0;
    static constexpr double kHeader = 14.0, kScaleHeight = 16.0;
    // value columns left (Below) and right (Above, Att/Rel) of the graph
    static constexpr double kLeftCol = 76.0, kRightCol = 156.0;

    using MeterSource = std::function<Meters* ()>;
    DynDisplay (const VSTGUI::CRect& r, pk::ParamHost* host, MeterSource meters);

    void draw (VSTGUI::CDrawContext* ctx) override;
    void onMouseDownEvent (VSTGUI::MouseDownEvent& e) override;
    void onMouseMoveEvent (VSTGUI::MouseMoveEvent& e) override;
    void onMouseUpEvent (VSTGUI::MouseUpEvent& e) override;
    void onMouseExitEvent (VSTGUI::MouseExitEvent& e) override;
    void idle (); // animates the meters

    enum class Hit { None, BelowEdge, AboveEdge, BelowBlock, AboveBlock };
    VSTGUI::CRect laneRect (int band) const;  // the full-width lane
    VSTGUI::CRect graphRect (int band) const; // the graph part of the lane
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
    MeterSource meters;
    Hit dragHit = Hit::None;
    std::vector<Target> targets;
    VSTGUI::CPoint downPoint;
    float shownIn[kNumBands] {}, shownOut[kNumBands] {}, shownGain[kNumBands] {};
};

} // namespace multidyn
