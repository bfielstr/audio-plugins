#pragma once

#include "Params.h"

#include "smacheratr/src/ui/TailPanel.h"

#include <memory>

#include "pluginkit/vst/EditorBase.h"

namespace pinchr {

class Controller;
class PinchView;

class Editor : public pk::EditorBase
{
public:
    // Classic: the display across the top (the output stage's response at the left, the compression curve at the
    // right); under it COMPRESSOR, GERMANIUM and OUTPUT side by side; the end saturator's section at the bottom.
    // An arranged layout (Wide) builds the display narrower and as tall as the panels' row, beside them.
    static constexpr double kWidth = 760.0;
    static constexpr double kTop = 40.0, kDisplayBottom = 240.0;
    static constexpr double kArrangedDisplayW = 420.0;
    static constexpr double kRowTop = 248.0, kRowBottom = 464.0;
    static constexpr double kTailTop = kRowBottom + 8.0;
    static constexpr double kHeight = kTailTop + smacheratr::TailPanel::kOpenHeight + 8.0;
    // the panels' places (left, right)
    static constexpr double kCompLeft = 8.0, kCompRight = 452.0;
    static constexpr double kGermLeft = 460.0, kGermRight = 604.0;
    static constexpr double kOutLeft = 612.0, kOutRight = 752.0;
    // inside a panel: the choices (a labelled dropdown) and switches at the top, then the knobs
    static constexpr double kKnobW = 56.0, kKnobH = 64.0, kCompKnobW = 76.0;
    static constexpr double kChoiceTop = 24.0, kChoiceH = 34.0;
    static constexpr double kSwitchTop = kChoiceTop + 15.0, kSwitchH = kChoiceH - 15.0;
    static constexpr double kKnobTop = 66.0, kKnobRow2 = 140.0;
    static constexpr double kCompX[4] = {12.0, 120.0, 228.0, 336.0};
    static constexpr double kGermX[2] = {12.0, 76.0};
    static constexpr double kOutX[2] = {12.0, 72.0};

    explicit Editor (Controller* c);
    void buildUI (VSTGUI::CFrame* f) override;
    pk::layout::Spec layoutSpec (bool arranged) const override;
    // The Basic page (pluginkit/ui/BasicView.h): the display; Input, Ratio, Attack, Release, Drive and
    // Feedback; Mix and Output; the end saturator in the extras
    pk::basic::Spec basicSpec () override;
    void idle () override;
    void paramChanged (uint32_t id) override;

private:
    std::unique_ptr<smacheratr::TailPanel> tail;
    std::unique_ptr<smacheratr::TailPanel> makeTail ();
    static smacheratr::TailBases tailBases ();
    void onClose () override;
    void showMenu (VSTGUI::CPoint where);
    PinchView* makeDisplay (const VSTGUI::CRect& r);
    VSTGUI::CRect displayRect (bool arranged) const;

    Controller* ctl;
    PinchView* display = nullptr;
    pk::Label* latencyLabel = nullptr;
};

} // namespace pinchr
