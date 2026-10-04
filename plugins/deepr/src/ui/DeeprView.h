// Deepr's display: the low end on a log axis (20 Hz - 2 kHz), the sub band shaded copper below Split,
// the dip's curve (dashed at full Depth, filled with the dip it is making now) and, at the right, the
// sub's level with the Threshold and the range where the dip deepens.
//   Split line, sideways                    Split
//   dip handle, sideways / up-down           Dip Freq / Depth (the handle sits at the full depth)
//   Alt (Option) + drag on the handle        Dip Width (right: wider)
//   wheel on the handle (or while held)      Dip Width
//   level meter, up / down                   Threshold
//   double-click or right-click              reset what is under the mouse
//   Shift                                    fine
#pragma once

#include "Engine.h"
#include "Params.h"

#include "pluginkit/ui/CachedLayer.h"
#include "pluginkit/ui/Widgets.h"

#include "vstgui/lib/cview.h"

#include <functional>

namespace deepr {

class DeeprView : public VSTGUI::CView
{
public:
    static constexpr double kMinHz = 20.0, kMaxHz = 2000.0;
    static constexpr double kMaxDb = 2.0, kMinDb = -15.0;          // the dip curve's axis
    static constexpr double kMeterMinDb = -60.0, kMeterMaxDb = 0.0; // the sub level meter's
    static constexpr double kMeterWidth = 46.0, kAxisHeight = 14.0;
    using RateSource = std::function<double ()>;
    using MeterSource = std::function<const Meters* ()>;

    DeeprView (const VSTGUI::CRect& r, pk::ParamHost* host, RateSource rate, MeterSource meters);
    void draw (VSTGUI::CDrawContext* ctx) override;
    void onMouseDownEvent (VSTGUI::MouseDownEvent& e) override;
    void onMouseMoveEvent (VSTGUI::MouseMoveEvent& e) override;
    void onMouseUpEvent (VSTGUI::MouseUpEvent& e) override;
    void onMouseExitEvent (VSTGUI::MouseExitEvent& e) override;
    void onMouseWheelEvent (VSTGUI::MouseWheelEvent& e) override;
    void idle (); // follows the meters

    VSTGUI::CRect plot () const;  // the curve area
    VSTGUI::CRect meter () const; // the sub level meter
    double xOfHz (double hz) const;
    double yOfDb (double db) const;
    double yOfLevel (double db) const; // in the meter
    VSTGUI::CPoint handle () const;

private:
    enum class Drag { None, Handle, Width, Split, Threshold };
    Drag hit (const VSTGUI::CPoint& p) const;
    double sampleRate () const;

    pk::ParamHost* host;
    RateSource rate;
    MeterSource meters;
    Drag drag = Drag::None;
    VSTGUI::CPoint down;
    double startFreq = 0.0, startDepth = 0.0, startWidth = 0.0, startSplit = 0.0, startThreshold = 0.0;
    float shownCut = 0.0f, shownSub = -120.0f; // eased meter readings (dB)
    // The wells, the grids with their scales, the sub band and the dip at full Depth, under what moves:
    // a cached layer (pk::CachedLayer), rebuilt when a setting or a drag changes it.
    pk::CachedLayer baseLayer;
    void paintBase (VSTGUI::CDrawContext* ctx);
    // the dip's response at a gain (dB) along the plot, open or closed down to 0 dB
    VSTGUI::SharedPointer<VSTGUI::CGraphicsPath> dipPath (VSTGUI::CDrawContext* ctx, double gainDb, bool closed) const;
};

} // namespace deepr
