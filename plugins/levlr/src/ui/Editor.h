#pragma once

#include "smacheratr/src/ui/TailDisplays.h"

#include "../core/Params.h"

#include "pluginkit/vst/EditorBase.h"

#include <memory>
#include <vector>

namespace levlr {

class Controller;
class LevelView;

// The band display on top, a row of controls (each band's level, mute, solo, drive and its type; the
// crossovers, the slope, Bands and the output), then the end Smacheratr. The columns of the bands past
// Bands are hidden (as they are in the display).
class Editor : public pk::EditorBase
{
public:
    static constexpr double kWidth = 900.0;
    static constexpr double kHeight = 728.0;
    // layout (also used by the host test)
    static constexpr double kViewLeft = 8.0, kViewTop = 40.0, kViewRight = 892.0, kViewBottom = 292.0;
    static constexpr double kRowTop = 300.0, kTailTop = 472.0;
    static constexpr double kColumnW = 150.0; // each band's controls
    static constexpr double kDriveTop = kRowTop + 92.0; // each band's Drive knob and Type
    static constexpr double kBandsTop = kRowTop + 92.0;  // Bands (1 .. 4), right of the band columns
    static constexpr double kBandsLeft = kViewLeft + kBands * kColumnW + 46.0, kBandsRight = kBandsLeft + 152.0;

    explicit Editor (Controller* c);
    void buildUI (VSTGUI::CFrame* f) override;
    void idle () override;
    void paramChanged (uint32_t id) override;

private:
    void onClose () override;
    void showMenu (VSTGUI::CPoint where);
    void updateBands (); // the columns of the bands in use, the Types of drives that are off dimmed

    Controller* ctl;
    std::unique_ptr<smacheratr::TailDisplays> tailDisplays;
    LevelView* levels = nullptr;
    std::vector<VSTGUI::CView*> headers;             // each band's name and range
    std::vector<VSTGUI::CView*> columns[kBands];     // each band's controls (with its header)
    pk::ParamView* driveType[kBands] = {};
};

} // namespace levlr
