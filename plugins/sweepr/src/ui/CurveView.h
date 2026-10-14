// The two bands' frequency response, in the style of a two-band EQ's screen: +18 / 0 / -18 dB, a grid from
// 31 Hz to 16 kHz. The curve now (the bands where their LFOs have swept them: a solid line over a faint
// fill, a dot on each band's swept frequency) over the curve at the knobs' settings (dashed), with a bar
// under each handle that shows how far its LFO sweeps.
//   drag a handle sideways    Freq
//   drag a handle up / down   Gain
//   Alt + drag up / down      Q
//   mouse wheel over a handle Q
//   double-click a handle     reset the band (Freq, Q, Gain)
// While audio runs the curve follows the engine's swept frequencies (Meters); without, it shows the
// settings. The grid is a cached layer; the curves and handles are drawn over it, and the view repaints
// only when a swept frequency moved or a setting changed.
#pragma once

#include "Engine.h"
#include "Params.h"

#include "pluginkit/ui/CachedLayer.h"
#include "pluginkit/ui/Widgets.h"

#include "vstgui/lib/cview.h"

#include <functional>

namespace sweepr {

class CurveView : public VSTGUI::CView
{
public:
    static constexpr double kMinHz = 20.0, kMaxHz = 20000.0, kMinDb = -21.0, kMaxDb = 21.0;
    static constexpr double kScaleH = 14.0; // the frequency labels' strip at the bottom
    using MeterSource = std::function<const Meters* ()>;

    CurveView (const VSTGUI::CRect& r, pk::ParamHost* host, MeterSource meters);
    void draw (VSTGUI::CDrawContext* ctx) override;
    void onMouseDownEvent (VSTGUI::MouseDownEvent& e) override;
    void onMouseMoveEvent (VSTGUI::MouseMoveEvent& e) override;
    void onMouseUpEvent (VSTGUI::MouseUpEvent& e) override;
    void onMouseExitEvent (VSTGUI::MouseExitEvent& e) override;
    void onMouseWheelEvent (VSTGUI::MouseWheelEvent& e) override;
    void idle ();

    double xOfHz (double hz) const;
    double yOfDb (double db) const;
    // a band's handle (its settings: Freq across, Gain up)
    VSTGUI::CPoint handle (int band) const;
    // the frequency a band plays at now (swept while audio runs, else its Freq)
    double liveHz (int band) const;

private:
    double responseDb (double f, double fa, double fb) const;
    int hit (const VSTGUI::CPoint& p) const; // the band whose handle is there, -1: none
    void paintGrid (VSTGUI::CDrawContext* ctx);
    bool live () const { return lastBlocks != 0 && idleSinceBlock < 10; }

    pk::ParamHost* host;
    MeterSource meters;
    int drag = -1;
    bool dragQ = false;
    VSTGUI::CPoint down;
    double startFreq = 0.0, startGain = 0.0, startQ = 0.0;
    float shownHz[kBands] {};
    double shownSr = 48000.0; // the rate the bells run at (the curve is the digital bells')
    uint32_t lastBlocks = 0;
    int idleSinceBlock = 1 << 20;
    pk::CachedLayer gridLayer;
};

} // namespace sweepr
