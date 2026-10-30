#pragma once

#include "Params.h"

#include <memory>

#include "pluginkit/vst/EditorBase.h"

namespace sunder {

class Controller;
class SunderView;

class Editor : public pk::EditorBase
{
public:
    // Classic: the display across the top (the output stage's response at the left, the compression curve at the
    // right); under it COMPRESSOR, GERMANIUM and OUTPUT side by side, and under them BASS (the bass enhancer, ahead
    // of the compressor or after it) across the window. An arranged layout (Wide) builds the display narrower and as tall as
    // the panels' row, beside them; BASS is the second row.
    static constexpr double kWidth = 760.0;
    static constexpr double kTop = 40.0, kDisplayBottom = 240.0;
    static constexpr double kArrangedDisplayW = 420.0;
    static constexpr double kRowTop = 248.0, kRowBottom = 464.0;
    static constexpr double kBassTop = kRowBottom + 8.0, kBassBottom = kBassTop + 176.0;
    static constexpr double kHeight = kBassBottom + 8.0;
    // BASS: Bass and the effect's knobs in the first row (then x2 and Oversample), the gate's and Low Cut in the
    // second (then Bass Position)
    static constexpr double kBassLeft = 8.0, kBassRight = 752.0;
    static constexpr double kBassKnobW = 76.0, kBassPitch = 82.0;
    // the panels' places (left, right)
    static constexpr double kCompLeft = 8.0, kCompRight = 452.0;
    static constexpr double kGermLeft = 460.0, kGermRight = 604.0;
    static constexpr double kOutLeft = 612.0, kOutRight = 752.0;
    // inside a panel: the choices (a labelled dropdown) and switches at the top, then the knobs; OUTPUT's Soft
    // Clip and Hard Clip under its knobs
    static constexpr double kKnobW = 56.0, kKnobH = 64.0, kCompKnobW = 76.0;
    static constexpr double kChoiceTop = 24.0, kChoiceH = 34.0;
    static constexpr double kSwitchTop = kChoiceTop + 15.0, kSwitchH = kChoiceH - 15.0;
    static constexpr double kKnobTop = 66.0, kKnobRow2 = 140.0;
    static constexpr double kCompX[4] = {12.0, 120.0, 228.0, 336.0};
    static constexpr double kGermX[2] = {12.0, 76.0};
    static constexpr double kOutX[2] = {12.0, 72.0};
    static constexpr double kClipTop = 146.0, kClipW = 116.0; // Soft Clip and Hard Clip, one above the other

    explicit Editor (Controller* c);
    void buildUI (VSTGUI::CFrame* f) override;
    pk::layout::Spec layoutSpec (bool arranged) const override;
    // The Basic page (pluginkit/ui/BasicView.h): the display; Input, Ratio, Release, Drive, Feedback and Bass;
    // Mix, Output, Soft Clip and Hard Clip
    pk::basic::Spec basicSpec () override;
    void idle () override;
    void paramChanged (uint32_t id) override;

private:
    void onClose () override;
    void showMenu (VSTGUI::CPoint where);
    SunderView* makeDisplay (const VSTGUI::CRect& r);
    VSTGUI::CRect displayRect (bool arranged) const;

    Controller* ctl;
    SunderView* display = nullptr;
    pk::Label* latencyLabel = nullptr;
};

} // namespace sunder
