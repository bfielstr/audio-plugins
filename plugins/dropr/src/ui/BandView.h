// Dropr's display, after Fuse's: frequency across (20 Hz .. 20 kHz), dB up (+36 .. -96: Fuse's -72 and
// below it room for Negative mode's floor).
//   the bands         shaded columns between the crossovers, each with its live meters: the band's
//                     level (after the Input gain), its level after the gain, and the gain reduction
//                     hanging from 0 dB (the number above it)
//   crossover handles vertical lines between the bands: drag sideways
//   gain points       one per band at its centre, with a smooth curve through them (the band's gain
//                     with Tilt): drag up / down
//   thresholds        the downward threshold (and Negative mode's floor, dotted) and the upward one,
//                     as horizontal lines: drag up / down
// Right-click or double-click a handle, point or line: back to its default.
#pragma once

#include "Engine.h"
#include "Params.h"

#include "pluginkit/ui/Widgets.h"

#include "vstgui/lib/cview.h"

#include <functional>

namespace dropr {

class BandView : public VSTGUI::CView
{
public:
    using MeterSource = std::function<const Meters* ()>;
    static constexpr double kTopDb = 36.0, kBottomDb = -96.0;
    static constexpr double kAxisLeft = 34.0, kAxisBottom = 16.0, kTop = 22.0, kPad = 8.0;

    BandView (const VSTGUI::CRect& r, pk::ParamHost* host, MeterSource meters);
    void draw (VSTGUI::CDrawContext* ctx) override;
    void onMouseDownEvent (VSTGUI::MouseDownEvent& e) override;
    void onMouseMoveEvent (VSTGUI::MouseMoveEvent& e) override;
    void onMouseUpEvent (VSTGUI::MouseUpEvent& e) override;
    void onMouseCancelEvent (VSTGUI::MouseCancelEvent& e) override;
    void onMouseExitEvent (VSTGUI::MouseExitEvent& e) override;
    void idle ();

    VSTGUI::CRect plot () const;
    double xOfHz (double hz) const;
    double hzAt (double x) const;
    double yOfDb (double db) const;
    double dbAt (double y) const;

    // what is under a point of the view (for the mouse and the host test)
    enum class Target { None, Point, Xover, DownThreshold, UpThreshold };
    struct Hit
    {
        Target what = Target::None;
        int index = 0;
    };
    Hit hitAt (const VSTGUI::CPoint& p) const;
    VSTGUI::CPoint pointPos (int band) const;  // a band's gain point
    double xoverX (int j) const;               // a crossover handle's x

private:
    int bands () const;
    void xovers (double* f) const;
    double tiltAt (int band) const; // the Tilt's dB at a band's centre
    void setPlain (uint32_t id, double v);
    uint32_t paramOf (const Hit& h) const;

    pk::ParamHost* host;
    MeterSource meters;
    Hit drag, hover;
    float shownLevel[kMaxBands], shownGain[kMaxBands], shownOut[kMaxBands];
};

} // namespace dropr
