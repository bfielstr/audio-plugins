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
    // the end saturator's section (smacheratr::TailPanel, both parts open) at the bottom, from y 604, 8 px clear under it
    static constexpr double kHeight = 604.0 + smacheratr::TailPanel::kOpenHeight + 8.0;
    // the stage display, for the tests
    static constexpr double kStageLeft = 8.0, kStageTop = 40.0, kStageRight = 560.0, kStageBottom = 300.0;

    explicit Editor (Controller* c);
    void buildUI (VSTGUI::CFrame* f) override;
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
