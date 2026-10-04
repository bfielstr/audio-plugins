#pragma once

#include "smacheratr/src/ui/TailPanel.h"

#include <memory>

#include "pluginkit/vst/EditorBase.h"

namespace orbitr {

class Controller;
class OrbView;

class Editor : public pk::EditorBase
{
public:
    static constexpr double kWidth = 760.0;
    // the end saturator's section (smacheratr::TailPanel, both parts open) at the bottom, from y 546, 8 px clear under it
    static constexpr double kTailTop = 546.0;
    static constexpr double kHeight = kTailTop + smacheratr::TailPanel::kOpenHeight + 8.0;
    // the MOTION panel and its Pattern switch (in the panel), for the host test
    static constexpr double kMotionLeft = 8.0, kMotionTop = 298.0;
    static constexpr double kPatternLeft = 14.0, kPatternTop = 30.0, kPatternW = 110.0, kPatternH = 20.0;
    // the GRAINS panel under it, its Grains switch where the Pattern switch is in MOTION
    static constexpr double kGrainsLeft = 8.0, kGrainsTop = 422.0;

    explicit Editor (Controller* c);
    void buildUI (VSTGUI::CFrame* f) override;
    void idle () override;
    void paramChanged (uint32_t id) override;

private:
    std::unique_ptr<smacheratr::TailPanel> tail;
    void onClose () override;
    void showMenu (VSTGUI::CPoint where);

    Controller* ctl;
    OrbView* display = nullptr;
    pk::Label* latencyLabel = nullptr;
};

} // namespace orbitr
