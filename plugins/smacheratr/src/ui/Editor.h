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
// chooser and Color below it, then Drive / Output / Dry/Wet. On the right the colour display, with the
// Color | Gentlr switch above it: the layer in front has its handles drawn and grabbable, and the knobs
// under the display are its own (Color: Amt Lo / Amt Hi / Freq / Width; Gentlr: the selected band's Freq /
// Width / Range). A Gentlr band is selected by its handle, its button in the GENTLR panel or its
// Threshold slider (Advanced); its handle and its slider are lit.
class Editor : public pk::EditorBase
{
public:
    static constexpr double kWidth = 760.0;
    static constexpr double kHeight = 520.0;
    // layout (also used by the host test)
    static constexpr double kShaperLeft = 8.0, kShaperTop = 68.0, kShaperWidth = 300.0, kShaperHeight = 190.0;
    static constexpr double kColorLeft = 316.0, kColorTop = 68.0, kColorViewWidth = 436.0, kColorViewHeight = 262.0;
    // the Color | Gentlr switch above the colour display's right end, and the knobs under the display
    static constexpr double kLayerW = 140.0, kLayerLeft = kColorLeft + kColorViewWidth - kLayerW, kLayerTop = 42.0;
    static constexpr double kLayerKnobsLeft = kColorLeft + 20.0, kLayerKnobStep = 104.0, kLayerKnobsTop = 346.0;
    // the GENTLR panel at the bottom, in groups with room between them: the Gentlr button at
    // (kGentlrButtonX, kGentlrTop + 40); the band buttons beside it with the Slope under them at (kSlopeX,
    // kGentlrTop + 64) (the shown band's Freq, Width and Range knobs are under the colour display, with
    // Gentlr in front); Advanced at (kGentlrAdvancedX, kGentlrTop + 40) with No Overlap under it at
    // (kNoOverlapX, kNoOverlapY); the region Drive last
    static constexpr double kGentlrTop = 432.0, kGentlrButtonX = 56.0, kGentlrAdvancedX = 527.0, kSlopeX = 194.0;
    static constexpr double kNoOverlapX = kGentlrAdvancedX, kNoOverlapY = kGentlrTop + 64.0;

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
    std::vector<pk::ParamView*> colorViews;                // the colour amounts (dim while Color is off)
    std::vector<pk::ParamView*> colorKnobs;                // those of them under the display (with Color in front)
    std::vector<pk::ParamView*> clarityViews[kGentlrBands]; // each band's knobs (with Gentlr in front, one band shown)
    std::vector<VSTGUI::CView*> clarityBandButtons;
    VSTGUI::CView* layerSwitch = nullptr;
    int clarityBand = 0; // the band shown
    void showClarityBand (int band);
    void setLayer (int layer); // 0 Color, 1 Gentlr (in front of the colour display; kept per instance)
    int layer () const;
    // Gentlr's Advanced mode: the Threshold sliders at the right of the colour display, the region
    // Drive's controls in the GENTLR panel (shown while Advanced is on)
    ThresholdSlider* thresholdSliders[kGentlrBands] = {nullptr, nullptr, nullptr, nullptr};
    std::vector<pk::ParamView*> advancedViews;
    pk::ParamView* noOverlapView = nullptr; // No Overlap (dim while Gentlr is off)
    pk::ParamView* slopeView = nullptr;     // the bands' Slope (dim while Gentlr is off)
    void layoutAdvanced ();
};

} // namespace smacheratr
