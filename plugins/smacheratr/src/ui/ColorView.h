// The colour EQ curve (applied before the shaper and undone after it) with two handles, and, while
// Gently (called Clarity before) is on, its bands drawn the way a multiband compressor shows a band:
// its range shaded, the most it can cut outlined and the cut it is making right now filled in, moving
// with the audio. (Gently's code keeps the old name: Clarity.)
//   left handle, up / down        Amt Lo
//   right handle, up / down       Amt Hi
//   right handle, sideways        Freq
//   Gently handle, sideways       a Gently band's frequency (band 2 is drawn blue)
//   Gently handle, up / down      its Range: the handle sits at the most it cuts, drag down for more
//   Gently band edge, sideways    its width
//   Alt (Option) + drag on a Gently band, sideways   its width, the band staying centred (right: wider)
//   wheel on a handle (held, or with Shift)   the colour peak's width / Gently's width
//   double-click or right-click a handle      reset its parameters
// Used by Smacheratr and, through a pk::MappedParamHost, by the Smacheratr in Smemplr's rack; the
// sample rate and the levels come from functions so it does not depend on a controller.
#pragma once

#include "../core/Engine.h"
#include "../core/Params.h"

#include "pluginkit/ui/Widgets.h"

#include "vstgui/lib/cview.h"

#include <functional>

namespace smacheratr {

class ColorView : public VSTGUI::CView
{
public:
    static constexpr double kMinHz = 20.0, kMaxHz = 20000.0, kMaxDb = 24.0;
    using RateSource = std::function<double ()>;
    using MeterSource = std::function<const Meters* ()>;

    ColorView (const VSTGUI::CRect& r, pk::ParamHost* host, RateSource rate, MeterSource meters);
    void draw (VSTGUI::CDrawContext* ctx) override;
    void onMouseDownEvent (VSTGUI::MouseDownEvent& e) override;
    void onMouseMoveEvent (VSTGUI::MouseMoveEvent& e) override;
    void onMouseUpEvent (VSTGUI::MouseUpEvent& e) override;
    void onMouseExitEvent (VSTGUI::MouseExitEvent& e) override;
    void onMouseWheelEvent (VSTGUI::MouseWheelEvent& e) override;
    void idle (); // follows Gently's cut
    std::function<void (int)> onBandPicked; // a Gently band's handle or edge was grabbed
    // Gently band k's colour: green, and blue for the second (also its Threshold slider's)
    static VSTGUI::CColor bandColor (int band, uint8_t alpha = 255);

    double xOfHz (double hz) const;
    double yOfDb (double db) const;
    VSTGUI::CPoint loHandle () const;
    VSTGUI::CPoint hiHandle () const;
    VSTGUI::CPoint clarityHandle (int band) const; // at a Clarity band's centre, as deep as its Range
    double clarityEdgeX (int band, bool high) const; // its edges

private:
    enum class Drag { None, Lo, Hi, Clarity, ClarityLow, ClarityHigh, ClarityWidth };
    Drag hit (const VSTGUI::CPoint& p, int* band = nullptr) const; // band: which Clarity band was hit
    int bandUnder (const VSTGUI::CPoint& p) const;                 // the working band whose region p is in (-1: none)
    double sampleRate () const;
    bool clarityOn (int band) const;    // the band works (Clarity on, Range above 0)
    bool clarityShown (int band) const; // its handle is there to grab (Clarity on)

    pk::ParamHost* host;
    RateSource rate;
    MeterSource meters;
    Drag drag = Drag::None;
    VSTGUI::CPoint down;
    int dragBand = 0; // the Clarity band being dragged
    double startLo = 0.0, startHi = 0.0, startFreq = 0.0, startClarity = 0.0, startRange = 0.0, startWidth = 0.0;
    bool movedH = false, movedV = false;
    float shownCut[kClarityBands] = {0.0f, 0.0f}; // the Clarity bands' cuts (dB, 0 or less), eased
};

} // namespace smacheratr
