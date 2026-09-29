// The colour EQ curve (applied before the shaper and undone after it) with two handles, and, while
// Clarity is on, Clarity's band drawn the way a multiband compressor shows a band: its range shaded,
// the most it can cut outlined and the cut it is making right now filled in, moving with the audio.
//   left handle, up / down        Amt Lo
//   right handle, up / down       Amt Hi
//   right handle, sideways        Freq
//   Clarity handle, sideways      Clarity Frequency
//   Clarity band edge, sideways   Clarity Width
//   wheel on a handle (held, or with Shift)   the colour peak's width / Clarity's width
//   double-click or right-click a handle      reset its parameters
// Used by Smacheratr and, through a pk::MappedParamHost, by the Smacheratr in Smempler's rack; the
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
    void idle (); // follows Clarity's cut

    double xOfHz (double hz) const;
    double yOfDb (double db) const;
    VSTGUI::CPoint loHandle () const;
    VSTGUI::CPoint hiHandle () const;
    VSTGUI::CPoint clarityHandle () const; // on the 0 dB line at Clarity's centre (when Clarity is on)
    double clarityEdgeX (bool high) const; // Clarity's band edges

private:
    enum class Drag { None, Lo, Hi, Clarity, ClarityLow, ClarityHigh };
    Drag hit (const VSTGUI::CPoint& p) const;
    double sampleRate () const;
    bool clarityOn () const;

    pk::ParamHost* host;
    RateSource rate;
    MeterSource meters;
    Drag drag = Drag::None;
    VSTGUI::CPoint down;
    double startLo = 0.0, startHi = 0.0, startFreq = 0.0, startClarity = 0.0;
    bool movedH = false, movedV = false;
    float shownCut = 0.0f; // Clarity's cut (dB, 0 or less), eased
};

} // namespace smacheratr
