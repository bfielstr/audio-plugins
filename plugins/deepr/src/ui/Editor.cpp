#include "Editor.h"

#include "DeeprView.h"
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

namespace deepr {

using namespace VSTGUI;
using pk::ActionButton;
using pk::Knob;
using pk::Label;
using pk::Panel;
using pk::Segmented;

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
} // namespace

Editor::Editor (Controller* c) : pk::EditorBase (c, kWidth, kHeight), ctl (c) {}

void Editor::onClose ()
{
    display = nullptr;
    latencyLabel = nullptr;
    tail.reset ();
}

void Editor::buildUI (CFrame* f)
{
    auto* root = new Background (CRect (0, 0, kWidth, kHeight));
    f->addView (root);
    root->addView (new Label (CRect (12, 6, 200, 28), "deepr", 14.0, true));
    latencyLabel = new Label (CRect (200, 6, 340, 28), "", 10.5);
    root->addView (new pk::PresetBar (CRect (440, 6, 636, 28), ctl));
    latencyLabel->setDim (true);
    root->addView (latencyLabel);
    auto* helpBtn = new ActionButton (CRect (644, 6, 666, 28), "?", [this] { setTooltipsEnabled (!tooltipsEnabled ()); },
                                      [this] { return tooltipsEnabled (); });
    helpBtn->setTooltipText ("Show or hide the floating help tooltips (the info box at the bottom shows the same help either way).");
    root->addView (helpBtn);
    root->addView (new ActionButton (CRect (672, 6, 752, 28), "Menu", [this] { showMenu (layoutPoint (CPoint (672, 28))); }));

    display = new DeeprView (CRect (8, 40, 752, 290), this,
                             [c = ctl] { auto* s = c->getShared (); return s ? s->sampleRate.load () : 48000.0; },
                             [c = ctl] () -> const Meters* { auto* s = c->getShared (); return s ? &s->meters : nullptr; });
    pk::setHelp (display, "Display", help::kDisplay);
    root->addView (display);

    // the dip: how deep, where, and what opens it
    auto* dip = new Panel (CRect (8, 298, 752, 414), "DIP");
    root->addView (dip);
    bind (dip, new Knob (CRect (24, 12, 104, 112), this, kDepth));
    bind (dip, new Knob (CRect (150, 28, 206, 92), this, kDipFreq));
    bind (dip, new Knob (CRect (240, 28, 296, 92), this, kDipWidth));
    bind (dip, new Knob (CRect (380, 28, 436, 92), this, kThreshold));
    bind (dip, new Knob (CRect (470, 28, 526, 92), this, kAttack));
    bind (dip, new Knob (CRect (560, 28, 616, 92), this, kRelease));

    // the sub band, and listening to the parts
    auto* sub = new Panel (CRect (8, 422, 560, 522), "SUB");
    root->addView (sub);
    bind (sub, new Knob (CRect (24, 24, 80, 88), this, kSplit));
    bind (sub, new Knob (CRect (104, 24, 160, 88), this, kMonoSub));
    bind (sub, new Knob (CRect (184, 24, 240, 88), this, kSubGain, nullptr, true));
    sub->addView (new Label (CRect (290, 28, 530, 42), "Listen", 10.5, false, 1));
    bind (sub, new Segmented (CRect (290, 46, 530, 70), this, kListen, {"Off", "Sub", "Cut"}));

    auto* out = new Panel (CRect (568, 422, 752, 522), "OUTPUT");
    root->addView (out);
    bind (out, new Knob (CRect (24, 24, 80, 88), this, kMix));
    bind (out, new Knob (CRect (104, 24, 160, 88), this, kOutput, nullptr, true));

    // the saturator at the end of the chain, with Smacheratr's displays above its controls
    tail = makeTail ();
    tail->add (root, layoutRegion ("tail", CRect (8, 530, 752, 530 + smacheratr::TailPanel::kOpenHeight)));

    applyParamTooltips (&help::forParam);
    idle ();
}

smacheratr::TailBases Editor::tailBases () { return {kTailBase, kTailExtBase, kTailExt2Base, kTailExt3Base, kTailExt4Base}; }

std::unique_ptr<smacheratr::TailPanel> Editor::makeTail ()
{
    return std::make_unique<smacheratr::TailPanel> (this, tailBases (),
                                                    [c = ctl] { auto* s = c->getShared (); return s ? s->sampleRate.load () : 48000.0; },
                                                    [c = ctl] () -> const smacheratr::Meters* { auto* s = c->getShared (); return s ? &s->tailMeters : nullptr; });
}

pk::basic::Spec Editor::basicSpec ()
{
    // how deep the dip (Depth), where (Dip Freq) and from what level (Threshold), and the sub's level
    // (Sub Gain); Mix and Output
    using namespace pk::basic;
    Spec s;
    s.title = "deepr";
    s.capture = [c = ctl] () -> const pk::CaptureBuffer* { auto* sh = c->getShared (); return sh ? &sh->capture : nullptr; };
    s.displayHeight = 220;
    s.display = [this] (const CRect& r) -> CView* {
        display = new DeeprView (r, this, [c = ctl] { auto* sh = c->getShared (); return sh ? sh->sampleRate.load () : 48000.0; },
                                 [c = ctl] () -> const Meters* { auto* sh = c->getShared (); return sh ? &sh->meters : nullptr; });
        pk::setHelp (display, "Display", help::kDisplay);
        return display;
    };
    s.rows = {{knob (kDepth), knob (kDipFreq), knob (kThreshold), knob (kSubGain, {}, true)}};
    s.output = {knob (kMix), knob (kOutput, {}, true)};
    smacheratr::TailPanel::addToBasic (s, this, tailBases (), tail, [this] { return makeTail (); });
    s.menu = [this] (CPoint p) { showMenu (p); };
    s.help = &help::forParam;
    s.advancedSwitch = CRect (348, 6, 432, 28);
    return s;
}

void Editor::paramChanged (uint32_t id)
{
    pk::EditorBase::paramChanged (id);
    if (tail)
        tail->paramChanged (id);
    if (display && id < kTailBase)
        display->invalid ();
}

void Editor::idle ()
{
    if (tail)
        tail->idle ();
    if (display)
        display->idle ();
    if (latencyLabel)
        if (auto* s = ctl->getShared ())
        {
            latencyLabel->setTexts (pk::latencyTexts (s->latency.load ()));
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
    addLayoutMenu (menu);
    menu->popup (frame, where, [this, sizes, menu, settingsAt] (COptionMenu* m) {
        const int32_t r = m->getLastResult ();
        if (pickedInSubMenu (m)) // (Layout: its entries act by themselves)
            return;
        if (settingsMenuPicked (r, settingsAt))
            return;
        if (r >= 0 && r < (int32_t)sizes.size ())
            resizeTo (sizes[(size_t)r]);
    });
}

pk::layout::Spec Editor::layoutSpec (bool) const
{
    pk::layout::Spec s;
    // the display beside the dip with the sub band under it, and the output; the end saturator under them
    s.panels = {
        {"display", "display", {8, 40, 752, 290}, 0},
        {"dip", "", {8, 298, 752, 414}, 0, 0},
        {"sub", "", {8, 422, 560, 522}, 0, 0},
        {"output", "", {568, 422, 752, 522}, 0},
        {"tail", "end of the chain", {8, 530, 752, 530 + smacheratr::TailPanel::kOpenHeight}, 1, -1, true},
    };
    return s;
}

} // namespace deepr
