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
    tail.reset ();
    stage = nullptr;
    gonio = nullptr;
    statusLabel = nullptr;
}

void Editor::buildUI (CFrame* f)
{
    auto* root = new Background (CRect (0, 0, kWidth, kHeight));
    f->addView (root);
    root->addView (new Label (CRect (12, 6, 200, 28), "widr", 14.0, true));
    statusLabel = new Label (CRect (66, 6, 340, 28), "", 10.5);
    statusLabel->setDim (true);
    root->addView (statusLabel);
    root->addView (new pk::PresetBar (CRect (440, 6, 636, 28), ctl));
    auto* helpBtn = new ActionButton (CRect (644, 6, 666, 28), "?", [this] { setTooltipsEnabled (!tooltipsEnabled ()); },
                                      [this] { return tooltipsEnabled (); });
    helpBtn->setTooltipText ("Show or hide the floating help tooltips (the info box at the bottom shows the same help either way).");
    root->addView (helpBtn);
    root->addView (new ActionButton (CRect (672, 6, 752, 28), "Menu", [this] { showMenu (layoutPoint (CPoint (672, 28))); }));

    stage = new StageView (CRect (kStageLeft, kStageTop, kStageRight, kStageBottom), this, ctl);
    pk::setHelp (stage, "Stage", help::kStage);
    root->addView (stage);
    gonio = new GonioView (CRect (568, 40, 752, 300), [c = ctl] () -> Meters* {
        auto* s = c->getShared ();
        return s ? &s->meters : nullptr;
    });
    pk::setHelp (gonio, "Goniometer", help::kGonio);
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

    // the cinema stage: the element lanes (a Position and a Width each), Cinema, Depth and Theatre
    auto* lanesPanel = new Panel (CRect (kLanesLeft, kLanesTop, 512, kLanesTop + 140), "LANES");
    root->addView (lanesPanel);
    static const char* laneNames[kNumLanes] = {"Voice", "Bass", "Hits", "Tones", "Ambience"};
    for (int l = 0; l < kNumLanes; ++l)
    {
        const double x = 6 + l * kLaneColumn;
        bind (lanesPanel, new Choice (CRect (x, 26, x + 88, 60), this, lanePosition (l), laneNames[l]));
        bind (lanesPanel, new Knob (knobAt (x + 16, 68), this, laneWidth (l)));
    }
    auto* cp = new Panel (CRect (kCinemaLeft, kLanesTop, 752, kLanesTop + 140), "CINEMA");
    root->addView (cp);
    bind (cp, new Knob (CRect (12, 24, 92, 124), this, kCinema));
    bind (cp, new Knob (knobAt (100, 36), this, kDepth));
    bind (cp, new Knob (knobAt (166, 36), this, kTheatre));

    // the parallel levels: the input and what Widr adds
    auto* lp = new Panel (CRect (8, kLevelsTop, 752, kLevelsTop + 40), "");
    root->addView (lp);
    bind (lp, new pk::HSlider (CRect (12, 8, 364, 32), this, kDryLevel, "Dry"));
    bind (lp, new pk::HSlider (CRect (380, 8, 732, 32), this, kWetLevel, "Wet"));

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
                                                    [c = ctl] { auto* s = c->getShared (); return s ? (double)s->meters.sampleRate.load () : 48000.0; },
                                                    [c = ctl] () -> const smacheratr::Meters* { auto* s = c->getShared (); return s ? &s->tailMeters : nullptr; });
}

pk::basic::Spec Editor::basicSpec ()
{
    // how wide and in what character (Character, Width), the top's sheen (Air), the low end kept mono
    // (Mono Below) and the cinema stage's lanes (Cinema); Output
    using namespace pk::basic;
    Spec s;
    s.title = "widr";
    s.capture = [c = ctl] () -> const pk::CaptureBuffer* { auto* sh = c->getShared (); return sh ? &sh->capture : nullptr; };
    s.displayHeight = 220;
    s.display = [this] (const CRect& r) -> CView* {
        auto* g = new pk::Group (r);
        const double split = r.getWidth () - 224; // (the stage 460 wide: whole pixels at every zoom)
        stage = new StageView (CRect (0, 0, split - 8, r.getHeight ()), this, ctl);
        pk::setHelp (stage, "Stage", help::kStage);
        g->addView (stage);
        gonio = new GonioView (CRect (split, 0, r.getWidth (), r.getHeight ()), [c = ctl] () -> Meters* {
            auto* sh = c->getShared ();
            return sh ? &sh->meters : nullptr;
        });
        pk::setHelp (gonio, "Goniometer", help::kGonio);
        g->addView (gonio);
        return g;
    };
    s.rows = {{segmented (kCharacter, "Character", {"Tight", "Wide", "Epic", "Surround"})},
              {knob (widr::kWidth), knob (kAir), knob (kMonoBelow), knob (kCinema)}};
    s.output = {knob (kOutput, {}, true)};
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
    if (stage)
        stage->invalid ();
}

void Editor::idle ()
{
    if (tail)
        tail->idle ();
    if (stage)
        stage->idle ();
    if (gonio)
        gonio->idle ();
    if (statusLabel)
    {
        const int latency = ctl->getShared () ? ctl->getShared ()->latency.load () : 0;
        const int n = stage ? stage->groupSize () : 0;
        // (shorter versions where the line has no room: the latency abbreviated, then left to the tooltip)
        const std::string dot = " \xC2\xB7 ", lat = std::to_string (latency);
        const std::string group = n <= 1 ? std::string ("Alone")
                                         : std::to_string (n) + " in group " + std::to_string ((int)std::lround (plainValue (kGroup)));
        statusLabel->setTexts ({"Latency " + lat + " samples" + dot + group, "Latency " + lat + dot + group, lat + " smp" + dot + group, group});
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
    // the stage, the goniometer, the width over the levels, the space over the mix; the end saturator under
    // them, beside the cinema stage (the lanes over Cinema)
    s.panels = {
        {"stage", "stage", {8, 40, 560, 300}, 0},
        {"gonio", "goniometer", {568, 40, 752, 300}, 0},
        {"width", "", {8, 308, 752, 432}, 0, 0},
        {"levels", "levels", {8, kLevelsTop, 752, kLevelsTop + 40}, 0, 0},
        {"space", "", {8, 440, 376, 548}, 0, 1},
        {"mix", "", {384, 440, 752, 548}, 0, 1},
        {"tail", "end of the chain", {8, kTailTop, 752, kTailTop + smacheratr::TailPanel::kOpenHeight}, 1, -1, true},
        {"lanes", "", {kLanesLeft, kLanesTop, 512, kLanesTop + 140}, 1, 2},
        {"cinema", "", {kCinemaLeft, kLanesTop, 752, kLanesTop + 140}, 1, 2},
    };
    return s;
}

} // namespace widr
