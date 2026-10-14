#pragma once

#include "Params.h"

#include "smacheratr/src/ui/TailPanel.h"

#include <memory>

#include "pluginkit/vst/EditorBase.h"

namespace tapr {

class Controller;
class TapeView;

class Editor : public pk::EditorBase
{
public:
    // Classic: the display across the top (the response at the left, the curve at the right); under it TAPE,
    // CUT and OUTPUT side by side; the end saturator's section at the bottom. An arranged layout (Wide)
    // builds the display narrower and as tall as the panels' row, beside them.
    static constexpr double kWidth = 760.0;
    static constexpr double kTop = 40.0, kDisplayBottom = 240.0;
    static constexpr double kArrangedDisplayW = 420.0;
    static constexpr double kRowTop = 248.0, kRowBottom = 464.0;
    static constexpr double kTailTop = kRowBottom + 8.0;
    static constexpr double kHeight = kTailTop + smacheratr::TailPanel::kOpenHeight + 8.0;
    // the panels' places (left, right)
    static constexpr double kTapeLeft = 8.0, kTapeRight = 316.0;
    static constexpr double kCutLeft = 324.0, kCutRight = 588.0;
    static constexpr double kOutLeft = 596.0, kOutRight = 752.0;
    // inside a panel: its switch and its Place (a labelled dropdown, its field level with the switch) at the
    // top, then the knobs: TAPE's three in a row; CUT's Freq knobs over their gains; OUTPUT's two side by side
    static constexpr double kKnobW = 56.0, kKnobH = 64.0, kTapeKnobW = 76.0;
    static constexpr double kChoiceTop = 24.0, kChoiceH = 34.0;
    static constexpr double kSwitchTop = kChoiceTop + 15.0, kSwitchH = kChoiceH - 15.0;
    static constexpr double kKnobTop = 66.0, kKnobRow2 = 140.0;
    static constexpr double kTapeX[3] = {12.0, 116.0, 220.0};
    static constexpr double kCutX[3] = {20.0, 104.0, 188.0};
    static constexpr double kOutX[2] = {16.0, 84.0};

    explicit Editor (Controller* c);
    void buildUI (VSTGUI::CFrame* f) override;
    pk::layout::Spec layoutSpec (bool arranged) const override;
    // The Basic page (pluginkit/ui/BasicView.h): the display; Drive, Speed, Compression, Lo, Mid and Hi;
    // Output; Mix and the end saturator in the extras
    pk::basic::Spec basicSpec () override;
    void idle () override;
    void paramChanged (uint32_t id) override;

private:
    std::unique_ptr<smacheratr::TailPanel> tail;
    std::unique_ptr<smacheratr::TailPanel> makeTail ();
    static smacheratr::TailBases tailBases ();
    void onClose () override;
    void showMenu (VSTGUI::CPoint where);
    TapeView* makeDisplay (const VSTGUI::CRect& r);
    VSTGUI::CRect displayRect (bool arranged) const;

    Controller* ctl;
    TapeView* display = nullptr;
    pk::Label* latencyLabel = nullptr;
};

} // namespace tapr
