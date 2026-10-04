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
    for (auto& v : clarityViews)
        v.clear ();
    clarityBandButtons.clear ();
    for (auto& t : thresholdSliders)
        t = nullptr;
    advancedViews.clear ();
    noOverlapView = nullptr;
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
    helpBtn->setTooltipText ("Show or hide these help tooltips.");
    root->addView (helpBtn);
    root->addView (new ActionButton (CRect (672, 6, 752, 28), "Menu", [this] { showMenu (CPoint (672, 28)); }));

    // left: the device as Live shows it, with the pre-limiter in front of the curve
    bind (root, new Toggle (CRect (kShaperLeft, 40, kShaperLeft + 76, 62), this, kPreLimit, "Pre-Limit"));
    thresholdView = bind (root, new NumberBox (CRect (kShaperLeft + 80, 42, kShaperLeft + 140, 60), this, kPreLimitThreshold));
    shaper = new ShaperView (CRect (kShaperLeft, kShaperTop, kShaperLeft + kShaperWidth, kShaperTop + kShaperHeight), this,
                             [c = ctl] () -> const Meters* {
                                 auto* s = c->getShared ();
                                 return s ? &s->meters : nullptr;
                             });
    shaper->setTooltipText (help::kShaperDisplay);
    root->addView (shaper);
    bind (root, new Choice (CRect (8, 266, 112, 288), this, kPostClip));
    bind (root, new Toggle (CRect (120, 266, 176, 288), this, kColorOn, "Color"));
    colorViews.push_back (bind (root, new Knob (knobRect (184, 262), this, kColorLo)));
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
    color->setTooltipText (help::kColorDisplay);
    root->addView (color);
    const uint32_t colorIds[3] = {kColorHi, kColorFreq, kColorWidth};
    for (int i = 0; i < 3; ++i)
        colorViews.push_back (bind (root, new Knob (knobRect (kColorLeft + 60 + i * 130, 346), this, colorIds[i])));

    // Gentlr's Threshold sliders (Advanced), at the right edge of the colour display while Advanced is on
    for (int k = 0; k < kGentlrBands; ++k)
        thresholdSliders[k] = bind (root, new ThresholdSlider (CRect (0, 0, 1, 1), this, k, [c = ctl] () -> const Meters* {
                                        auto* s = c->getShared ();
                                        return s ? &s->meters : nullptr;
                                    }));

    // bottom: Gentlr (one button), a band selector and the selected band's Frequency, Width and Range;
    // Advanced, and with it the region Drive; No Overlap
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
        auto* bt = new ActionButton (CRect (92 + k * 46, 30, 136 + k * 46, 50), names[k], [this, k] { showClarityBand (k); },
                                     [this, k] { return clarityBand == k; });
        bt->setTooltipText (tips[k]);
        cp->addView (bt);
        clarityBandButtons.push_back (bt);
        if (!hasWidth (k))
        {
            // the Sub and High bands: Freq and Range where the other bands have theirs (no width, no button: a
            // band works while its Range is above 0 dB)
            clarityViews[k].push_back (bind (cp, new Knob (knobRect (284, 10), this, kGentlrFreqIds[k], "Freq")));
            clarityViews[k].push_back (bind (cp, new Knob (knobRect (400, 10), this, kGentlrRangeIds[k], "Range")));
            continue;
        }
        clarityViews[k].push_back (bind (cp, new Knob (knobRect (284, 10), this, kClarityFreqIds[k], "Freq")));
        clarityViews[k].push_back (bind (cp, new Knob (knobRect (342, 10), this, kClarityWidthIds[k], "Width")));
        clarityViews[k].push_back (bind (cp, new Knob (knobRect (400, 10), this, kClarityRangeIds[k], "Range")));
    }
    color->onBandPicked = [this] (int k) { showClarityBand (k); };
    showClarityBand (clarityBand);
    bind (cp, new Toggle (CRect (kGentlrAdvancedX - 8 - 42, 30, kGentlrAdvancedX - 8 + 42, 50), this, kClarityAdvanced, "Advanced"));
    advancedViews.push_back (bind (cp, new Toggle (CRect (556, 30, 606, 50), this, kClarityDrive, "Drive")));
    advancedViews.push_back (bind (cp, new Knob (knobRect (610, 10), this, kClarityDriveAmount, "Amount")));
    noOverlapView = bind (cp, new NoOverlapToggle (CRect (kNoOverlapX - 8 - 34, 30, kNoOverlapX - 8 + 34, 50), this, smacheratrBandParams ()));
    layoutAdvanced ();

    applyParamTooltips (&help::forParam);
    updateLooks ();
    idle ();
}

void Editor::showClarityBand (int band)
{
    clarityBand = band < 0 ? 0 : band >= kGentlrBands ? kGentlrBands - 1 : band;
    for (int k = 0; k < kGentlrBands; ++k)
        for (auto* v : clarityViews[k])
            v->setVisible (k == clarityBand);
    for (auto* b : clarityBandButtons)
        b->invalid ();
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
            std::snprintf (buf, sizeof (buf), "%s%s, latency %d samples",
                           plainValue (kHiQuality) >= 0.5 ? "Hi-Quality: 4x oversampling" : "Hi-Quality off",
                           plainValue (kMidSide) >= 0.5 ? ", Mid/Side" : "", s->latency.load ());
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
    menu->addEntry ("Hi-Quality (4x oversampling)", -1, plainValue (kHiQuality) >= 0.5 ? CMenuItem::kChecked : CMenuItem::kNoFlags);
    menu->addEntry ("Pre-DC Filter", -1, plainValue (kDcFilter) >= 0.5 ? CMenuItem::kChecked : CMenuItem::kNoFlags);
    menu->addEntry ("Mid/Side (saturate mid and side apart)", -1,
                    plainValue (kMidSide) >= 0.5 ? CMenuItem::kChecked : CMenuItem::kNoFlags);
    const int settingsAt = pk::addSettingsMenuEntries (menu);
    menu->popup (frame, where, [this, sizes, menu, settingsAt] (COptionMenu* m) {
        const int32_t r = m->getLastResult ();
        if (settingsMenuPicked (r, settingsAt))
            return;
        if (r >= 0 && r < (int32_t)sizes.size ())
            resizeTo (sizes[(size_t)r]);
        else if (r == (int32_t)sizes.size () + 1)
            ctl->setPlainFromUI (kHiQuality, plainValue (kHiQuality) >= 0.5 ? 0.0 : 1.0);
        else if (r == (int32_t)sizes.size () + 2)
            ctl->setPlainFromUI (kDcFilter, plainValue (kDcFilter) >= 0.5 ? 0.0 : 1.0);
        else if (r == (int32_t)sizes.size () + 3)
            ctl->setPlainFromUI (kMidSide, plainValue (kMidSide) >= 0.5 ? 0.0 : 1.0);
    });
}

} // namespace smacheratr
