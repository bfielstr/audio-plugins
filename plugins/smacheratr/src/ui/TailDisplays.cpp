#include "TailDisplays.h"

#include "Help.h"

#include "../core/TailExt.h"

#include "vstgui/lib/cviewcontainer.h"

namespace smacheratr {

using namespace VSTGUI;

TailDisplays::TailDisplays (pk::ParamHost* editor, uint32_t b, uint32_t e, uint32_t e2, ColorView::RateSource r,
                            ColorView::MeterSource m)
    : base (b), extBase (e), ext2Base (e2), rate (std::move (r)), meters (std::move (m))
{
    host = std::make_unique<pk::MappedParamHost> (editor, paramTable (), [b, e, e2] (uint32_t id) -> int64_t {
        const int f = tailFieldOf (id);
        return f < 0 ? -1 : (int64_t)tailParamOf ((uint32_t)f, b, e, e2);
    });
}

void TailDisplays::add (CViewContainer* parent, const CRect& area)
{
    const double w = std::min (230.0, area.getWidth () * 0.34);
    shaper = new ShaperView (CRect (area.left, area.top, area.left + w, area.bottom), host.get (), meters);
    shaper->setTooltipText (help::kShaperDisplay);
    parent->addView (shaper);
    colorArea = CRect (area.left + w + 8, area.top, area.right, area.bottom);
    color = new ColorView (colorArea, host.get (), rate, meters);
    color->setTooltipText (help::kColorDisplay);
    color->onBandPicked = [this] (int k) {
        if (bandPicked)
            bandPicked (k);
    };
    parent->addView (color);
    // Gently's Advanced mode: the region Drive and the Threshold sliders, in a strip at the right of
    // the colour display (hidden while Advanced is off)
    driveOn = new pk::Toggle (CRect (0, 0, 1, 1), host.get (), kClarityDrive, "Drive");
    driveOn->setTooltipText (help::forParam (kClarityDrive));
    parent->addView (driveOn);
    driveAmount = new pk::NumberBox (CRect (0, 0, 1, 1), host.get (), kClarityDriveAmount);
    driveAmount->setTooltipText (help::forParam (kClarityDriveAmount));
    parent->addView (driveAmount);
    for (int k = 0; k < kClarityBands; ++k)
    {
        sliders[k] = new ThresholdSlider (CRect (0, 0, 1, 1), host.get (), k, meters);
        sliders[k]->setTooltipText (help::forParam (kClarityThresholdIds[k]));
        parent->addView (sliders[k]);
    }
    layoutAdvanced ();
    updateLooks ();
}

void TailDisplays::layoutAdvanced ()
{
    if (!color)
        return;
    ThresholdSlider::layout (color, sliders, colorArea, host->plainValue (kClarityAdvanced) >= 0.5, {driveOn, driveAmount});
}

void TailDisplays::updateLooks ()
{
    const double on = host->plainValue (kClarity);
    for (int k = 0; k < kClarityBands; ++k)
        if (sliders[k])
            sliders[k]->setEnabledLook (clarityBandOn (on, host->plainValue (kClarityRangeIds[k])));
    if (driveOn)
        driveOn->setEnabledLook (on >= 0.5);
    if (driveAmount)
        driveAmount->setEnabledLook (on >= 0.5 && host->plainValue (kClarityDrive) >= 0.5);
}

void TailDisplays::idle ()
{
    if (shaper)
        shaper->idle ();
    if (color)
        color->idle ();
    for (auto* s : sliders)
        if (s && s->isVisible ())
            s->idle ();
}

bool TailDisplays::isTailParam (uint32_t id) const
{
    return (id >= base && id < base + pk::kTailFields) || (id >= extBase && id < extBase + pk::kTailExtFields) ||
           (id >= ext2Base && id < ext2Base + pk::kTailExt2Fields);
}

void TailDisplays::paramChanged (uint32_t id)
{
    if (!isTailParam (id))
        return;
    if (id == ext2Base + pk::kTailExt2Advanced)
        layoutAdvanced ();
    updateLooks ();
    for (CView* v : {(CView*)shaper, (CView*)color, (CView*)sliders[0], (CView*)sliders[1], (CView*)driveOn, (CView*)driveAmount})
        if (v)
            v->invalid ();
}

void TailDisplays::onBandPicked (std::function<void (int)> f) { bandPicked = std::move (f); }

void TailDisplays::closed ()
{
    shaper = nullptr;
    color = nullptr;
    for (auto& s : sliders)
        s = nullptr;
    driveOn = driveAmount = nullptr;
}

} // namespace smacheratr
