#include "Editor.h"

#include "GentlrView.h"
#include "Help.h"
#include "plugin/Controller.h"

#include "pluginkit/vst/Clipboard.h"
#include "pluginkit/ui/Theme.h"
#include "pluginkit/vst/PresetBar.h"

#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cframe.h"
#include "vstgui/lib/cgraphicspath.h"
#include "vstgui/lib/controls/coptionmenu.h"

#include <cmath>
#include <cstdio>
#include <functional>
#include <string>

namespace gentlr {

using namespace VSTGUI;
using pk::ActionButton;
using pk::Choice;
using pk::Knob;
using pk::Label;
using pk::Toggle;

namespace {
constexpr double kKnobW = 56, kKnobH = 64;
CRect knobRect (double x, double y) { return CRect (x, y, x + kKnobW, y + kKnobH); }

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

std::string hzText (double hz)
{
    char buf[24];
    if (hz < 999.5)
        std::snprintf (buf, sizeof (buf), "%.0f Hz", hz);
    else
        std::snprintf (buf, sizeof (buf), "%.1f kHz", hz / 1000.0);
    return buf;
}

// A band's name and the edges of its region now (the Sub and High bands: where they start to taper).
class BandHeader : public CView
{
public:
    BandHeader (const CRect& r, pk::ParamHost* h, int b, std::function<double ()> rate)
        : CView (r), host (h), band (b), sampleRate (std::move (rate))
    {
    }
    void draw (CDrawContext* ctx) override
    {
        const CRect r = getViewSize ();
        const bool on = bandWorks (band, host->plainValue (onParam (band)), host->plainValue (rangeParam (band)));
        // a header as linework: a ticked dim rule under it, and a lamp at the left, lit while the band
        // works (idle otherwise); the name in pale copper (text dim while it does not work)
        pk::draw::tickRule (ctx, r.left, r.right, r.bottom - 1, pk::theme::kLineDim, 8);
        ctx->setFillColor (on ? pk::theme::kEnergyLive : pk::theme::kEnergyIdle);
        ctx->drawRect (CRect (r.left, r.top + 4, r.left + 2, r.bottom - 5), kDrawFilled);
        char name[16];
        std::snprintf (name, sizeof (name), band == kSub ? "SUB" : band == kHigh ? "HIGH" : "BAND %d", band + 1);
        ctx->setFont (pk::theme::font (10.5, true));
        ctx->setFontColor (on ? pk::theme::kCopperPale : pk::theme::kTextDim);
        ctx->drawString (name, CRect (r.left + 9, r.top, r.right, r.bottom), kLeftText, true);
        ctx->setFont (pk::theme::font (9.5));
        ctx->setFontColor (on ? pk::theme::kText : pk::theme::kTextDim);
        std::string edges;
        if (band == kSub)
            edges = "20 - " + hzText (host->plainValue (kSubFreq));
        else if (band == kHigh)
            edges = hzText (host->plainValue (kHighFreq)) + " - 20 k";
        else
        {
            const smacheratr::ClarityBand b = smacheratr::clarityBand (sampleRate (), host->plainValue (bandParam (band, kFreq)),
                                                                       host->plainValue (bandParam (band, gentlr::kWidth)));
            edges = hzText (b.lowHz) + " - " + hzText (b.highHz);
        }
        ctx->drawString (edges.c_str (), CRect (r.left, r.top, r.right - 6, r.bottom), kRightText, true);
    }

private:
    pk::ParamHost* host;
    int band;
    std::function<double ()> sampleRate;
};
} // namespace

Editor::Editor (Controller* c) : pk::EditorBase (c, kWidth, kHeight), ctl (c) {}

void Editor::onClose ()
{
    if (tailDisplays)
        tailDisplays->closed ();
    tailDisplays.reset ();
    view = nullptr;
    for (auto& s : sliders)
        s = nullptr;
    headers.clear ();
    for (auto& v : bandViews)
        v.clear ();
    advancedViews.clear ();
    driveAmount = nullptr;
}

void Editor::buildUI (CFrame* f)
{
    auto* root = new Background (CRect (0, 0, kWidth, kHeight));
    f->addView (root);
    root->addView (new Label (CRect (12, 6, 200, 28), "gentlr", 14.0, true));
    root->addView (new pk::PresetBar (CRect (840, 6, 1036, 28), ctl));
    auto* helpBtn = new ActionButton (CRect (1044, 6, 1066, 28), "?", [this] { setTooltipsEnabled (!tooltipsEnabled ()); },
                                      [this] { return tooltipsEnabled (); });
    helpBtn->setTooltipText ("Show or hide these help tooltips.");
    root->addView (helpBtn);
    root->addView (new ActionButton (CRect (1072, 6, 1152, 28), "Menu", [this] { showMenu (CPoint (1072, 28)); }));
    // the bands' Slope (bands 1 and 2, both at once), in the header, left of the presets (over the
    // detector's column)
    root->addView (new Label (CRect (kSlopeLeft - 72, 6, kSlopeLeft - 6, 28), "Band Slope", 10.5));
    bind (root, new Choice (CRect (kSlopeLeft, 7, kSlopeLeft + 108, 27), this, kSlope));

    auto metersOf = [c = ctl] () -> const Meters* {
        auto* s = c->getShared ();
        return s ? &s->meters : nullptr;
    };
    auto rateOf = [c = ctl] {
        auto* s = c->getShared ();
        return s ? s->sampleRate.load () : 48000.0;
    };
    view = new GentlrView (CRect (kViewLeft, kViewTop, kViewRight, kViewBottom), this, metersOf);
    view->setTooltipText (help::kDisplay);
    root->addView (view);

    // the Threshold sliders are Smacheratr's, on Gentlr's Thresholds (the same range) and levels
    sliderHost = std::make_unique<pk::MappedParamHost> (this, smacheratr::paramTable (), [] (uint32_t id) { return fromSmacheratr (id); });
    for (int k = 0; k < kAllBands; ++k)
    {
        sliders[k] = new smacheratr::ThresholdSlider (CRect (0, 0, 1, 1), sliderHost.get (), k, [c = ctl] () -> const smacheratr::Meters* {
            auto* s = c->getShared ();
            return s ? &s->meters.bands : nullptr;
        });
        sliders[k]->setTooltipText (k == kSub ? help::kSubThresholdSlider : k == kHigh ? help::kHighThresholdSlider : help::kThresholdSlider);
        root->addView (sliders[k]); // (not bound: its ID is Smacheratr's; paramChanged repaints it)
    }

    // each band: its name and edges, On, Frequency, Width, Range
    headers.clear ();
    const char* knobLabels[3] = {"Freq", "Width", "Range"};
    const uint32_t knobFields[3] = {gentlr::kFreq, gentlr::kWidth, gentlr::kRange}; // (Editor::kWidth is the window's)
    for (int k = 0; k < kBands; ++k)
    {
        const double x = kViewLeft + k * kBandW;
        auto* h = new BandHeader (CRect (x, kRowTop, x + 132, kRowTop + 18), this, k, rateOf);
        root->addView (h);
        headers.push_back (h);
        bind (root, new Toggle (CRect (x + 138, kRowTop - 1, x + kBandW - 10, kRowTop + 19), this, bandParam (k, kOn), "On"));
        for (int i = 0; i < 3; ++i)
            bandViews[k].push_back (bind (root, new Knob (knobRect (x + i * 64, kRowTop + 24), this, bandParam (k, knobFields[i]), knobLabels[i])));
    }
    // the Sub and High bands: their name and where they taper, Frequency, Range (no width, and no On: a band
    // works while its Range is above 0 dB)
    for (int k : {kSub, kHigh})
    {
        const double x = k == kSub ? kSubLeft : kHighLeft;
        auto* h = new BandHeader (CRect (x, kRowTop, x + kSubW - 8, kRowTop + 18), this, k, rateOf);
        root->addView (h);
        headers.push_back (h);
        bandViews[k].push_back (bind (root, new Knob (knobRect (x, kRowTop + 24), this, freqParam (k), "Freq")));
        bandViews[k].push_back (bind (root, new Knob (knobRect (x + 64, kRowTop + 24), this, rangeParam (k), "Range")));
    }

    // the detector: stereo mode, Attack, Release
    const double dx = kHighLeft + kSubW + 8; // 724
    bind (root, new Choice (CRect (dx, kRowTop - 1, dx + 120, kRowTop + 19), this, kStereo));
    bind (root, new Knob (knobRect (dx, kRowTop + 24), this, kAttack));
    bind (root, new Knob (knobRect (dx + 64, kRowTop + 24), this, kRelease));

    // Advanced, and with it the region Drive
    const double ax = dx + 146; // 870
    bind (root, new Toggle (CRect (ax, kRowTop - 1, ax + 120, kRowTop + 19), this, kAdvanced, "Advanced"));
    advancedViews.push_back (bind (root, new Toggle (CRect (ax, kRowTop + 40, ax + 56, kRowTop + 60), this, kDrive, "Drive")));
    driveAmount = bind (root, new Knob (knobRect (ax + 64, kRowTop + 24), this, kDriveAmount, "Amount"));
    advancedViews.push_back (driveAmount);

    // No Overlap (the bands push each other), Mix and Output
    bind (root, new smacheratr::NoOverlapToggle (CRect (kNoOverlapLeft, kRowTop - 1, kViewRight - 4, kRowTop + 19), this, GentlrView::bandParams ()));
    bind (root, new Knob (knobRect (kViewRight - 2 * kKnobW - 12, kRowTop + 24), this, kMix));
    bind (root, new Knob (knobRect (kViewRight - kKnobW - 4, kRowTop + 24), this, kOutput));

    // the saturator at the end of the chain, with Smacheratr's displays above its controls
    auto* tailPanel = addTailPanel (root, CRect (kViewLeft, kTailTop, kViewRight, kTailTop + 78 + smacheratr::TailDisplays::kHeight), kTailBase,
                                    kTailExtBase, kTailExt2Base, kTailExt3Base);
    tailDisplays = std::make_unique<smacheratr::TailDisplays> (this, smacheratr::TailBases {kTailBase, kTailExtBase, kTailExt2Base, kTailExt3Base, kTailExt4Base}, rateOf,
                                                               [c = ctl] () -> const smacheratr::Meters* {
                                                                   auto* s = c->getShared ();
                                                                   return s ? &s->tailMeters : nullptr;
                                                               });
    tailDisplays->add (tailPanel, CRect (10, 24, kViewRight - kViewLeft - 10, 24 + smacheratr::TailDisplays::kHeight - 22));
    tailDisplays->onBandPicked ([this] (int k) { showTailBand (k); });

    applyParamTooltips (&help::forParam);
    layoutAdvanced ();
    updateLooks ();
    idle ();
}

void Editor::layoutAdvanced ()
{
    const bool advanced = plainValue (kAdvanced) >= 0.5;
    const CRect area (kViewLeft, kViewTop, kViewRight, kViewBottom);
    // the sliders take a strip at the right of the display (the display itself is Gentlr's, so it is
    // sized here rather than by ThresholdSlider::layout)
    smacheratr::ThresholdSlider::layout (nullptr, sliders, area, advanced);
    if (view)
    {
        CRect r = area;
        if (advanced)
            r.right -= smacheratr::ThresholdSlider::kStripWidth;
        view->setViewSize (r);
        view->setMouseableArea (r);
        view->invalid ();
    }
    for (auto* v : advancedViews)
        v->setVisible (advanced);
}

void Editor::updateLooks ()
{
    for (int k = 0; k < kAllBands; ++k)
    {
        const bool works = bandWorks (k, plainValue (onParam (k)), plainValue (rangeParam (k)));
        for (auto* v : bandViews[k])
            v->setEnabledLook (!hasOn (k) || plainValue (onParam (k)) >= 0.5); // (Sub, High: always there to turn up)
        if (sliders[k])
            sliders[k]->setEnabledLook (works);
    }
    if (driveAmount)
        driveAmount->setEnabledLook (plainValue (kDrive) >= 0.5);
}

void Editor::paramChanged (uint32_t id)
{
    pk::EditorBase::paramChanged (id);
    if (tailDisplays)
        tailDisplays->paramChanged (id);
    if (isTailParam (id))
        return;
    if (view)
        view->invalid ();
    if (id == kAdvanced)
        layoutAdvanced ();
    if (isBandParam (id))
    {
        for (auto* h : headers)
            h->invalid ();
        for (auto* s : sliders)
            if (s)
                s->invalid ();
    }
    if (isBandParam (id) || id == kDrive)
        updateLooks ();
}

void Editor::idle ()
{
    if (tailDisplays)
        tailDisplays->idle ();
    if (view)
        view->idle ();
    for (auto* s : sliders)
        if (s && s->isVisible ())
            s->idle ();
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
    const int settingsAt = pk::addSettingsMenuEntries (menu);
    menu->popup (frame, where, [this, sizes, menu, settingsAt] (COptionMenu* m) {
        const int32_t r = m->getLastResult ();
        if (settingsMenuPicked (r, settingsAt))
            return;
        if (r >= 0 && r < (int32_t)sizes.size ())
            resizeTo (sizes[(size_t)r]);
    });
}

} // namespace gentlr
