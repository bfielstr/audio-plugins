#include "Editor.h"

#include "BandView.h"
#include "Help.h"
#include "plugin/Controller.h"

#include "pluginkit/vst/Clipboard.h"
#include "pluginkit/ui/Theme.h"
#include "pluginkit/vst/PresetBar.h"

#include "vstgui/lib/cframe.h"
#include "vstgui/lib/controls/coptionmenu.h"

#include <cmath>
#include <cstdio>
#include <vector>

namespace dropr {

using namespace VSTGUI;
using pk::ActionButton;
using pk::Knob;
using pk::Label;
using pk::Panel;

namespace {
class Background : public CViewContainer
{
public:
    using CViewContainer::CViewContainer;
    void drawBackgroundRect (CDrawContext* ctx, const CRect&) override
    {
        ctx->setFillColor (pk::theme::kBackground);
        ctx->drawRect (CRect (0, 0, getViewSize ().getWidth (), getViewSize ().getHeight ()), kDrawFilled);
        ctx->setFillColor (pk::theme::kHeader);
        ctx->drawRect (CRect (0, 0, getViewSize ().getWidth (), 34), kDrawFilled);
    }
};
} // namespace

Editor::Editor (Controller* c) : pk::EditorBase (c, kWidth, kHeight), ctl (c) {}

void Editor::onClose ()
{
    display = nullptr;
    ratioKnob = negRatioKnob = rangeKnob = nullptr;
    latencyLabel = nullptr;
    tailDisplays.reset ();
}

void Editor::buildUI (CFrame* f)
{
    auto* root = new Background (CRect (0, 0, kWidth, kHeight));
    f->addView (root);
    root->addView (new Label (CRect (12, 6, 200, 28), "dropr", 14.0, true));
    latencyLabel = new Label (CRect (200, 6, 430, 28), "", 10.5);
    root->addView (new pk::PresetBar (CRect (440, 6, 636, 28), ctl));
    latencyLabel->setDim (true);
    root->addView (latencyLabel);
    auto* helpBtn = new ActionButton (CRect (644, 6, 666, 28), "?", [this] { setTooltipsEnabled (!tooltipsEnabled ()); },
                                      [this] { return tooltipsEnabled (); });
    helpBtn->setTooltipText ("Show or hide these help tooltips.");
    root->addView (helpBtn);
    root->addView (new ActionButton (CRect (672, 6, 752, 28), "Menu", [this] { showMenu (CPoint (672, 28)); }));

    display = new BandView (CRect (kDisplayLeft, kDisplayTop, kDisplayRight, kDisplayBottom), this,
                            [c = ctl] () -> const Meters* { auto* s = c->getShared (); return s ? &s->meters : nullptr; });
    display->setTooltipText (help::kDisplay);
    root->addView (display);

    // the right column: how many bands, stereo or mid-side, how far the channels are linked
    auto* side = new Panel (CRect (kSideLeft, kSideTop, 752, kDisplayBottom), "CHANNELS");
    root->addView (side);
    side->addView (new Label (CRect (8, 20, 116, 34), "Bands", 10.5));
    bind (side, new pk::Segmented (CRect (kBandsSelLeft, kBandsSelTop, kBandsSelLeft + kBandsSelW, kBandsSelTop + 20), this, kBands, {"1", "2", "3", "4", "5", "6"}));
    side->addView (new Label (CRect (8, 62, 116, 76), "Mode", 10.5));
    bind (side, new pk::Segmented (CRect (8, 76, 116, 96), this, kMode, {"Stereo", "M/S"}));
    bind (side, new Knob (CRect (34, 108, 90, 172), this, kLink));

    // the dynamics
    auto* dyn = new Panel (CRect (kDynLeft, kDynTop, 752, 420), "DYNAMICS");
    root->addView (dyn);
    auto col = [] (int i) { return kDynFirst + kDynColumn * i; };
    auto knobAt = [&] (int i, uint32_t id, bool bipolar = false) {
        return bind (dyn, new Knob (CRect (col (i), 22, col (i) + 56, 86), this, id, nullptr, bipolar));
    };
    knobAt (0, kAdaptive);
    knobAt (1, kAttack);
    knobAt (2, kRelease);
    knobAt (3, kDownThreshold);
    ratioKnob = knobAt (4, kDownRatio);
    negRatioKnob = knobAt (4, kNegRatio);
    bind (dyn, new pk::Toggle (CRect (col (4), kNegToggleTop, col (4) + 56, kNegToggleTop + 16), this, kNegative, "Negative"));
    rangeKnob = knobAt (5, kRange);
    knobAt (6, kUpThreshold);
    knobAt (7, kUpRatio);
    knobAt (8, kKnee);
    knobAt (9, kTilt, true);
    knobAt (10, kMakeup);

    // levels: in, dry / wet, out
    auto* levels = new Panel (CRect (8, 428, 752, 516), "LEVELS");
    root->addView (levels);
    bind (levels, new Knob (CRect (24, 20, 80, 84), this, kInput));
    bind (levels, new Knob (CRect (104, 20, 160, 84), this, kMix));
    bind (levels, new Knob (CRect (184, 20, 240, 84), this, kOutput, nullptr, true));
    levels->addView (new Label (CRect (270, 30, 736, 46),
                                "Dry: the input before the Input gain.  Wet: the bands after their gains.", 10.0));
    showRatio ();

    // the saturator at the end of the chain, with Smacheratr's displays above its controls
    auto* tailPanel = addTailPanel (root, CRect (8, 524, 752, 598 + smacheratr::TailDisplays::kHeight), kTailBase, kTailExtBase, kTailExt2Base);
    tailDisplays = std::make_unique<smacheratr::TailDisplays> (this, kTailBase, kTailExtBase, kTailExt2Base,
                                                               [c = ctl] { auto* s = c->getShared (); return s ? s->sampleRate.load () : 48000.0; },
                                                               [c = ctl] () -> const smacheratr::Meters* { auto* s = c->getShared (); return s ? &s->tailMeters : nullptr; });
    tailDisplays->add (tailPanel, CRect (10, 24, 734, 24 + smacheratr::TailDisplays::kHeight - 22));
    tailDisplays->onBandPicked ([this] (int k) { showTailBand (k); });

    applyParamTooltips (&help::forParam);
    idle ();
}

void Editor::paramChanged (uint32_t id)
{
    pk::EditorBase::paramChanged (id);
    if (tailDisplays)
        tailDisplays->paramChanged (id);
    if (display && id < kTailBase)
        display->invalid ();
    if (id == kNegative)
        showRatio ();
}

std::string Editor::valueText (uint32_t id)
{
    if (id == kNegRatio)
        return negRatioText (plainValue (id));
    if (id == kTilt)
    {
        char buf[32];
        std::snprintf (buf, sizeof (buf), "%+.1f dB/oct", plainValue (id));
        return std::fabs (plainValue (id)) < 0.05 ? "0 dB/oct" : buf;
    }
    return pk::EditorBase::valueText (id);
}

void Editor::showRatio ()
{
    const bool neg = plainValue (kNegative) >= 0.5;
    if (ratioKnob)
        ratioKnob->setVisible (!neg);
    if (negRatioKnob)
        negRatioKnob->setVisible (neg);
    if (rangeKnob)
        rangeKnob->setEnabledLook (neg);
}

void Editor::idle ()
{
    if (tailDisplays)
        tailDisplays->idle ();
    if (display)
        display->idle ();
    if (latencyLabel)
        if (auto* s = ctl->getShared ())
        {
            char buf[64];
            std::snprintf (buf, sizeof (buf), "Latency %d samples", s->latency.load ());
            latencyLabel->setText (buf);
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
    const int settingsAt = pk::addSettingsMenuEntries (menu);
    menu->popup (frame, where, [this, sizes, menu, settingsAt] (COptionMenu* m) {
        const int32_t r = m->getLastResult ();
        if (settingsMenuPicked (r, settingsAt))
            return;
        if (r >= 0 && r < (int32_t)sizes.size ())
            resizeTo (sizes[(size_t)r]);
    });
}

} // namespace dropr
