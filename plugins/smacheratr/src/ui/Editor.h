#pragma once

#include "pluginkit/vst/EditorBase.h"

#include <vector>

namespace smacheratr {

class Controller;
class ShaperView;
class ColorView;

// Laid out like Live's Saturator: the curve chooser above the shaper display, the post clip
// chooser, Color and Amt Lo below it, then Drive / Output / Dry/Wet. The expanded controls
// (colour curve with Amt Hi / Freq / Width, the Waveshaper) are always shown on the right.
class Editor : public pk::EditorBase
{
public:
    static constexpr double kWidth = 760.0;
    static constexpr double kHeight = 440.0;
    // layout (also used by the host test)
    static constexpr double kShaperLeft = 8.0, kShaperTop = 68.0, kShaperWidth = 300.0, kShaperHeight = 190.0;
    static constexpr double kColorLeft = 316.0, kColorTop = 40.0, kColorViewWidth = 436.0, kColorViewHeight = 160.0;

    explicit Editor (Controller* c);
    void buildUI (VSTGUI::CFrame* f) override;
    void idle () override;
    void paramChanged (uint32_t id) override;

private:
    void onClose () override;
    void updateLooks ();
    void showMenu (VSTGUI::CPoint where);

    Controller* ctl;
    ShaperView* shaper = nullptr;
    ColorView* color = nullptr;
    pk::Label* status = nullptr;
    pk::ParamView* bassView = nullptr;
    std::vector<pk::ParamView*> wsViews, colorViews;
};

} // namespace smacheratr
