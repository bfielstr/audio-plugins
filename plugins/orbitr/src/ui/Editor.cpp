#include "Editor.h"

#include "Help.h"
#include "OrbView.h"
#include "plugin/Controller.h"

#include "pluginkit/vst/Clipboard.h"
#include "pluginkit/ui/Theme.h"
#include "pluginkit/vst/PresetBar.h"

#include "vstgui/lib/cframe.h"
#include "vstgui/lib/controls/coptionmenu.h"

#include <cmath>
#include <cstdio>
#include <vector>

namespace orbitr {

using namespace VSTGUI;
using pk::ActionButton;
using pk::Knob;
using pk::Label;
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
    root->addView (new Label (CRect (12, 6, 200, 28), "orbitr", 14.0, true));
    latencyLabel = new Label (CRect (200, 6, 340, 28), "", 10.5);
    root->addView (new pk::PresetBar (CRect (440, 6, 636, 28), ctl));
    latencyLabel->setDim (true);
    root->addView (latencyLabel);
    auto* helpBtn = new ActionButton (CRect (644, 6, 666, 28), "?", [this] { setTooltipsEnabled (!tooltipsEnabled ()); },
                                      [this] { return tooltipsEnabled (); });
    helpBtn->setTooltipText ("Show or hide the floating help tooltips (the info box at the bottom shows the same help either way).");
    root->addView (helpBtn);
    root->addView (new ActionButton (CRect (672, 6, 752, 28), "Menu", [this] { showMenu (layoutPoint (CPoint (672, 28))); }));

    display = new OrbView (CRect (8, 40, 752, 290), this,
                           [c = ctl] () -> const Meters* { auto* s = c->getShared (); return s ? &s->meters : nullptr; });
    pk::setHelp (display, "Orbs", help::kOrbView);
    root->addView (display);

    // the motion: how the orbs move and how they are heard
    auto* motion = new Panel (CRect (kMotionLeft, kMotionTop, 600, 414), "MOTION");
    root->addView (motion);
    bind (motion, new Segmented (CRect (kPatternLeft, kPatternTop, kPatternLeft + kPatternW, kPatternTop + kPatternH), this, kPattern, {"Orbit", "Swarm"}));
    bind (motion, new Toggle (CRect (14, 62, 124, 82), this, kFloor, "Floor"));
    const uint32_t knobs[] = {kOrbs, kSpeed, kDistance, kRadius, kSpread, kRandom, kMix};
    for (int i = 0; i < 7; ++i)
        bind (motion, new Knob (CRect (140 + 64 * i, 28, 196 + 64 * i, 92), this, knobs[i]));

    // the grains: each orb plays grains of the recent input instead of the input (the knobs under
    // MOTION's first four)
    auto* grains = new Panel (CRect (kGrainsLeft, kGrainsTop, 600, kGrainsTop + 116), "GRAINS");
    root->addView (grains);
    bind (grains, new Toggle (CRect (kPatternLeft, kPatternTop, kPatternLeft + kPatternW, kPatternTop + kPatternH), this, kGrains, "Grains"));
    const uint32_t grainKnobs[] = {kGrainSize, kGrainDensity, kGrainScatter, kGrainPitch};
    for (int i = 0; i < 4; ++i)
        bind (grains, new Knob (CRect (140 + 64 * i, 28, 196 + 64 * i, 92), this, grainKnobs[i], nullptr, i == 3));

    // the output, beside both (its knobs on their rows)
    auto* out = new Panel (CRect (608, 298, 752, kGrainsTop + 116), "OUTPUT");
    root->addView (out);
    bind (out, new Knob (CRect (44, 28, 100, 92), this, kDryWet));
    bind (out, new Knob (CRect (44, 152, 100, 216), this, kOutput, nullptr, true));

    // the saturator at the end of the chain, with Smacheratr's displays above its controls
    tail = makeTail ();
    tail->add (root, layoutRegion ("tail", CRect (8, kTailTop, 752, kTailTop + smacheratr::TailPanel::kOpenHeight)));

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
    // how the orbs move (Pattern), how many (Orbs), how fast (Speed) and how wide (Spread); Dry/Wet and
    // Output
    using namespace pk::basic;
    Spec s;
    s.title = "orbitr";
    s.capture = [c = ctl] () -> const pk::CaptureBuffer* { auto* sh = c->getShared (); return sh ? &sh->capture : nullptr; };
    s.displayHeight = 220;
    s.display = [this] (const CRect& r) -> CView* {
        display = new OrbView (r, this, [c = ctl] () -> const Meters* { auto* sh = c->getShared (); return sh ? &sh->meters : nullptr; });
        pk::setHelp (display, "Orbs", help::kOrbView);
        return display;
    };
    s.rows = {{segmented (kPattern, "Pattern", {"Orbit", "Swarm"})}, {knob (kOrbs), knob (kSpeed), knob (kSpread)}};
    s.output = {knob (kDryWet), knob (kOutput, {}, true)};
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
    if (display && (id < kTailBase || id >= kGrains))
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
    // the orbs, the motion over the grains, the output; the end saturator under them
    s.panels = {
        {"display", "orbs", {8, 40, 752, 290}, 0},
        {"motion", "", {kMotionLeft, kMotionTop, 600, 414}, 0, 0},
        {"grains", "", {kGrainsLeft, kGrainsTop, 600, kGrainsTop + 116}, 0, 0},
        {"output", "", {608, 298, 752, kGrainsTop + 116}, 0},
        {"tail", "end of the chain", {8, kTailTop, 752, kTailTop + smacheratr::TailPanel::kOpenHeight}, 1, -1, true},
    };
    return s;
}

} // namespace orbitr
