// Sunder's display. At the left, the output stage's response (Drive and Feedback) from 10 Hz to 20 kHz,
// relative to its gain at 1 kHz (+-18 dB; the gain itself in the corner). At the right, the compression
// curve of the Curve at the Ratio: the level out against the level into the detector (-50 to +12 dB), with
// the detector's level now and the gain reduction while audio plays. The grid is a cached layer; the view
// repaints when a setting changed or the meters moved.
#pragma once

#include "Engine.h"
#include "Params.h"

#include "pluginkit/ui/CachedLayer.h"
#include "pluginkit/ui/Widgets.h"

#include "vstgui/lib/cview.h"

#include <functional>

namespace sunder {

class SunderView : public VSTGUI::CView
{
public:
    static constexpr double kMinHz = 10.0, kMaxHz = 20000.0, kMinDb = -18.0, kMaxDb = 18.0;
    static constexpr double kMinIn = -50.0, kMaxIn = 12.0; // the curve's box (dB)
    static constexpr double kScaleH = 14.0; // the frequency labels' strip at the bottom
    using MeterSource = std::function<const Meters* ()>;

    SunderView (const VSTGUI::CRect& r, pk::ParamHost* host, MeterSource meters);
    void draw (VSTGUI::CDrawContext* ctx) override;
    void idle ();

    VSTGUI::CRect responseRect () const; // the response's part of the view
    VSTGUI::CRect curveRect () const;    // the curve's box (empty when the view is too narrow for it)
    double xOfHz (double hz) const;
    double yOfDb (double db) const;

private:
    void paintGrid (VSTGUI::CDrawContext* ctx);
    bool live () const { return lastBlocks != 0 && idleSinceBlock < 10; }

    pk::ParamHost* host;
    MeterSource meters;
    float shownGr = 0.0f, shownLevel = -120.0f;
    uint32_t lastBlocks = 0;
    int idleSinceBlock = 1 << 20;
    pk::CachedLayer gridLayer;
};

} // namespace sunder
