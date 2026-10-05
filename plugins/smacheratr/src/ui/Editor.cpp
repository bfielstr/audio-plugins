#include "Editor.h"

#include "BandPush.h"
#include "ColorView.h"
#include "Help.h"
#include "ShaperView.h"
#include "ThresholdSlider.h"
#include "plugin/Controller.h"

#include "pluginkit/vst/Clipboard.h"
#include "pluginkit/ui/Theme.h"
#include "pluginkit/vst/PresetBar.h"

#include "vstgui/lib/cframe.h"
#include "vstgui/lib/controls/coptionmenu.h"

#include <cmath>
#include <cstdio>

namespace smacheratr {

using namespace VSTGUI;
using pk::ActionButton;
using pk::Choice;
using pk::Knob;
using pk::Label;
using pk::NumberBox;
using pk::Toggle;

namespace {
constexpr double kKnobW = 56, kKnobH = 64;
CRect knobRect (double x, double y, double w = kKnobW, double h = kKnobH) { return CRect (x, y, x + w, y + h); }

class Background : public CViewContainer
{
public:
    using CViewContainer::CViewContainer;
    void drawBackgroundRect (CDrawContext* ctx, const CRect&) override
    {
        // the ground, the header band and the copper window frame (docs/THEME.md, "Window")
        pk::draw::window (ctx, CRect (0, 0, getViewSize ().getWidth (), getViewSize ().getHeight ()), 34);
    }
};
} // namespace

Editor::Editor (Controller* c) : pk::EditorBase (c, kWidth, kHeight), ctl (c) {}

void Editor::onClose ()
{
    shaper = nullptr;
    color = nullptr;
    status = nullptr;
    thresholdView = nullptr;
    colorViews.clear ();
    colorKnobs.clear ();
    layerSwitch = nullptr;
    for (auto& v : clarityViews)
        v.clear ();
    clarityBandButtons.clear ();
    for (auto& t : thresholdSliders)
        t = nullptr;
    advancedViews.clear ();
    noOverlapView = nullptr;
    slopeView = nullptr;
}

void Editor::buildUI (CFrame* f)
{
    auto* root = new Background (CRect (0, 0, kWidth, kHeight));
    f->addView (root);
    root->addView (new Label (CRect (12, 6, 200, 28), "smacheratr", 14.0, true));
    status = new Label (CRect (200, 6, 430, 28), "", 10.5);
    root->addView (new pk::PresetBar (CRect (440, 6, 636, 28), ctl));
    status->setDim (true);
    root->addView (status);
    auto* helpBtn = new ActionButton (CRect (644, 6, 666, 28), "?", [this] { setTooltipsEnabled (!tooltipsEnabled ()); },
                                      [this] { return tooltipsEnabled (); });
    helpBtn->setTooltipText ("Show or hide the floating help tooltips (the info box at the bottom shows the same help either way).");
    root->addView (helpBtn);
    root->addView (new ActionButton (CRect (672, 6, 752, 28), "Menu", [this] { showMenu (layoutPoint (CPoint (672, 28))); }));

    // left: the device as Live shows it, with the pre-limiter in front of the curve
    bind (root, new Toggle (CRect (kShaperLeft, 40, kShaperLeft + 76, 62), this, kPreLimit, "Pre-Limit"));
    thresholdView = bind (root, new NumberBox (CRect (kShaperLeft + 80, 42, kShaperLeft + 140, 60), this, kPreLimitThreshold));
    shaper = new ShaperView (CRect (kShaperLeft, kShaperTop, kShaperLeft + kShaperWidth, kShaperTop + kShaperHeight), this,
                             [c = ctl] () -> const Meters* {
                                 auto* s = c->getShared ();
                                 return s ? &s->meters : nullptr;
                             });
    pk::setHelp (shaper, "Analog Curve", help::kShaperDisplay);
    root->addView (shaper);
    bind (root, new Choice (CRect (8, 266, 112, 288), this, kPostClip));
    bind (root, new Toggle (CRect (120, 266, 176, 288), this, kColorOn, "Color"));
    bind (root, new Knob (knobRect (24, 346, 68, 78), this, kDrive, nullptr, true));
    bind (root, new Knob (knobRect (128, 346, 68, 78), this, kOutput));
    bind (root, new Knob (knobRect (232, 346, 68, 78), this, kDryWet));

    // right: the colour curve and its controls
    color = new ColorView (
        CRect (kColorLeft, kColorTop, kColorLeft + kColorViewWidth, kColorTop + kColorViewHeight), this,
        [c = ctl] () {
            auto* s = c->getShared ();
            return s ? s->sampleRate.load (std::memory_order_relaxed) : 48000.0;
        },
        [c = ctl] () -> const Meters* {
            auto* s = c->getShared ();
            return s ? &s->meters : nullptr;
        });
    pk::setHelp (color, "Colour EQ", help::kColorDisplay);
    root->addView (color);
    // which layer of it is in front, above its right end
    layerSwitch = new pk::ViewSwitch (CRect (kLayerLeft, kLayerTop, kLayerLeft + kLayerW, kLayerTop + 18), {"Color", "Gentlr"},
                                      [this] { return layer (); }, [this] (int i) { setLayer (i); });
    layerSwitch->setTooltipText ("Which layer of the colour display is in front: the colour filters (their points, and Amt Lo, Amt "
                                 "Hi, Freq and Width under the display) or Gentlr (its band handles, and the selected band's Freq, "
                                 "Width and Range under the display). The other layer is drawn faint behind.");
    root->addView (layerSwitch);
    // with Color in front: its four amounts under the display
    const uint32_t colorIds[4] = {kColorLo, kColorHi, kColorFreq, kColorWidth};
    for (int i = 0; i < 4; ++i)
    {
        colorKnobs.push_back (bind (root, new Knob (knobRect (kLayerKnobsLeft + i * kLayerKnobStep, kLayerKnobsTop), this, colorIds[i])));
        colorViews.push_back (colorKnobs.back ());
    }

    // Gentlr's Threshold sliders (Advanced), at the right edge of the colour display while Advanced is on
    for (int k = 0; k < kGentlrBands; ++k)
    {
        thresholdSliders[k] = bind (root, new ThresholdSlider (CRect (0, 0, 1, 1), this, k, [c = ctl] () -> const Meters* {
                                        auto* s = c->getShared ();
                                        return s ? &s->meters : nullptr;
                                    }));
        // grabbing a band's Threshold selects the band (Gentlr's layer in front, its knobs under the display)
        thresholdSliders[k]->onPicked = [this] (int b) {
            setLayer (1);
            showClarityBand (b);
        };
    }

    // bottom: Gentlr (one button), a band selector (the selected band's Frequency, Width and Range are under
    // the colour display, with Gentlr in front); the bands' Slope under the selector; Advanced, and with it
    // the region Drive; No Overlap
    auto* cp = new pk::Panel (CRect (8, kGentlrTop, 752, kGentlrTop + 80), "GENTLR");
    root->addView (cp);
    bind (cp, new Toggle (CRect (12, 30, 84, 50), this, kClarity, "Gentlr"));
    clarityBandButtons.clear ();
    for (int k = 0; k < kGentlrBands; ++k)
    {
        static const char* const names[kGentlrBands] = {"Band 1", "Band 2", "Sub", "High"};
        static const char* const tips[kGentlrBands] = {
            "Show Gentlr's first band (Gentlr in the display).",
            "Show Gentlr's second band (Gentlr 2 in the display; it works once its Range is above 0 dB).",
            "Show Gentlr's Sub band (from the bottom of the spectrum, it starts to taper at its Freq; it works once its "
            "Range is above 0 dB).",
            "Show Gentlr's High band (from its Freq, where it starts to taper, to the top of the spectrum; it works once "
            "its Range is above 0 dB)."};
        // (Band 1 and Band 2 wider than Sub and High, for their longer names)
        static const double left[kGentlrBands] = {92, 145, 198, 241}, right[kGentlrBands] = {142, 195, 238, 281};
        auto* bt = new ActionButton (CRect (left[k], 30, right[k], 50), names[k],
                                     [this, k] {
                                         setLayer (1); // (its knobs are Gentlr's layer's)
                                         showClarityBand (k);
                                     },
                                     [this, k] { return clarityBand == k; });
        bt->setTooltipText (tips[k]);
        cp->addView (bt);
        clarityBandButtons.push_back (bt);
        // its knobs under the colour display, where the colour amounts are with Color in front
        auto knobAt = [&] (int i, uint32_t id, const char* name) {
            clarityViews[k].push_back (
                bind (root, new Knob (knobRect (kLayerKnobsLeft + i * kLayerKnobStep, kLayerKnobsTop), this, id, name)));
        };
        if (!hasWidth (k))
        {
            // the Sub and High bands: Freq and Range where the other bands have theirs (no width, no button: a
            // band works while its Range is above 0 dB)
            knobAt (0, kGentlrFreqIds[k], "Freq");
            knobAt (2, kGentlrRangeIds[k], "Range");
            continue;
        }
        knobAt (0, kClarityFreqIds[k], "Freq");
        knobAt (1, kClarityWidthIds[k], "Width");
        knobAt (2, kClarityRangeIds[k], "Range");
    }
    color->onBandPicked = [this] (int k) { showClarityBand (k); };
    setLayer (layer ());
    cp->addView (new Label (CRect (92, 54, 132, 74), "Slope", 10.5));
    slopeView = bind (cp, new Choice (CRect (kSlopeX - 8 - 50, 54, kSlopeX - 8 + 50, 74), this, kClaritySlope));
    // how the bands sit together: Advanced, No Overlap under it (both columns 82 wide)
    bind (cp, new Toggle (CRect (kGentlrAdvancedX - 8 - 41, 30, kGentlrAdvancedX - 8 + 41, 50), this, kClarityAdvanced, "Advanced"));
    noOverlapView = bind (cp, new NoOverlapToggle (CRect (kNoOverlapX - 8 - 41, kNoOverlapY - kGentlrTop - 10, kNoOverlapX - 8 + 41,
                                                          kNoOverlapY - kGentlrTop + 10),
                                                   this, smacheratrBandParams ()));
    advancedViews.push_back (bind (cp, new Toggle (CRect (566, 30, 616, 50), this, kClarityDrive, "Drive")));
    advancedViews.push_back (bind (cp, new Knob (knobRect (622, 10), this, kClarityDriveAmount, "Amount")));
    layoutAdvanced ();

    applyParamTooltips (&help::forParam);
    updateLooks ();
    idle ();
}

void Editor::showClarityBand (int band)
{
    clarityBand = band < 0 ? 0 : band >= kGentlrBands ? kGentlrBands - 1 : band;
    const bool gentlrFront = layer () == 1;
    for (int k = 0; k < kGentlrBands; ++k)
    {
        for (auto* v : clarityViews[k])
            v->setVisible (gentlrFront && k == clarityBand);
        if (thresholdSliders[k])
            thresholdSliders[k]->setSelected (k == clarityBand);
    }
    for (auto* v : colorKnobs)
        v->setVisible (!gentlrFront);
    if (color)
        color->setSelectedBand (clarityBand);
    for (auto* b : clarityBandButtons)
        b->invalid ();
}

int Editor::layer () const { return controller->uiColorLayer == 1 ? 1 : 0; }

void Editor::setLayer (int l)
{
    controller->uiColorLayer = l == 1 ? 1 : 0;
    if (color)
        color->setLayer (l == 1 ? ColorView::Layer::Gentlr : ColorView::Layer::Color);
    if (layerSwitch)
        layerSwitch->invalid ();
    showClarityBand (clarityBand); // (the knobs under the display: the layer's)
}

void Editor::layoutAdvanced ()
{
    const bool advanced = plainValue (kClarityAdvanced) >= 0.5;
    ThresholdSlider::layout (color, thresholdSliders,
                             CRect (kColorLeft, kColorTop, kColorLeft + kColorViewWidth, kColorTop + kColorViewHeight), advanced);
    for (auto* v : advancedViews)
        v->setVisible (advanced);
}

void Editor::updateLooks ()
{
    if (thresholdView)
        thresholdView->setEnabledLook (plainValue (kPreLimit) >= 0.5);
    const bool on = plainValue (kColorOn) >= 0.5;
    for (auto* v : colorViews)
        v->setEnabledLook (on);
    const bool gentlr = plainValue (kClarity) >= 0.5;
    for (int k = 0; k < kGentlrBands; ++k)
    {
        for (auto* v : clarityViews[k])
            v->setEnabledLook (gentlr);
        if (thresholdSliders[k])
            thresholdSliders[k]->setEnabledLook (smacheratrBandParams ().works (this, k));
    }
    if (noOverlapView)
        noOverlapView->setEnabledLook (gentlr);
    if (slopeView)
        slopeView->setEnabledLook (gentlr);
    for (auto* v : advancedViews)
        v->setEnabledLook (gentlr);
    if (advancedViews.size () == 2)
        advancedViews[1]->setEnabledLook (gentlr && plainValue (kClarityDrive) >= 0.5);
}

void Editor::paramChanged (uint32_t id)
{
    pk::EditorBase::paramChanged (id);
    if (shaper)
        shaper->invalid ();
    if (color)
        color->invalid ();
    if (id == kPreLimit || id == kColorOn || id == kClarity || id == kClarityRange || id == kClarity2Range || id == kClaritySubRange ||
        id == kClarityHighRange || id == kClarityDrive)
        updateLooks ();
    if (id == kClarityAdvanced)
        layoutAdvanced ();
}

void Editor::idle ()
{
    if (shaper)
        shaper->idle ();
    if (color)
        color->idle ();
    for (auto* t : thresholdSliders)
        if (t && t->isVisible ())
            t->idle ();
    if (status)
        if (auto* s = ctl->getShared ())
        {
            char buf[96];
            const int f = oversamplingFactor (plainValue (kOversampling));
            std::snprintf (buf, sizeof (buf), "%s%s, latency %d samples", f == 4 ? "4x oversampling" : f == 2 ? "2x oversampling" : "Oversampling off",
                           plainValue (kMidSide) >= 0.5 ? ", Mid/Side" : "", std::max (0, s->meters.latency.load ()));
            status->setText (buf);
        }
}

void Editor::showMenu (CPoint where)
{
    if (!frame)
        return;
    auto menu = makeOwned<COptionMenu> ();
    std::vector<double> sizes {0.75, 1.0, 1.25, 1.5, 2.0};
    for (double s : sizes)
    {
        char buf[32];
        std::snprintf (buf, sizeof (buf), "Interface Size %d%%", (int)std::lround (s * 100));
        menu->addEntry (buf, -1, std::fabs (currentScale () - s) < 0.01 ? CMenuItem::kChecked : CMenuItem::kNoFlags);
    }
    menu->addSeparator ();
    // Oversampling: one entry per setting, the one in use checked
    const int osNow = std::clamp ((int)std::lround (plainValue (kOversampling)), 0, kNumOsModes - 1);
    static const char* const osNames[kNumOsModes] = {"Oversampling Off", "Oversampling 2x", "Oversampling 4x"};
    for (int k = 0; k < kNumOsModes; ++k)
        menu->addEntry (osNames[k], -1, k == osNow ? CMenuItem::kChecked : CMenuItem::kNoFlags);
    menu->addEntry ("Pre-DC Filter", -1, plainValue (kDcFilter) >= 0.5 ? CMenuItem::kChecked : CMenuItem::kNoFlags);
    menu->addEntry ("Mid/Side (saturate mid and side apart)", -1,
                    plainValue (kMidSide) >= 0.5 ? CMenuItem::kChecked : CMenuItem::kNoFlags);
    const int settingsAt = pk::addSettingsMenuEntries (menu);
    addLayoutMenu (menu);
    menu->popup (frame, where, [this, sizes, menu, settingsAt] (COptionMenu* m) {
        const int32_t r = m->getLastResult ();
        if (pickedInSubMenu (m)) // (Layout: its entries act by themselves)
            return;
        if (settingsMenuPicked (r, settingsAt))
            return;
        if (r >= 0 && r < (int32_t)sizes.size ())
            resizeTo (sizes[(size_t)r]);
        else if (r > (int32_t)sizes.size () && r <= (int32_t)sizes.size () + kNumOsModes)
            ctl->setPlainFromUI (kOversampling, r - (int32_t)sizes.size () - 1);
        else if (r == (int32_t)sizes.size () + kNumOsModes + 1)
            ctl->setPlainFromUI (kDcFilter, plainValue (kDcFilter) >= 0.5 ? 0.0 : 1.0);
        else if (r == (int32_t)sizes.size () + kNumOsModes + 2)
            ctl->setPlainFromUI (kMidSide, plainValue (kMidSide) >= 0.5 ? 0.0 : 1.0);
    });
}

pk::layout::Spec Editor::layoutSpec (bool) const
{
    pk::layout::Spec s;
    // the Analog curve with its controls, the colour display with its own, Gentlr: one row
    s.panels = {
        {"analog", "analog", {8, 40, 312, 428}, 0},
        {"color", "color", {316, 40, 752, 428}, 0},
        {"gentlr", "", {8, kGentlrTop, 752, kGentlrTop + 80}, 0},
    };
    return s;
}

} // namespace smacheratr
