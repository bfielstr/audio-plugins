#pragma once

#include "smacheratr/src/ui/TailDisplays.h"
#include "smacheratr/src/ui/ThresholdSlider.h"

#include "../core/Params.h"

#include "pluginkit/vst/EditorBase.h"

#include <memory>
#include <vector>

namespace gently {

class Controller;
class GentlyView;

// The band display on top (with Advanced, the bands' Threshold sliders at its right edge: band 1, band
// 2, Sub, High), a row of controls (each band's On, Frequency, Width and Range; the Sub and High bands'
// On, Frequency and Range; the detector's Attack, Release and stereo mode; Advanced and the region
// Drive; No Overlap, Mix and Output), then the end Smacheratr.
class Editor : public pk::EditorBase
{
public:
    static constexpr double kWidth = 1160.0;
    static constexpr double kHeight = 660.0;
    // layout (also used by the host test)
    static constexpr double kViewLeft = 8.0, kViewTop = 40.0, kViewRight = 1152.0, kViewBottom = 292.0;
    static constexpr double kRowTop = 300.0, kTailTop = 404.0;
    static constexpr double kBandW = 198.0; // each band's controls
    static constexpr double kSubLeft = kViewLeft + kBands * kBandW, kSubW = 156.0; // the Sub band's
    static constexpr double kHighLeft = kSubLeft + kSubW;                           // the High band's (as wide)
    static constexpr double kNoOverlapLeft = kViewRight - 2 * 56.0 - 12.0;          // No Overlap, above Mix and Output

    explicit Editor (Controller* c);
    void buildUI (VSTGUI::CFrame* f) override;
    void idle () override;
    void paramChanged (uint32_t id) override;

private:
    void onClose () override;
    void showMenu (VSTGUI::CPoint where);
    void layoutAdvanced ();
    void updateLooks ();

    Controller* ctl;
    std::unique_ptr<smacheratr::TailDisplays> tailDisplays;
    std::unique_ptr<pk::MappedParamHost> sliderHost; // Smacheratr's Threshold sliders on Gently's Thresholds
    GentlyView* view = nullptr;
    smacheratr::ThresholdSlider* sliders[kAllBands] = {nullptr, nullptr, nullptr, nullptr}; // (ThresholdSlider::layout takes all four)
    std::vector<VSTGUI::CView*> headers;              // each band's name and edges (the Sub and High bands' too)
    std::vector<pk::ParamView*> bandViews[kAllBands]; // each band's knobs (dim while it is off)
    std::vector<VSTGUI::CView*> advancedViews;     // the region Drive (shown with Advanced)
    pk::ParamView* driveAmount = nullptr;
};

} // namespace gently
