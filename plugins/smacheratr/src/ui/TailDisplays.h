// Smacheratr's two displays (the Analog curve, and the colour curve with Gently's bands) for the
// saturator at the end of another plug-in's chain: the displays work on the plug-in's tail
// parameters (pk::addTailParams at `base`, the extended block at `extBase`, Gently's Advanced block at
// `ext2Base`) through a mapping, and read the tail's levels and the sample rate through functions.
// While Gently's Advanced is on, a strip at the right of the colour display holds the region Drive
// (on / amount) and the bands' Threshold sliders.
#pragma once

#include "ColorView.h"
#include "ShaperView.h"
#include "ThresholdSlider.h"

#include "pluginkit/ui/Widgets.h"

#include <memory>

namespace smacheratr {

class TailDisplays
{
public:
    // `editor` is the plug-in's editor (the host of its own parameters)
    TailDisplays (pk::ParamHost* editor, uint32_t base, uint32_t extBase, uint32_t ext2Base, ColorView::RateSource rate,
                  ColorView::MeterSource meters);

    // Adds both displays to `parent` (usually the tail panel), side by side in `area`.
    void add (VSTGUI::CViewContainer* parent, const VSTGUI::CRect& area);
    void idle ();
    void paramChanged (uint32_t id); // redraws when a tail parameter changes
    void closed ();                  // the editor closed: its views are gone
    void onBandPicked (std::function<void (int)> f); // a Gently band grabbed in the display

    // Extra height the tail panel needs for the displays.
    static constexpr double kHeight = 170.0;
    std::function<void (int)> bandPicked;

private:
    bool isTailParam (uint32_t id) const;
    void layoutAdvanced ();
    void updateLooks ();

    std::unique_ptr<pk::MappedParamHost> host;
    uint32_t base, extBase, ext2Base;
    ColorView::RateSource rate;
    ColorView::MeterSource meters;
    ShaperView* shaper = nullptr;
    ColorView* color = nullptr;
    VSTGUI::CRect colorArea;
    ThresholdSlider* sliders[kClarityBands] = {nullptr, nullptr};
    pk::ParamView* driveOn = nullptr;
    pk::ParamView* driveAmount = nullptr;
};

} // namespace smacheratr
