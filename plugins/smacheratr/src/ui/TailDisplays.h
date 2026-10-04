// Smacheratr's two displays (the Analog curve, and the colour curve with Gentlr's bands) for the
// saturator at the end of another plug-in's chain: the displays work on the plug-in's tail
// parameters (its four blocks, TailBases: pk::addTailParams, the extended block, Gentlr's Advanced
// block, its High band and No Overlap) through a mapping, and read the tail's levels and the sample
// rate through functions. While Gentlr's Advanced is on, a strip at the right of the colour display
// holds the region Drive (on / amount) and the bands' Threshold sliders. Gentlr's No Overlap button
// goes above the colour display's right end (in the tail panel's title row), its bands' Slope left of it.
#pragma once

#include "ColorView.h"
#include "ShaperView.h"
#include "ThresholdSlider.h"

#include "../core/TailExt.h"

#include "pluginkit/ui/Widgets.h"

#include <memory>

namespace smacheratr {

class TailDisplays
{
public:
    // `editor` is the plug-in's editor (the host of its own parameters)
    TailDisplays (pk::ParamHost* editor, const TailBases& bases, ColorView::RateSource rate, ColorView::MeterSource meters);

    // Adds both displays to `parent` (usually the tail panel), side by side in `area`.
    void add (VSTGUI::CViewContainer* parent, const VSTGUI::CRect& area);
    void idle ();
    void paramChanged (uint32_t id); // redraws when a tail parameter changes
    void closed ();                  // the editor closed: its views are gone
    void onBandPicked (std::function<void (int)> f); // a Gentlr band grabbed in the display

    // Extra height the tail panel needs for the displays.
    static constexpr double kHeight = 170.0;
    std::function<void (int)> bandPicked;

private:
    bool isTailParam (uint32_t id) const;
    void layoutAdvanced ();
    void updateLooks ();

    std::unique_ptr<pk::MappedParamHost> host;
    TailBases bases;
    ColorView::RateSource rate;
    ColorView::MeterSource meters;
    ShaperView* shaper = nullptr;
    ColorView* color = nullptr;
    VSTGUI::CRect colorArea;
    ThresholdSlider* sliders[kGentlrBands] = {nullptr, nullptr, nullptr, nullptr};
    pk::ParamView* noOverlap = nullptr;
    pk::ParamView* slope = nullptr;
    pk::ParamView* driveOn = nullptr;
    pk::ParamView* driveAmount = nullptr;
};

} // namespace smacheratr
