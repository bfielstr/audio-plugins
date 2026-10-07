#pragma once

#include "smacheratr/src/ui/TailPanel.h"

#include <memory>

#include "pluginkit/vst/EditorBase.h"

namespace widr {

class Controller;
class StageView;
class GonioView;

class Editor : public pk::EditorBase
{
public:
    static constexpr double kWidth = 760.0;
    // the cinema stage's row (LANES, CINEMA) from y 556, the levels from 704, the end saturator's section
    // (smacheratr::TailPanel, both parts open) at the bottom, from y 752, 8 px clear under it
    static constexpr double kLanesTop = 556.0, kLevelsTop = 704.0, kTailTop = 752.0;
    static constexpr double kHeight = kTailTop + smacheratr::TailPanel::kOpenHeight + 8.0;
    // the LANES panel's columns (one per lane, kLaneColumn wide from kLanesLeft), the CINEMA panel, for the tests
    static constexpr double kLanesLeft = 8.0, kLaneColumn = 100.0, kCinemaLeft = 520.0;
    // the stage display, for the tests
    static constexpr double kStageLeft = 8.0, kStageTop = 40.0, kStageRight = 560.0, kStageBottom = 300.0;

    explicit Editor (Controller* c);
    void buildUI (VSTGUI::CFrame* f) override;
    // the panels for Menu > Layout (pluginkit/Layout.h): the Wide template's rows by purpose
    pk::layout::Spec layoutSpec (bool arranged) const override;
    void idle () override;
    void paramChanged (uint32_t id) override;
    StageView* stageView () const { return stage; }

private:
    std::unique_ptr<smacheratr::TailPanel> tail;
    void onClose () override;
    void showMenu (VSTGUI::CPoint where);

    Controller* ctl;
    StageView* stage = nullptr;
    GonioView* gonio = nullptr;
    pk::Label* statusLabel = nullptr;
};

} // namespace widr
