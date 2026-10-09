// Moistr's Loop Lock display (0.30): an overview of the window (LoopWindow.h: the slowest cycle of everything that moves),
// as a sampler shows its sample. Every active modulator's curve across the whole window, overlaid (the bells, the High
// Shelf, the moving bands, the gesture's lanes, Wobble, PARA's paths; each 0 .. 1), the loop region shaded with its
// Start and End handles, and while Loop Lock runs a playhead where the motion clock is. The title gives the window's
// length (beats and seconds) and what sets it (the slowest part); the footer the region and Length.
//
// The mouse: drag a handle to move Start or End; drag inside the region to slide it (its length kept); a double-click
// takes it back to the whole window. The curves' points are worked out again only when the settings or the window
// change; the playhead moves over them.
#pragma once

#include "Engine.h"
#include "Params.h"

#include "pluginkit/ui/CachedLayer.h"
#include "pluginkit/ui/Widgets.h"

#include "vstgui/lib/cview.h"

#include <functional>
#include <string>
#include <vector>

namespace moistr {

class LoopView : public VSTGUI::CView
{
public:
    using MeterSource = std::function<const Meters* ()>;
    using UserSource = std::function<const Scene* ()>; // the user gesture as the engine plays it (nullptr: none)
    LoopView (const VSTGUI::CRect& r, pk::ParamHost* host, MeterSource meters, UserSource user);
    void draw (VSTGUI::CDrawContext* ctx) override;
    void idle ();
    void onMouseDownEvent (VSTGUI::MouseDownEvent& e) override;
    void onMouseMoveEvent (VSTGUI::MouseMoveEvent& e) override;
    void onMouseUpEvent (VSTGUI::MouseUpEvent& e) override;
    void onMouseCancelEvent (VSTGUI::MouseCancelEvent& e) override;

    // the plot's rectangle (the window across it), for the mouse and the host test
    VSTGUI::CRect plot () const;

private:
    enum class Drag { None, Start, End, Region };
    double tempo () const;
    double windowNow () const; // beats
    void paintCurves (VSTGUI::CDrawContext* ctx, const VSTGUI::CRect& p, double window);
    void finish ();

    pk::ParamHost* host;
    MeterSource meters;
    UserSource user;
    uint64_t layerKey = 0, curveKey = 1;
    std::vector<std::vector<double>> curveValues;
    Drag drag = Drag::None;
    double grabAt = 0.0, grabStart = 0.0, grabEnd = 1.0;
    double shownPos = -1.0, shownWindow = 0.0;
};

} // namespace moistr
