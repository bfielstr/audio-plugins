#include "TailDisplays.h"

#include "Help.h"

#include "../core/TailExt.h"
#include "pluginkit/ui/InfoBox.h"

#include "vstgui/lib/cviewcontainer.h"

namespace smacheratr {

using namespace VSTGUI;

TailDisplays::TailDisplays (pk::ParamHost* editor, const TailBases& b, ColorView::RateSource r, ColorView::MeterSource m)
    : bases (b), rate (std::move (r)), meters (std::move (m))
{
    host = std::make_unique<pk::MappedParamHost> (editor, paramTable (), [b] (uint32_t id) -> int64_t {
        const int f = tailFieldOf (id);
        return f < 0 ? -1 : (int64_t)tailParamOf ((uint32_t)f, b);
    });
}

void TailDisplays::add (CViewContainer* parent, const CRect& area)
{
    const double w = std::min (230.0, area.getWidth () * 0.34);
    shaper = new ShaperView (CRect (area.left, area.top, area.left + w, area.bottom), host.get (), meters);
    pk::setHelp (shaper, "Analog Curve", help::kShaperDisplay);
    parent->addView (shaper);
    colorArea = CRect (area.left + w + 8, area.top, area.right, area.bottom);
    color = new ColorView (colorArea, host.get (), rate, meters);
    pk::setHelp (color, "Colour EQ", help::kColorDisplay);
    color->onBandPicked = [this] (int k) {
        if (bandPicked)
            bandPicked (k);
    };
    parent->addView (color);
    // No Overlap, above the colour display's right end
    noOverlap = new NoOverlapToggle (CRect (area.right - 90, area.top - 22, area.right, area.top - 4), host.get (), smacheratrBandParams ());
    noOverlap->setTooltipText (help::forParam (kClarityNoOverlap));
    parent->addView (noOverlap);
    // the bands' Slope, left of it
    parent->addView (new pk::Label (CRect (area.right - 244, area.top - 22, area.right - 206, area.top - 4), "Slope", 10.5));
    slope = new pk::Choice (CRect (area.right - 202, area.top - 22, area.right - 98, area.top - 4), host.get (), kClaritySlope);
    slope->setTooltipText (help::kSlope);
    parent->addView (slope);
    // Gentlr's Advanced mode: the region Drive and the Threshold sliders, in a strip at the right of
    // the colour display (hidden while Advanced is off)
    driveOn = new pk::Toggle (CRect (0, 0, 1, 1), host.get (), kClarityDrive, "Drive");
    driveOn->setTooltipText (help::forParam (kClarityDrive));
    parent->addView (driveOn);
    driveAmount = new pk::NumberBox (CRect (0, 0, 1, 1), host.get (), kClarityDriveAmount);
    driveAmount->setTooltipText (help::forParam (kClarityDriveAmount));
    parent->addView (driveAmount);
    for (int k = 0; k < kGentlrBands; ++k)
    {
        sliders[k] = new ThresholdSlider (CRect (0, 0, 1, 1), host.get (), k, meters);
        sliders[k]->setTooltipText (help::forParam (kGentlrThresholdIds[k]));
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
    for (int k = 0; k < kGentlrBands; ++k)
        if (sliders[k])
            sliders[k]->setEnabledLook (smacheratrBandParams ().works (host.get (), k));
    if (noOverlap)
        noOverlap->setEnabledLook (on >= 0.5);
    if (slope)
        slope->setEnabledLook (on >= 0.5);
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

bool TailDisplays::isTailParam (uint32_t id) const { return tailFieldIn (id, bases) >= 0; }

void TailDisplays::paramChanged (uint32_t id)
{
    if (!isTailParam (id))
        return;
    if (id == bases.ext2Base + pk::kTailExt2Advanced)
        layoutAdvanced ();
    updateLooks ();
    for (CView* v : {(CView*)shaper, (CView*)color, (CView*)sliders[0], (CView*)sliders[1], (CView*)sliders[2], (CView*)sliders[3],
                     (CView*)driveOn, (CView*)driveAmount, (CView*)noOverlap, (CView*)slope})
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
    driveOn = driveAmount = noOverlap = slope = nullptr;
}

} // namespace smacheratr
