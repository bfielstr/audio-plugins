// The modulation section's views (Modulation.h): each LFO's handle (drag it onto a control to modulate
// that control's parameter) and its scope, the list of mappings, and the overlay that shows the
// modulation on the controls themselves (a ring around a knob, a line under any other control).
#pragma once

#include "Modulation.h"

#include "vstgui/lib/ccolor.h"
#include "vstgui/lib/clinestyle.h"
#include "vstgui/lib/cview.h"

#include <cstdint>
#include <string>
#include <vector>

namespace smemplr {

class Editor;

// each LFO's colour (its handle, its rings and its rows in the list: cinnabar for all of them) and its
// line style (solid, dashed, dotted, dash-dot), which tells the LFOs apart on the rings
VSTGUI::CColor modColor (int lfo);
VSTGUI::CLineStyle modLineStyle (int lfo);

// "LFO n": press and drag it onto a control; the overlay frames the control it would modulate.
class LfoHandle : public VSTGUI::CView
{
public:
    LfoHandle (const VSTGUI::CRect& r, int lfo, Editor* ed) : CView (r), lfo (lfo), ed (ed) {}
    void draw (VSTGUI::CDrawContext* ctx) override;
    void onMouseDownEvent (VSTGUI::MouseDownEvent& e) override;
    void onMouseMoveEvent (VSTGUI::MouseMoveEvent& e) override;
    void onMouseUpEvent (VSTGUI::MouseUpEvent& e) override;
    void onMouseCancelEvent (VSTGUI::MouseCancelEvent& e) override;

private:
    VSTGUI::CPoint toFrame (VSTGUI::CPoint p) const;
    int lfo;
    Editor* ed;
    bool pressed = false, dragging = false;
    VSTGUI::CPoint down;
};

// The LFO's shape over a cycle, and where it is now.
class LfoScope : public VSTGUI::CView
{
public:
    LfoScope (const VSTGUI::CRect& r, int lfo, Editor* ed) : CView (r), lfo (lfo), ed (ed) {}
    void draw (VSTGUI::CDrawContext* ctx) override;

private:
    int lfo;
    Editor* ed;
};

// A control that a mapping modulates, as the overlay draws it (frame coordinates).
struct ModRing
{
    VSTGUI::CRect r;
    bool knob = false;
    uint32_t target = 0;
    std::vector<size_t> maps; // the mappings on it, in the list's order
};

// Over the whole editor: draws the rings, and the frame around the control an LFO is dragged over. Only a
// knob's rings take the mouse (elsewhere the controls get it): drag a ring up / down for its mapping's
// depth (Shift: fine), right-click it to remove the mapping.
class ModOverlay : public VSTGUI::CView
{
public:
    ModOverlay (const VSTGUI::CRect& r, Editor* ed);
    void setRings (std::vector<ModRing> next); // (repaints where they were and are)
    void setDragTarget (const VSTGUI::CRect& r, int lfo); // an empty rect: none
    void draw (VSTGUI::CDrawContext* ctx) override;
    using CView::hitTest;
    bool hitTest (const VSTGUI::CPoint& where, const VSTGUI::Event& event) override;
    void onMouseDownEvent (VSTGUI::MouseDownEvent& e) override;
    void onMouseMoveEvent (VSTGUI::MouseMoveEvent& e) override;
    void onMouseUpEvent (VSTGUI::MouseUpEvent& e) override;
    void onMouseCancelEvent (VSTGUI::MouseCancelEvent& e) override;

    // a knob's geometry, as pk::Knob draws it: the dial's centre and radius; ring k is outside it
    static void knobDial (const VSTGUI::CRect& r, VSTGUI::CPoint& centre, double& radius);
    static double ringRadius (double dial, size_t k) { return dial + 5.0 + 3.0 * (double)k; }

private:
    bool ringAt (const VSTGUI::CPoint& p, size_t& mapping) const;
    static VSTGUI::CRect area (const ModRing& g);
    Editor* ed;
    std::vector<ModRing> rings;
    VSTGUI::CRect dragRect;
    int dragLfo = 0;
    bool editing = false;
    size_t editMap = 0;
    double lastY = 0.0;
};

// The mappings, a row each: the LFO, the parameter, the depth. Drag a depth up / down to change it
// (Shift: fine), double-click it to turn it over (+ / -); right-click a row or click its x to remove it.
class ModList : public VSTGUI::CView
{
public:
    static constexpr double kRow = 17.0;
    ModList (const VSTGUI::CRect& r, Editor* ed) : CView (r), ed (ed) {}
    void draw (VSTGUI::CDrawContext* ctx) override;
    void onMouseDownEvent (VSTGUI::MouseDownEvent& e) override;
    void onMouseMoveEvent (VSTGUI::MouseMoveEvent& e) override;
    void onMouseUpEvent (VSTGUI::MouseUpEvent& e) override;
    void onMouseCancelEvent (VSTGUI::MouseCancelEvent& e) override;

private:
    Editor* ed;
    bool editing = false;
    size_t editRow = 0;
    double lastY = 0.0;
};

} // namespace smemplr
