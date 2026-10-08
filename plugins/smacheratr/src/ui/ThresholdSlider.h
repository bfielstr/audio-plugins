// Gentlr's Threshold for one band (Advanced mode) as a vertical slider, with the band's level next to
// it: the level Gentlr measures in the band (its peak level going into the curve) rises from the
// bottom as a meter, energy idle below the threshold and lit cinnabar over it (there the band is being
// cut); the threshold is the text-coloured line with the handle.
//   drag up / down              the Threshold (Shift: fine)
//   double-click or right-click reset it (-18 dB: where Gentlr starts without Advanced)
//   mouse wheel                 step it
// Grabbing a slider picks its band (onPicked: the editor shows that band's controls and lights its handle
// in the colour display); the band picked is drawn selected (setSelected: a lit lamp under its name).
// Used by Smacheratr, the saturator at the end of the other plug-ins (TailPanel) and the Smacheratr
// in Smemplr's rack; layout() puts the sliders at the right edge of the colour display.
#pragma once

#include "ColorView.h"

#include "pluginkit/ui/CachedLayer.h"
#include "pluginkit/ui/Widgets.h"

#include <functional>
#include <vector>

namespace smacheratr {

class ThresholdSlider : public pk::ParamView
{
public:
    using MeterSource = ColorView::MeterSource;
    static constexpr double kWidth = 24.0, kGap = 4.0;
    static constexpr double kStripWidth = kGentlrBands * (kWidth + kGap); // what the sliders take from the display

    ThresholdSlider (const VSTGUI::CRect& r, pk::ParamHost* host, int band, MeterSource meters);
    void draw (VSTGUI::CDrawContext* ctx) override;
    void onMouseDownEvent (VSTGUI::MouseDownEvent& e) override;
    void onMouseMoveEvent (VSTGUI::MouseMoveEvent& e) override;
    void onMouseUpEvent (VSTGUI::MouseUpEvent& e) override;
    void onMouseCancelEvent (VSTGUI::MouseCancelEvent& e) override;
    void onMouseWheelEvent (VSTGUI::MouseWheelEvent& e) override;
    void idle (); // follows the band's level
    int bandIndex () const { return band; }
    // the band shown in the editor (its Gentlr controls, its handle lit): this slider lit too
    void setSelected (bool s)
    {
        if (s != selected)
        {
            selected = s;
            invalid ();
        }
    }
    bool isSelected () const { return selected; }
    std::function<void (int)> onPicked; // the slider was grabbed (its band)

    // Lays the colour display and all four bands' sliders out in `area` (where the display alone sits
    // without Advanced): with `advanced` the sliders take a strip at its right edge (kStripWidth wide)
    // and `above` (optional views, e.g. the region Drive's controls) are stacked at the top of that
    // strip, 18 high each; without it the display takes the whole area and those views are hidden.
    static void layout (ColorView* color, ThresholdSlider* const* sliders, const VSTGUI::CRect& area, bool advanced,
                        const std::vector<VSTGUI::CView*>& above = {});

private:
    VSTGUI::CRect track () const;
    double yOfDb (double db) const;

    int band;
    MeterSource meters;
    float shownDb = -120.0f; // the band's level, eased
    bool dragging = false, selected = false;
    // the bed (the well, its outline, the band's name and lamp): drawn once, blitted while the level moves
    pk::CachedLayer bed;
    double startY = 0.0, startValue = 0.0;
};

} // namespace smacheratr
