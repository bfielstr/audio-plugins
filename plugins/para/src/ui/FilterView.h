// The two filters and their sum, in the style of a morphing EQ display: the high-pass in orange,
// the low-pass in blue, the sum in white, with a handle at each cutoff. Behind them, live spectra of
// the input (grey) and the output (light), so you can see the notch working; the handles move with
// the tracked note and the envelope, and the envelope level shows at the top right.
// A handle sits at its filter's cutoff, as high as the resonant peak (plus the filter's gain).
//   drag a handle sideways   cutoff
//   drag a handle up / down  resonance, and the gain too with Drag Gain on
//   Alt + drag up / down      gain only (to the bottom: -inf)
//   mouse wheel               resonance, while holding a handle or with Shift over it
//   double-click a handle    reset it
// With audio running the display adds what the engine does (tracking, envelope, glide) to the
// current settings; without, it shows the settings with the last tracked note. Vocal movement is
// applied here, from the filter moved last, so it follows every edit at once: the pushed filter's
// handle sits at the leader's cutoff and sinks and dims as it fades.
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
    void onMouseWheelEvent (VSTGUI::MouseWheelEvent& e) override;
    void idle ();

    double xOfHz (double hz) const;
    double hzOfX (double x) const;
    double yOfDb (double db) const;
    // effective cutoffs (tracking and envelope included), the Vocal fades, and the handles
    void cutoffs (double& hp, double& lp) const;
    void effective (double& hp, double& lp, float& hpMul, float& lpMul) const;
    double handleDb (bool hp) const;
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
    float shownOffset = 0.0f, shownEnv = 0.0f, shownHpShift = 0.0f, shownLpShift = 0.0f, shownHpMul = 1.0f,
          shownLpMul = 1.0f;
    bool shownLeaderLp = true;
    // Vocal: which filter leads (the one whose frequency moved last), tracked from the settings
    void trackLeader ();
    bool leaderLp = true, leaderKnown = false;
    double seenHp = -1.0, seenLp = -1.0;
    bool live () const; // audio running: the engine's movement is added to the settings
    uint32_t lastBlocks = 0;
    int idleSinceBlock = 1 << 20; // idle calls since the block count last moved
    int shownNote = -1;
    uint32_t lastWritten = 0;
    bool haveSpectrum = false;
    double rate = 48000.0;
    std::vector<float> bufIn, bufOut, specIn, specOut, window;
};

} // namespace para
