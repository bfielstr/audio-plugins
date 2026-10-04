#include "Editor.h"

#include "GonioView.h"
#include "Help.h"
#include "StageView.h"
#include "plugin/Controller.h"

#include "pluginkit/vst/Clipboard.h"
#include "pluginkit/ui/Theme.h"
#include "pluginkit/vst/PresetBar.h"

#include "vstgui/lib/cframe.h"
#include "vstgui/lib/controls/coptionmenu.h"

#include <cmath>
#include <cstdio>
#include <vector>

namespace widr {

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

CRect knobAt (double x, double y) { return CRect (x, y, x + 56, y + 64); }
} // namespace

Editor::Editor (Controller* c) : pk::EditorBase (c, kWidth, kHeight), ctl (c) {}

void Editor::onClose ()
{
    tailDisplays.reset ();
    stage = nullptr;
    gonio = nullptr;
    statusLabel = nullptr;
}

void Editor::buildUI (CFrame* f)
{
    auto* root = new Background (CRect (0, 0, kWidth, kHeight));
    f->addView (root);
    root->addView (new Label (CRect (12, 6, 200, 28), "widr", 14.0, true));
    statusLabel = new Label (CRect (200, 6, 430, 28), "", 10.5);
    statusLabel->setDim (true);
    root->addView (statusLabel);
    root->addView (new pk::PresetBar (CRect (440, 6, 636, 28), ctl));
    auto* helpBtn = new ActionButton (CRect (644, 6, 666, 28), "?", [this] { setTooltipsEnabled (!tooltipsEnabled ()); },
                                      [this] { return tooltipsEnabled (); });
    helpBtn->setTooltipText ("Show or hide these help tooltips.");
    root->addView (helpBtn);
    root->addView (new ActionButton (CRect (672, 6, 752, 28), "Menu", [this] { showMenu (CPoint (672, 28)); }));

    stage = new StageView (CRect (kStageLeft, kStageTop, kStageRight, kStageBottom), this, ctl);
    stage->setTooltipText (help::kStage);
    root->addView (stage);
    gonio = new GonioView (CRect (568, 40, 752, 300), [c = ctl] () -> Meters* {
        auto* s = c->getShared ();
        return s ? &s->meters : nullptr;
    });
    gonio->setTooltipText (help::kGonio);
    root->addView (gonio);

    auto* wp = new Panel (CRect (8, 308, 752, 432), "WIDTH");
    root->addView (wp);
    wp->addView (new Label (CRect (14, 24, 254, 38), "Character", 10.5, false, 1));
    bind (wp, new Segmented (CRect (14, 42, 254, 66), this, kCharacter, {"Tight", "Wide", "Epic", "Surround"}));
    bind (wp, new Knob (CRect (270, 18, 350, 118), this, widr::kWidth)); // (Editor::kWidth is the window)
    bind (wp, new Knob (knobAt (372, 30), this, kContrast));
    bind (wp, new Knob (knobAt (442, 30), this, kAir));
    bind (wp, new Knob (knobAt (512, 30), this, kBeyond));
    bind (wp, new Knob (knobAt (582, 30), this, kMonoBelow));
    bind (wp, new Knob (knobAt (652, 30), this, kGuard));

    auto* sp = new Panel (CRect (8, 440, 376, 548), "SPACE");
    root->addView (sp);
    const uint32_t spaceIds[5] = {kSize, kSpace, kDecay, kPreDelay, kDamping};
    for (int i = 0; i < 5; ++i)
        bind (sp, new Knob (knobAt (12 + i * 70, 28), this, spaceIds[i]));

    auto* mp = new Panel (CRect (384, 440, 752, 548), "MIX");
    root->addView (mp);
    bind (mp, new Choice (CRect (12, 28, 104, 62), this, kRole, "Role"));
    bind (mp, new Knob (knobAt (116, 28), this, kAware));
    mp->addView (new Label (CRect (184, 28, 244, 42), "Group", 10.0, false, 1));
    bind (mp, new NumberBox (CRect (184, 46, 244, 64), this, kGroup));
    bind (mp, new Toggle (CRect (184, 72, 282, 90), this, kMonoCheck, "Mono Check"));
    bind (mp, new Knob (knobAt (298, 28), this, kOutput, nullptr, true));

    // the parallel levels: the input and what Widr adds
    auto* lp = new Panel (CRect (8, 556, 752, 596), "");
    root->addView (lp);
    bind (lp, new pk::HSlider (CRect (12, 8, 364, 32), this, kDryLevel, "Dry"));
    bind (lp, new pk::HSlider (CRect (380, 8, 732, 32), this, kWetLevel, "Wet"));

    // the saturator at the end of the chain, with Smacheratr's displays above its controls
    auto* tailPanel = addTailPanel (root, CRect (8, 604, 752, 682 + smacheratr::TailDisplays::kHeight), kTailBase, kTailExtBase, kTailExt2Base, kTailExt3Base);
    tailDisplays = std::make_unique<smacheratr::TailDisplays> (this, smacheratr::TailBases {kTailBase, kTailExtBase, kTailExt2Base, kTailExt3Base},
                                                               [c = ctl] { auto* s = c->getShared (); return s ? (double)s->meters.sampleRate.load () : 48000.0; },
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
    if (stage)
        stage->invalid ();
}

void Editor::idle ()
{
    if (tailDisplays)
        tailDisplays->idle ();
    if (stage)
        stage->idle ();
    if (gonio)
        gonio->idle ();
    if (statusLabel)
    {
        const int latency = ctl->getShared () ? ctl->getShared ()->latency.load () : 0;
        const int n = stage ? stage->groupSize () : 0;
        char buf[96];
        if (n <= 1)
            std::snprintf (buf, sizeof (buf), "Latency %d samples \xC2\xB7 Alone", latency);
        else
            std::snprintf (buf, sizeof (buf), "Latency %d samples \xC2\xB7 %d in group %d", latency, n,
                           (int)std::lround (plainValue (kGroup)));
        statusLabel->setText (buf);
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

} // namespace widr
