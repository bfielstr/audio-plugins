#pragma once

#include "../core/Params.h"

#include "pluginkit/vst/EditorBase.h"

#include <vector>

namespace smacheratr {

class Controller;
class ShaperView;
class ColorView;
class ThresholdSlider;

// Laid out like Live's Saturator: the Analog curve with the pre-limiter above it, the post clip
// chooser, Color and Amt Lo below it, then Drive / Output / Dry/Wet. The expanded controls (the
// colour curve with Amt Hi / Freq / Width) are on the right.
class Editor : public pk::EditorBase
{
public:
    static constexpr double kWidth = 760.0;
    static constexpr double kHeight = 520.0;
    // layout (also used by the host test)
    static constexpr double kShaperLeft = 8.0, kShaperTop = 68.0, kShaperWidth = 300.0, kShaperHeight = 190.0;
    static constexpr double kColorLeft = 316.0, kColorTop = 40.0, kColorViewWidth = 436.0, kColorViewHeight = 290.0;
    // the GENTLR panel at the bottom: the Gentlr button at (kGentlrButtonX, kGentlrTop + 40), Advanced
    // at (kGentlrAdvancedX, kGentlrTop + 40), No Overlap at (kNoOverlapX, kGentlrTop + 40), the Slope under
    // the band buttons at (kSlopeX, kGentlrTop + 64)
    static constexpr double kGentlrTop = 432.0, kGentlrButtonX = 56.0, kGentlrAdvancedX = 516.0, kNoOverlapX = 712.0, kSlopeX = 182.0;

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
    pk::ParamView* thresholdView = nullptr;
    std::vector<pk::ParamView*> colorViews;
    std::vector<pk::ParamView*> clarityViews[kGentlrBands]; // each band's knobs (one band shown)
    std::vector<VSTGUI::CView*> clarityBandButtons;
    int clarityBand = 0; // the band shown
    void showClarityBand (int band);
    // Gentlr's Advanced mode: the Threshold sliders at the right of the colour display, the region
    // Drive's controls in the GENTLR panel (shown while Advanced is on)
    ThresholdSlider* thresholdSliders[kGentlrBands] = {nullptr, nullptr, nullptr, nullptr};
    std::vector<pk::ParamView*> advancedViews;
    pk::ParamView* noOverlapView = nullptr; // No Overlap (dim while Gentlr is off)
    pk::ParamView* slopeView = nullptr;     // the bands' Slope (dim while Gentlr is off)
    void layoutAdvanced ();
};

} // namespace smacheratr
