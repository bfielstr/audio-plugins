#pragma once

#include "smacheratr/src/ui/TailPanel.h"

#include <memory>

#include "pluginkit/vst/EditorBase.h"

namespace smeezr {

class Controller;
class PinkView;

class Editor : public pk::EditorBase
{
public:
    // Classic: the display across the top; under it the big Squeeze knob in the middle between MIX (with
    // Speed) and OUTPUT; the end saturator's section at the bottom. An arranged layout (Wide) builds the
    // display narrower and as tall as the knobs' row, beside them.
    static constexpr double kWidth = 760.0;
    static constexpr double kTop = 40.0, kDisplayBottom = 220.0;
    static constexpr double kRowTop = 228.0, kRowBottom = 488.0;
    static constexpr double kTailTop = kRowBottom + 8.0;
    static constexpr double kHeight = kTailTop + smacheratr::TailPanel::kOpenHeight + 8.0;
    static constexpr double kArrangedDisplayW = 420.0;
    // the panels' places (left, right) in the Classic layout, for the host test
    static constexpr double kMixLeft = 146.0, kMixRight = 262.0;
    static constexpr double kSqueezeLeft = 270.0, kSqueezeRight = 490.0;
    static constexpr double kOutLeft = 498.0, kOutRight = 614.0;
    // the big knob inside SQUEEZE (panel coordinates) and the stage readout under it
    static constexpr double kBigLeft = 20.0, kBigTop = 22.0, kBigW = 180.0, kBigH = 200.0;
    static constexpr double kStageTop = 228.0;
    // the small knobs (56 x 64) in MIX and OUTPUT, and the Speed switch under Mix (panel coordinates)
    static constexpr double kKnobLeft = 30.0, kKnobTop = 30.0, kKnobW = 56.0, kKnobH = 64.0;
    static constexpr double kSpeedLeft = 12.0, kSpeedTop = 200.0, kSpeedW = 92.0, kSpeedH = 20.0;

    explicit Editor (Controller* c);
    void buildUI (VSTGUI::CFrame* f) override;
    // the panels for Menu > Layout (pluginkit/Layout.h): the Wide template's rows by purpose
    pk::layout::Spec layoutSpec (bool arranged) const override;
    // The Basic page (pluginkit/ui/BasicView.h): the pink balance, Squeeze, Speed and Mix; Output; the end saturator in the extras
    pk::basic::Spec basicSpec () override;
    void idle () override;
    void paramChanged (uint32_t id) override;

private:
    std::unique_ptr<smacheratr::TailPanel> tail;
    std::unique_ptr<smacheratr::TailPanel> makeTail ();
    static smacheratr::TailBases tailBases ();
    void onClose () override;
    void showMenu (VSTGUI::CPoint where);
    void updateStage ();
    VSTGUI::CRect displayRect (bool arranged) const;

    Controller* ctl;
    PinkView* display = nullptr;
    pk::Label* latencyLabel = nullptr;
    pk::Label* stageLabel = nullptr;
};

} // namespace smeezr
