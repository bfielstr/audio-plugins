// The two bands on a frequency display: each band's curve as it is right now (moving with its shape),
// the range its shape covers (dashed: the gain swing, or the frequency sweep's ends), and a handle at
// its setting.
//   handle, sideways / up-down    the band's Frequency / Gain
//   a band's edge, sideways       its Width (the band stays centred)
//   wheel on a handle             its Width
//   Alt + drag a band sideways    its Width (the band stays centred; Shift: fine)
//   click a handle                shows that band's controls
#pragma once

#include "../core/Engine.h"
#include "../core/Params.h"

#include "pluginkit/ui/Widgets.h"

#include "vstgui/lib/cview.h"

#include <functional>

namespace wubr {

class BandView : public VSTGUI::CView
{
public:
    static constexpr double kMinHz = 20.0, kMaxHz = 20000.0, kMaxDb = 24.0;
    using MeterSource = std::function<const Meters* ()>;

    BandView (const VSTGUI::CRect& r, pk::ParamHost* host, MeterSource meters);
    void draw (VSTGUI::CDrawContext* ctx) override;
    void onMouseDownEvent (VSTGUI::MouseDownEvent& e) override;
    void onMouseMoveEvent (VSTGUI::MouseMoveEvent& e) override;
    void onMouseUpEvent (VSTGUI::MouseUpEvent& e) override;
    void onMouseCancelEvent (VSTGUI::MouseCancelEvent& e) override;
    void onMouseWheelEvent (VSTGUI::MouseWheelEvent& e) override;
    void idle ();
    std::function<void (int)> onBandPicked;
    int selected = 0; // the band whose controls are shown (drawn on top)

    double xOfHz (double hz) const;
    double yOfDb (double db) const;
    VSTGUI::CPoint handle (int band) const;
    double edgeX (int band, bool high) const; // the band's edges (Width octaves apart around its centre)

private:
    int hit (const VSTGUI::CPoint& p) const;     // a handle, or -1
    int hitEdge (const VSTGUI::CPoint& p) const; // a band's edge, or -1 (a band that is on)
    int hitBody (const VSTGUI::CPoint& p) const; // a band that is on, between its edges (the selected one first), or -1
    bool live () const;

    pk::ParamHost* host;
    MeterSource meters;
    int drag = -1;
    bool dragEdge = false;
    bool dragWidth = false; // Alt held on a band: sideways sets its width
    VSTGUI::CPoint down;
    double startFreq = 0.0, startGain = 0.0, startWidth = 1.5;
    float shownFreq[kBands] = {120.0f, 2000.0f}, shownDb[kBands] = {0.0f, 0.0f};
    uint32_t lastBlocks = 0;
    int idleSinceBlock = 1 << 20;
};

} // namespace wubr
