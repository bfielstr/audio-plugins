#pragma once

#include "smacheratr/src/ui/TailDisplays.h"

#include <memory>

#include "pluginkit/vst/EditorBase.h"

namespace para {

class Controller;
class FilterView;

// The display on top and the controls in titled sections below it, as in Live's devices:
// HIGH-PASS, LOW-PASS and SPLIT on the first row, TRACKING and OUTPUT on the second.
class Editor : public pk::EditorBase
{
public:
    static constexpr double kWidth = 760.0;
    static constexpr double kHeight = 606.0 + 170.0; // the tail panel's displays (smacheratr::TailDisplays::kHeight)
    // layout (also used by the host test)
    static constexpr double kViewLeft = 8.0, kViewTop = 40.0, kViewRight = 752.0, kViewBottom = 290.0;

    explicit Editor (Controller* c);
    void buildUI (VSTGUI::CFrame* f) override;
    void idle () override;
    void paramChanged (uint32_t id) override;

private:
    std::unique_ptr<smacheratr::TailDisplays> tailDisplays;
    void onClose () override;
    void showMenu (VSTGUI::CPoint where);
    void updateLooks ();

    Controller* ctl;
    FilterView* view = nullptr;
    pk::ParamView* lpResKnob = nullptr;
};

} // namespace para
