#pragma once

#include "smacheratr/src/ui/TailPanel.h"

#include <memory>

#include "pluginkit/vst/EditorBase.h"

namespace moistr {

class Controller;
class BandView;

class Editor : public pk::EditorBase
{
public:
    // Classic: the display across the top, two rows of panels (SPLIT, LOW, MID, HIGH over MOVEMENT,
    // BAND MOVE, GLUE), OUTPUT beside both, the end saturator's section at the bottom
    static constexpr double kWidth = 1044.0;
    static constexpr double kRow1 = 238.0, kRow2 = 362.0, kRowH = 116.0;
    static constexpr double kTailTop = kRow2 + kRowH + 8.0;
    static constexpr double kHeight = kTailTop + smacheratr::TailPanel::kOpenHeight + 8.0;
    // the panels' places (left, right) in the Classic layout, for the host test
    static constexpr double kSplitLeft = 8.0, kSplitRight = 276.0;
    static constexpr double kLowLeft = 284.0, kLowRight = 496.0;
    static constexpr double kMidLeft = 504.0, kMidRight = 716.0;
    static constexpr double kHighLeft = 724.0, kHighRight = 936.0;
    static constexpr double kMoveLeft = 8.0, kMoveRight = 342.0;
    static constexpr double kDepthLeft = 350.0, kDepthRight = 626.0;
    static constexpr double kGlueLeft = 634.0, kGlueRight = 936.0;
    static constexpr double kOutLeft = 944.0, kOutRight = 1036.0;
    // a switch at the top left of its panel (Slope in SPLIT, Sync in MOVEMENT, Passes in GLUE; panel coordinates)
    static constexpr double kSwitchLeft = 14.0, kSwitchTop = 30.0, kSwitchW = 110.0, kSwitchH = 20.0;
    // the knobs: 56 x 64 from y 28, one every 64 px (from x 14, or x 136 beside a switch)
    static constexpr double kKnobLeft = 14.0, kKnobBeside = 136.0, kKnobStep = 64.0, kKnobTop = 28.0, kKnobW = 56.0, kKnobH = 64.0;
    // the display (Classic; an arranged layout builds it narrower: kArrangedDisplayW)
    static constexpr double kDisplayTop = 40.0, kDisplayBottom = 230.0, kArrangedDisplayW = 400.0;

    explicit Editor (Controller* c);
    void buildUI (VSTGUI::CFrame* f) override;
    // the panels for Menu > Layout (pluginkit/Layout.h): the Wide template's rows by purpose
    pk::layout::Spec layoutSpec (bool arranged) const override;
    void idle () override;
    void paramChanged (uint32_t id) override;

private:
    std::unique_ptr<smacheratr::TailPanel> tail;
    void onClose () override;
    void showMenu (VSTGUI::CPoint where);
    VSTGUI::CRect displayRect (bool arranged) const;

    Controller* ctl;
    BandView* display = nullptr;
    pk::Label* latencyLabel = nullptr;
};

} // namespace moistr
