// Orbitr's display (Detonatr's Motion page): the orbs seen from above round the listener ("you",
// facing up), with their trails, the ball (Swarm) or the circles' reach (Orbit) round the centre at
// the Distance and Angle, and rings every metre round the listener (every 5 m when far). With Grains,
// each orb swells with its newest grain and wears a ring.
//
// The ball and the listener can be dragged (core/OrbGeometry.h has the geometry):
//   drag the ball (inside it or on its edge)   moves it: Distance and Angle (the listener stays)
//   drag the listener                         moves you: the ball stays on screen, so Distance and
//                                             Angle change the other way
//   Shift                                     fine (a fifth of the mouse's movement)
//   double-click either                       Distance and Angle back to their defaults (3 m, 0)
//   mouse wheel over the ball or the listener Distance
// The view keeps its mapping while a drag lasts (what is held stays under the mouse) and fits the
// listener and the ball again when it ends: after dragging the listener, it is back in the middle.
#pragma once

#include "Engine.h"
#include "OrbGeometry.h"
#include "Params.h"

#include "pluginkit/ui/CachedLayer.h"
#include "pluginkit/ui/Widgets.h"

#include "vstgui/lib/cview.h"

#include <functional>

namespace orbitr {

class OrbView : public VSTGUI::CView
{
public:
    using MeterSource = std::function<const Meters* ()>;
    OrbView (const VSTGUI::CRect& r, pk::ParamHost* host, MeterSource meters);
    void draw (VSTGUI::CDrawContext* ctx) override;
    void idle (); // follows the meters

    void onMouseDownEvent (VSTGUI::MouseDownEvent& e) override;
    void onMouseMoveEvent (VSTGUI::MouseMoveEvent& e) override;
    void onMouseUpEvent (VSTGUI::MouseUpEvent& e) override;
    void onMouseCancelEvent (VSTGUI::MouseCancelEvent& e) override;
    void onMouseExitEvent (VSTGUI::MouseExitEvent& e) override;
    void onMouseWheelEvent (VSTGUI::MouseWheelEvent& e) override;

private:
    pk::ParamHost* host;
    MeterSource meters;
    int orbs = 0;
    // the orbs relative to the swarm's centre (metres), drawn round the centre the parameters set (so
    // they follow a drag at once, with or without audio running)
    float x[Motion::kMaxOrbs] {}, y[Motion::kMaxOrbs] {};
    bool grains = false;
    float grain[Motion::kMaxOrbs] {}; // each orb's newest grain's window (Grains)
    // each orb's trail (the last positions, relative to the centre)
    static constexpr int kTrail = 12;
    float tx[Motion::kMaxOrbs][kTrail] {}, ty[Motion::kMaxOrbs][kTrail] {};
    bool trailSet[Motion::kMaxOrbs][kTrail] {};
    int trailPos = 0;
    uint32_t seen = 0;
    // The well, the metre rings, the ball and the listener (they change only with the Distance, the
    // Angle, the radius, the mapping and what is pointed at or held): a cached layer (pk::CachedLayer)
    // the orbs and their trails are drawn over.
    pk::CachedLayer baseLayer;
    void paintBase (VSTGUI::CDrawContext* ctx, const geo::Map& map);

    // the swarm's place (from the parameters: Distance and Angle) and its Radius
    geo::Polar place () const;
    double radius () const;
    // metres to pixels and back: the view fitted to the listener and the ball, or held during a drag
    geo::Map map () const;
    // dragging
    geo::Grab hover = geo::Grab::None, drag = geo::Grab::None;
    geo::Map held;         // the mapping while dragging
    geo::Polar dragStart;  // Distance and Angle when the drag started
    VSTGUI::CPoint last;   // the mouse's last place
    double dragX = 0.0, dragY = 0.0; // the drag so far (pixels, Shift's moves scaled down)
    void setHover (geo::Grab g);
    void endDrag ();
};

} // namespace orbitr
