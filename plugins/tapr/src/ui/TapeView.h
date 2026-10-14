// Tapr's display. At the left, the frequency response the tape and the Cut EQ give (Speed's high shelf
// with Tape on, the three Cut bells with Cut on), +-21 dB from 20 Hz to 20 kHz, with a handle on each Cut
// bell:
//   drag a handle sideways    its Freq (within its band's range)
//   drag a handle up / down   its gain (Lo, Mid or Hi)
//   double-click a handle     back to its default Freq and 0 dB
// At the right, the curve: the sine clipper from -1.5 to +1.5 in, with the level reaching it now (two
// markers while audio runs) and the leveller's gain. The grid is a cached layer; the view repaints when a
// setting changed or the meters moved.
#pragma once

#include "Engine.h"
#include "Params.h"

#include "pluginkit/ui/CachedLayer.h"
#include "pluginkit/ui/Widgets.h"

#include "vstgui/lib/cview.h"

#include <functional>

namespace tapr {

class TapeView : public VSTGUI::CView
{
public:
    static constexpr double kMinHz = 20.0, kMaxHz = 20000.0, kMinDb = -21.0, kMaxDb = 21.0;
    static constexpr double kScaleH = 14.0; // the frequency labels' strip at the bottom
    using MeterSource = std::function<const Meters* ()>;

    TapeView (const VSTGUI::CRect& r, pk::ParamHost* host, MeterSource meters);
    void draw (VSTGUI::CDrawContext* ctx) override;
    void onMouseDownEvent (VSTGUI::MouseDownEvent& e) override;
    void onMouseMoveEvent (VSTGUI::MouseMoveEvent& e) override;
    void onMouseUpEvent (VSTGUI::MouseUpEvent& e) override;
    void idle ();

    VSTGUI::CRect responseRect () const; // the response's part of the view
    VSTGUI::CRect curveRect () const;    // the curve's box (empty when the view is too narrow for it)
    double xOfHz (double hz) const;
    double yOfDb (double db) const;
    VSTGUI::CPoint handle (int band) const; // a Cut bell's handle (its Freq across, its gain up)

private:
    double responseDb (double f) const;
    int hit (const VSTGUI::CPoint& p) const; // the Cut band whose handle is there, -1: none
    void paintGrid (VSTGUI::CDrawContext* ctx);
    bool live () const { return lastBlocks != 0 && idleSinceBlock < 10; }

    pk::ParamHost* host;
    MeterSource meters;
    int drag = -1;
    VSTGUI::CPoint down;
    double startGain = 0.0, startFreq = 0.0;
    float shownPeak = 0.0f, shownLevel = 0.0f;
    double shownSr = 48000.0;
    uint32_t lastBlocks = 0;
    int idleSinceBlock = 1 << 20;
    pk::CachedLayer gridLayer;
};

} // namespace tapr
