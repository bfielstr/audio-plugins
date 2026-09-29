// Smacheratr's two displays (the Analog curve, and the colour curve with Clarity's band) for the
// saturator at the end of another plug-in's chain: the displays work on the plug-in's tail
// parameters (pk::addTailParams at `base`, the extended block at `extBase`) through a mapping, and
// read the tail's levels and the sample rate through functions.
#pragma once

#include "ColorView.h"
#include "ShaperView.h"

#include "pluginkit/ui/Widgets.h"

#include <memory>

namespace smacheratr {

class TailDisplays
{
public:
    // `editor` is the plug-in's editor (the host of its own parameters)
    TailDisplays (pk::ParamHost* editor, uint32_t base, uint32_t extBase, ColorView::RateSource rate,
                  ColorView::MeterSource meters);

    // Adds both displays to `parent` (usually the tail panel), side by side in `area`.
    void add (VSTGUI::CViewContainer* parent, const VSTGUI::CRect& area);
    void idle ();
    void paramChanged (uint32_t id); // redraws when a tail parameter changes
    void closed ();                  // the editor closed: its views are gone

    // Extra height the tail panel needs for the displays.
    static constexpr double kHeight = 170.0;

private:
    bool isTailParam (uint32_t id) const;

    std::unique_ptr<pk::MappedParamHost> host;
    uint32_t base, extBase;
    ColorView::RateSource rate;
    ColorView::MeterSource meters;
    ShaperView* shaper = nullptr;
    ColorView* color = nullptr;
};

} // namespace smacheratr
