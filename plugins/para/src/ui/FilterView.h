// The two filters and their sum, in the style of a morphing EQ display: the high-pass in orange,
// the low-pass in blue, the sum in white, with a handle at each cutoff. Behind them, live spectra of
// the input (grey) and the output (light), so you can see the notch working; the handles move with
// the tracked note and the envelope, and the envelope level shows at the top right.
//   drag a handle sideways   cutoff
//   drag a handle up / down  that filter's gain (to the bottom: -inf)
//   Alt + drag up / down      resonance
//   double-click a handle    reset it
// Used by Para and, through a pk::MappedParamHost, inside Smempler; the levels come from a
// function so it does not depend on a controller.
#pragma once

#include "../core/Engine.h"
#include "../core/Params.h"

#include "pluginkit/ui/Widgets.h"

#include "vstgui/lib/cview.h"

#include <cstdint>
#include <functional>
#include <vector>

namespace para {

class FilterView : public VSTGUI::CView
{
public:
    static constexpr double kMinHz = 20.0, kMaxHz = 20000.0, kMinDb = -36.0, kMaxDb = 18.0;
    static constexpr double kSpecFloorDb = -96.0;
    static constexpr int kFftSize = 2048;
    using MeterSource = std::function<Meters* ()>;

    FilterView (const VSTGUI::CRect& r, pk::ParamHost* host, MeterSource meters);
    void draw (VSTGUI::CDrawContext* ctx) override;
    void onMouseDownEvent (VSTGUI::MouseDownEvent& e) override;
    void onMouseMoveEvent (VSTGUI::MouseMoveEvent& e) override;
    void onMouseUpEvent (VSTGUI::MouseUpEvent& e) override;
    void onMouseExitEvent (VSTGUI::MouseExitEvent& e) override;
    void idle ();

    double xOfHz (double hz) const;
    double hzOfX (double x) const;
    double yOfDb (double db) const;
    // effective cutoffs (tracking and envelope included) and the handles
    void cutoffs (double& hp, double& lp) const;
    VSTGUI::CPoint hpHandle () const;
    VSTGUI::CPoint lpHandle () const;

private:
    enum class Drag { None, Hp, Lp };
    Drag hit (const VSTGUI::CPoint& p) const;
    uint32_t resId (bool hp) const;
    void analyse (const std::vector<float>& x, std::vector<float>& spec);
    double specAt (const std::vector<float>& spec, double f0, double f1) const;

    pk::ParamHost* host;
    MeterSource meters;
    Drag drag = Drag::None;
    VSTGUI::CPoint down;
    double startFreq = 0.0, startRes = 0.0, startGain = 0.0;
    float shownOffset = 0.0f, shownEnv = 0.0f, shownHpHz = 0.0f, shownLpHz = 0.0f, shownHpMul = 1.0f, shownLpMul = 1.0f;
    bool live () const; // Vocal movement with audio running: positions and fades come from the engine
    int shownNote = -1;
    uint32_t lastWritten = 0;
    bool haveSpectrum = false;
    double rate = 48000.0;
    std::vector<float> bufIn, bufOut, specIn, specOut, window;
};

} // namespace para
