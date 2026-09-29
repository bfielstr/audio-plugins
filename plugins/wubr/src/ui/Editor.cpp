#include "Editor.h"

#include "BandView.h"
#include "Help.h"
#include "ShapeView.h"
#include "plugin/Controller.h"

#include "pluginkit/ui/Theme.h"
#include "pluginkit/vst/PresetBar.h"

#include "vstgui/lib/cframe.h"
#include "vstgui/lib/controls/coptionmenu.h"

#include <cmath>
#include <cstdio>

namespace wubr {

using namespace VSTGUI;
using pk::ActionButton;
using pk::Choice;
using pk::Knob;
using pk::Label;
using pk::NumberBox;
using pk::Panel;
using pk::Segmented;
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
    tailDisplays.reset ();
    bands = nullptr;
    for (auto& s : shapes)
        s = nullptr;
    for (auto& v : bandViews)
        v.clear ();
    bandButtons.clear ();
    for (int b = 0; b < kBands; ++b)
        rateModeViews[b] = syncViews[b] = hzViews[b] = nullptr;
    envViews.clear ();
    sensView = nullptr;
}

void Editor::buildUI (CFrame* f)
{
    auto* root = new Background (CRect (0, 0, kWidth, kHeight));
    f->addView (root);
    root->addView (new Label (CRect (12, 6, 200, 28), "wubr", 14.0, true));
    root->addView (new pk::PresetBar (CRect (580, 6, 776, 28), ctl));
    auto* helpBtn = new ActionButton (CRect (784, 6, 806, 28), "?", [this] { setTooltipsEnabled (!tooltipsEnabled ()); },
                                      [this] { return tooltipsEnabled (); });
    helpBtn->setTooltipText ("Show or hide these help tooltips.");
    root->addView (helpBtn);
    root->addView (new ActionButton (CRect (812, 6, 892, 28), "Menu", [this] { showMenu (CPoint (812, 28)); }));

    auto metersOf = [c = ctl] () -> const Meters* {
        auto* s = c->getShared ();
        return s ? &s->meters : nullptr;
    };
    bands = new BandView (CRect (8, kBandTop, 892, kBandBottom), this, metersOf);
    bands->setTooltipText (help::kBandDisplay);
    bands->onBandPicked = [this] (int b) { showBand (b); };
    root->addView (bands);

    // the row: the band selector and the selected band's On and Target; the mode, for both bands
    bandButtons.clear ();
    for (int b = 0; b < kBands; ++b)
    {
        auto* bt = new ActionButton (CRect (8 + b * 72, kRowTop, 76 + b * 72, kRowTop + 22), b == 0 ? "Band 1" : "Band 2",
                                     [this, b] { showBand (b); }, [this, b] { return shown == b; });
        bt->setTooltipText (b == 0 ? "Show band 1 (green)." : "Show band 2 (blue).");
        root->addView (bt);
        bandButtons.push_back (bt);
    }
    root->addView (new Label (CRect (430, kRowTop + 2, 470, kRowTop + 20), "Mode", 10.5, false, 2));
    bind (root, new Segmented (CRect (474, kRowTop, 604, kRowTop + 22), this, kMode, {"LFO", "Envelope"}));
    envViews.push_back (bind (root, new Segmented (CRect (612, kRowTop, 730, kRowTop + 22), this, kTrigger, {"MIDI", "Transient"})));
    sensView = bind (root, new NumberBox (CRect (736, kRowTop + 2, 790, kRowTop + 20), this, kSensitivity));

    // per band: its On, Target, Hold, shape and knobs (both bands' are made; the selected one is shown)
    for (int b = 0; b < kBands; ++b)
    {
        auto& views = bandViews[b];
        views.clear ();
        auto add = [&] (CView* v) {
            views.push_back (v);
            return v;
        };
        add (bind (root, new Toggle (CRect (160, kRowTop, 206, kRowTop + 22), this, bandParam (b, kBandOn), "On")));
        add (bind (root, new Segmented (CRect (212, kRowTop, 420, kRowTop + 22), this, bandParam (b, kTarget), {"Gain", "Frequency", "Both"})));
        auto* holdLabel = new Label (CRect (796, kRowTop + 2, 836, kRowTop + 20), "Hold", 10.5, false, 2);
        root->addView (holdLabel);
        add (holdLabel);
        auto* hold = bind (root, new NumberBox (CRect (840, kRowTop + 2, 892, kRowTop + 20), this, bandParam (b, kHold)));
        envViews.push_back (hold);
        add (hold);
        // both bands' shapes, always shown: band 1 above band 2
        const double half = (kShapeBottom - kShapeTop - 4.0) / 2.0;
        const double top = kShapeTop + b * (half + 4.0);
        shapes[b] = new ShapeView (CRect (kShapeLeft, top, kShapeRight, top + half), this, b, metersOf);
        shapes[b]->setTooltipText (help::kShapeDisplay);
        root->addView (shapes[b]);
        // knobs: the band, then its rate
        const uint32_t ids[5] = {kFreq, wubr::kWidth, kGain, kDepth, kSweep}; // (kWidth alone is the window width here)
        for (int i = 0; i < 5; ++i)
            add (bind (root, new Knob (knobRect (604 + i * 57, kShapeTop), this, bandParam (b, ids[i]), nullptr, i == 2 || i == 3)));
        // the rate: shown for the band whose rate runs (band 1's while linked; see updateLooks)
        rateModeViews[b] = bind (root, new Segmented (CRect (604, kShapeTop + 80, 700, kShapeTop + 100), this, bandParam (b, kRateMode), {"Sync", "Free"}));
        syncViews[b] = bind (root, new Choice (CRect (604, kShapeTop + 108, 700, kShapeTop + 128), this, bandParam (b, kSync)));
        hzViews[b] = bind (root, new NumberBox (CRect (604, kShapeTop + 108, 700, kShapeTop + 128), this, bandParam (b, kRateHz)));
        add (bind (root, new Knob (knobRect (718, kShapeTop + 80), this, bandParam (b, kPhase))));
    }
    bind (root, new Toggle (CRect (604, kShapeTop + 136, 700, kShapeTop + 156), this, kLinkRate, "Link Rates"));
    bind (root, new Knob (knobRect (775, kShapeTop + 80), this, kDryWet));
    bind (root, new Knob (knobRect (832, kShapeTop + 80), this, kOutput));

    // the saturator at the end of the chain, with Smacheratr's displays above its controls
    auto* tailPanel = addTailPanel (root, CRect (8, 448, 892, 526 + smacheratr::TailDisplays::kHeight), kTailBase, kTailExtBase);
    tailDisplays = std::make_unique<smacheratr::TailDisplays> (
        this, kTailBase, kTailExtBase,
        [c = ctl] {
            auto* s = c->getShared ();
            return s ? s->sampleRate.load () : 48000.0;
        },
        [c = ctl] () -> const smacheratr::Meters* {
            auto* s = c->getShared ();
            return s ? &s->tailMeters : nullptr;
        });
    tailDisplays->add (tailPanel, CRect (10, 24, 874, 24 + smacheratr::TailDisplays::kHeight - 22));
    tailDisplays->onBandPicked ([this] (int k) { showTailBand (k); });

    applyParamTooltips (&help::forParam);
    showBand (shown);
    idle ();
}

void Editor::showBand (int band)
{
    shown = band == 1 ? 1 : 0;
    for (int b = 0; b < kBands; ++b)
        for (auto* v : bandViews[b])
            v->setVisible (b == shown);
    for (auto* v : bandButtons)
        v->invalid ();
    if (bands)
    {
        bands->selected = shown;
        bands->invalid ();
    }
    updateLooks ();
}

void Editor::updateLooks ()
{
    // the rate controls of the band whose rate runs: band 1's while linked
    const int rb = plainValue (kLinkRate) >= 0.5 ? 0 : shown;
    for (int b = 0; b < kBands; ++b)
        if (rateModeViews[b] && syncViews[b] && hzViews[b])
        {
            const bool free = std::lround (plainValue (bandParam (b, kRateMode))) == kFree;
            rateModeViews[b]->setVisible (b == rb);
            syncViews[b]->setVisible (b == rb && !free);
            hzViews[b]->setVisible (b == rb && free);
        }
    const bool envelope = std::lround (plainValue (kMode)) == kEnvelope;
    for (auto* v : envViews)
        v->setEnabledLook (envelope);
    if (sensView)
        sensView->setEnabledLook (envelope && std::lround (plainValue (kTrigger)) == kTransient);
}

void Editor::paramChanged (uint32_t id)
{
    pk::EditorBase::paramChanged (id);
    if (tailDisplays)
        tailDisplays->paramChanged (id);
    if (bands)
        bands->invalid ();
    for (auto* s : shapes)
        if (s)
            s->invalid ();
    if (id == kMode || id == kTrigger || id == kLinkRate ||
        (id >= kBandBase && id < kTailExtBase && (id - kBandBase) % kBandBlock == kRateMode))
        updateLooks ();
}

void Editor::idle ()
{
    if (tailDisplays)
        tailDisplays->idle ();
    if (bands)
        bands->idle ();
    for (auto* s : shapes)
        if (s)
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
    menu->popup (frame, where, [this, sizes, menu] (COptionMenu* m) {
        const int32_t r = m->getLastResult ();
        if (r >= 0 && r < (int32_t)sizes.size ())
            resizeTo (sizes[(size_t)r]);
    });
}

} // namespace wubr
