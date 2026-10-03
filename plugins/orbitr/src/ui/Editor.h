#pragma once

#include "smacheratr/src/ui/TailDisplays.h"

#include <memory>

#include "pluginkit/vst/EditorBase.h"

namespace orbitr {

class Controller;
class OrbView;

class Editor : public pk::EditorBase
{
public:
    static constexpr double kWidth = 760.0;
    static constexpr double kHeight = 504.0 + 170.0; // the tail panel's displays (smacheratr::TailDisplays::kHeight)
    // the MOTION panel and its Pattern switch (in the panel), for the host test
    static constexpr double kMotionLeft = 8.0, kMotionTop = 298.0;
    static constexpr double kPatternLeft = 14.0, kPatternTop = 30.0, kPatternW = 110.0, kPatternH = 20.0;

    explicit Editor (Controller* c);
    void buildUI (VSTGUI::CFrame* f) override;
    void idle () override;
    void paramChanged (uint32_t id) override;

private:
    std::unique_ptr<smacheratr::TailDisplays> tailDisplays;
    void onClose () override;
    void showMenu (VSTGUI::CPoint where);

    Controller* ctl;
    OrbView* display = nullptr;
    pk::Label* latencyLabel = nullptr;
};

} // namespace orbitr
