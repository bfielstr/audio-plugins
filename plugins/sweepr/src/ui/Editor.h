#pragma once

#include "Params.h"

#include <memory>

#include "pluginkit/vst/EditorBase.h"

namespace sweepr {

class Controller;
class CurveView;

class Editor : public pk::EditorBase
{
public:
    // Classic: the curve across the top; under it BAND A, BAND B (each its bell and its LFO), TAPE and
    // OUTPUT side by side. An arranged layout (Wide) builds the curve narrower and as tall as the panels'
    // row, beside them.
    static constexpr double kWidth = 900.0;
    static constexpr double kTop = 40.0, kDisplayBottom = 260.0;
    static constexpr double kArrangedDisplayW = 440.0;
    static constexpr double kRowTop = 268.0, kRowBottom = 524.0;
    static constexpr double kHeight = kRowBottom + 8.0;
    // the panels' places (left, right)
    static constexpr double kBandLeft[kBands] = {8.0, 296.0};
    static constexpr double kBandW = 280.0;
    static constexpr double kTapeLeft = 584.0, kTapeRight = 780.0;
    static constexpr double kOutLeft = 788.0, kOutRight = 892.0;
    // inside a band's panel: the bell's knobs in a row, then the LFO: Sync, the rate (Division or Rate),
    // Shape and (B) Link in a column at the left, Depth and Phase knobs beside them
    static constexpr double kKnobW = 56.0, kKnobH = 64.0;
    static constexpr double kKnobTop = 28.0, kKnobX[3] = {24.0, 112.0, 200.0};
    static constexpr double kLfoTop = 118.0;
    static constexpr double kColLeft = 12.0, kColW = 110.0, kRowH = 20.0, kRowStep = 26.0;
    // inside TAPE: Tape at the top, Drive and Speed, Compression under them (knobs a little wider: its label)
    static constexpr double kTapeKnobW = 76.0;
    static constexpr double kTapeX[2] = {16.0, 104.0};
    static constexpr double kTapeRow[2] = {58.0, 142.0};
    // inside OUTPUT: Mix over Output, then Soft Clip and Hard Clip, one above the other
    static constexpr double kClipTop = 190.0, kClipH = 19.0, kClipGap = 6.0, kClipX = 8.0;

    explicit Editor (Controller* c);
    void buildUI (VSTGUI::CFrame* f) override;
    pk::layout::Spec layoutSpec (bool arranged) const override;
    // The Basic page (pluginkit/ui/BasicView.h): the curve; both bands' Gain and LFO Depth, Drive and Mix;
    // Output, Soft Clip and Hard Clip
    pk::basic::Spec basicSpec () override;
    void idle () override;
    void paramChanged (uint32_t id) override;

private:
    void onClose () override;
    void showMenu (VSTGUI::CPoint where);
    void updateRates ();
    CurveView* makeCurve (const VSTGUI::CRect& r);
    VSTGUI::CRect displayRect (bool arranged) const;

    Controller* ctl;
    CurveView* display = nullptr;
    pk::Label* latencyLabel = nullptr;
    VSTGUI::CView* divViews[kBands] {};
    VSTGUI::CView* rateViews[kBands] {};
    pk::ParamView* bLfoViews[3] {}; // B's Sync, Division and Rate, and Shape: dimmed while Link is on
    pk::ParamView* bShape = nullptr;
};

} // namespace sweepr
