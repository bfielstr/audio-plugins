#pragma once

#include <memory>

#include "pluginkit/vst/EditorBase.h"

namespace para {

class Controller;
class FilterView;
class GainLockHost;

// The display on top and the controls in titled sections below it, as in Live's devices:
// HIGH-PASS, LOW-PASS (each with its Gain Lock) and SPLIT (the two slopes too) on the first row, OUTPUT
// (movement and the drive too; Soft Clip and Hard Clip, the end of the chain, under Dry/Wet and Output) on
// the second.
class Editor : public pk::EditorBase
{
public:
    static constexpr double kWidth = 760.0;
    // layout (also used by the host test)
    static constexpr double kViewLeft = 8.0, kViewTop = 40.0, kViewRight = 752.0, kViewBottom = 290.0;
    static constexpr double kRow1Top = 298.0, kRow1Bottom = 422.0, kRow2Top = 428.0, kRow2Bottom = kRow2Top + 122.0;
    // in OUTPUT: Soft Clip and Hard Clip side by side under Dry/Wet and Output
    static constexpr double kClipTop = 94.0, kClipW = 80.0, kClipH = 18.0;
    // OUTPUT ends the window, 8 px clear under it
    static constexpr double kHeight = kRow2Bottom + 8.0;

    explicit Editor (Controller* c);
    ~Editor () override;
    void buildUI (VSTGUI::CFrame* f) override;
    // the panels for Menu > Layout (pluginkit/Layout.h): the Wide template's rows by purpose
    pk::layout::Spec layoutSpec (bool arranged) const override;
    // The Basic page (pluginkit/ui/BasicView.h): the filter response, HP Freq, LP Freq and Movement; Dry/Wet,
    // Output, Soft Clip and Hard Clip
    pk::basic::Spec basicSpec () override;
    void idle () override;
    void paramChanged (uint32_t id) override;

private:
    void onClose () override;
    void showMenu (VSTGUI::CPoint where);
    void updateLooks ();

    Controller* ctl;
    std::unique_ptr<GainLockHost> lockHost; // in front of this editor for the gains and their locks
    FilterView* view = nullptr;
    pk::ParamView* lpResKnob = nullptr;
    // dimmed while their drive is off: the high-pass's and the low-pass's amount, and Pre/Post (both off)
    pk::ParamView* hpDriveKnob = nullptr;
    pk::ParamView* lpDriveKnob = nullptr;
    pk::ParamView* drivePosView = nullptr;
};

} // namespace para
