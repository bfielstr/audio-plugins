#pragma once

#include "smacheratr/src/ui/TailDisplays.h"

#include "../core/Params.h"

#include "pluginkit/vst/EditorBase.h"

#include <memory>
#include <vector>

namespace levlr {

class Controller;
class LevelView;

// The band display on top, a row of controls (each band's level, mute and solo; the crossovers, the
// slope and the output), then the end Smacheratr.
class Editor : public pk::EditorBase
{
public:
    static constexpr double kWidth = 900.0;
    static constexpr double kHeight = 660.0;
    // layout (also used by the host test)
    static constexpr double kViewLeft = 8.0, kViewTop = 40.0, kViewRight = 892.0, kViewBottom = 292.0;
    static constexpr double kRowTop = 300.0, kTailTop = 404.0;
    static constexpr double kColumnW = 150.0; // each band's controls

    explicit Editor (Controller* c);
    void buildUI (VSTGUI::CFrame* f) override;
    void idle () override;
    void paramChanged (uint32_t id) override;

private:
    void onClose () override;
    void showMenu (VSTGUI::CPoint where);

    Controller* ctl;
    std::unique_ptr<smacheratr::TailDisplays> tailDisplays;
    LevelView* levels = nullptr;
    std::vector<VSTGUI::CView*> headers; // each band's name and range
};

} // namespace levlr
