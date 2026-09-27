#pragma once

#include "Widgets.h"

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

// ADSR display with draggable handles. which: 0 amp, 1 filter, 2 pitch.
class EnvelopeDisplay : public VSTGUI::CView
{
public:
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

    // Shared drawing routine (also used by the filter display's envelope view).
    static void drawAdsr (VSTGUI::CDrawContext* ctx, const VSTGUI::CRect& area, ParamHost* host, int which,
                          VSTGUI::CPoint* handles, bool dim);

private:
    uint32_t idA () const;
    ParamHost* host;
    int which;
    int dragHandle = -1;
    VSTGUI::CPoint last;
    double va = 0, vd = 0, vs = 0, vr = 0;
};

} // namespace simplr
