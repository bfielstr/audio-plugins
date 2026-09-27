#pragma once

#include "UiKit.h"

#include "Params.h"

#include <string>
#include <vector>

namespace simplr {

// Filter frequency response (drag: x = frequency, y = resonance) or the filter envelope.
class FilterDisplay : public VSTGUI::CView
{
public:
    FilterDisplay (const VSTGUI::CRect& r, ParamHost* h);
    void draw (VSTGUI::CDrawContext* ctx) override;
    void onMouseDownEvent (VSTGUI::MouseDownEvent& e) override;
    void onMouseMoveEvent (VSTGUI::MouseMoveEvent& e) override;
    void onMouseUpEvent (VSTGUI::MouseUpEvent& e) override;

private:
    VSTGUI::CRect toggleRect (int i) const;
    ParamHost* host;
    bool showEnv = false;
    bool dragging = false;
    VSTGUI::CPoint last;
    double freqN = 0, resN = 0;
};

// Breakpoint envelope display. which: 0 amp, 1 filter, 2 pitch.
//   drag a point            move it (time / level)
//   double-click a segment  add a breakpoint there (between the peak and the sustain point)
//   double-click a square   remove that breakpoint
//   Shift + drag a segment  bend its curve
class EnvelopeDisplay : public VSTGUI::CView
{
public:
    enum Kind { kStart, kAttack, kBreak, kDecay, kHold, kRelease };
    struct Pt
    {
        VSTGUI::CPoint p;
        double level;
        int kind;
        int index;        // breakpoint index for kBreak
        uint32_t curveId; // curve param of the segment ending here
    };
    struct Geometry
    {
        Pt pts[kMaxEnvPoints + 5];
        int count = 0;
        int points = 0;
        VSTGUI::CRect area;
        double curveOf (ParamHost* host, int seg) const;
    };

    EnvelopeDisplay (const VSTGUI::CRect& r, ParamHost* h, int which);
    void setWhich (int w)
    {
        which = w;
        invalid ();
    }
    void draw (VSTGUI::CDrawContext* ctx) override;
    void onMouseDownEvent (VSTGUI::MouseDownEvent& e) override;
    void onMouseMoveEvent (VSTGUI::MouseMoveEvent& e) override;
    void onMouseUpEvent (VSTGUI::MouseUpEvent& e) override;
    void tick (); // called from the editor's timer (clears status messages)

    static Geometry geometry (ParamHost* host, int which, const VSTGUI::CRect& area);
    // Shared drawing routine (also used by the filter display's envelope view).
    static void drawAdsr (VSTGUI::CDrawContext* ctx, const VSTGUI::CRect& area, ParamHost* host, int which,
                          VSTGUI::CPoint* handles, bool dim);
    VSTGUI::CRect plotArea () const;

private:
    enum DragMode { DragHandle, DragCurve };
    uint32_t idA () const;
    int segmentAt (const Geometry& g, double x) const;
    void insertPoint (const Geometry& g, int seg, double x, double y);
    void removePoint (const Geometry& g, int index);
    void flash (const std::string& msg);

    ParamHost* host;
    int which;
    DragMode dragMode = DragHandle;
    bool curveRising = false;
    std::vector<uint32_t> dragIds;
    std::vector<double> dragValues;
    VSTGUI::CPoint last;
    std::string status;
    int statusTicks = 0;
};

} // namespace simplr
