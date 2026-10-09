#include "Editor.h"

#include "Help.h"
#include "HistoryView.h"
#include "plugin/Controller.h"

#include "pluginkit/vst/Clipboard.h"
#include "pluginkit/ui/Theme.h"
#include "pluginkit/vst/PresetBar.h"

#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cframe.h"
#include "vstgui/lib/controls/coptionmenu.h"

#include <cmath>
#include <cstdio>
#include <vector>

namespace smoothr {

using namespace VSTGUI;
using pk::ActionButton;
using pk::Knob;
using pk::Label;
using pk::Panel;
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
} // namespace

Editor::Editor (Controller* c) : pk::EditorBase (c, kWidth, kHeight), ctl (c) {}

void Editor::onClose ()
{
    tail.reset ();
    history = nullptr;
}

void Editor::buildUI (CFrame* f)
{
    auto* root = new Background (CRect (0, 0, kWidth, kHeight));
    f->addView (root);
    root->addView (new Label (CRect (12, 6, 200, 28), "smoothr", 14.0, true));
    root->addView (new pk::PresetBar (CRect (580, 6, 776, 28), ctl));
    auto* helpBtn = new ActionButton (CRect (784, 6, 806, 28), "?", [this] { setTooltipsEnabled (!tooltipsEnabled ()); },
                                      [this] { return tooltipsEnabled (); });
    helpBtn->setTooltipText ("Show or hide the floating help tooltips (the info box at the bottom shows the same help either way).");
    root->addView (helpBtn);
    root->addView (new ActionButton (CRect (812, 6, 892, 28), "Menu", [this] { showMenu (layoutPoint (CPoint (812, 28))); }));

    history = new HistoryView (CRect (kViewLeft, kViewTop, kViewRight, kViewBottom), this, [c = ctl] () -> Meters* {
        auto* s = c->getShared ();
        return s ? &s->meters : nullptr;
    });
    pk::setHelp (history, "History", help::kDisplay);
    root->addView (history);

    // the limiter: Input, Ceiling, Release (and Auto), Smooth
    auto* lim = new Panel (CRect (kViewLeft, kRowTop, 560, kRowTop + 92), "limiter");
    root->addView (lim);
    bind (lim, new Knob (knobRect (12, 22), this, kInput));
    bind (lim, new Knob (knobRect (80, 22), this, kCeiling));
    bind (lim, new Knob (knobRect (164, 22), this, kRelease));
    bind (lim, new Toggle (CRect (226, 44, 276, 64), this, kAutoRelease, "Auto"));
    bind (lim, new Knob (knobRect (300, 22), this, kSmooth));
    lim->addView (new Label (CRect (370, 30, 546, 46), "the lows move slowly,", 10.0));
    lim->addView (new Label (CRect (370, 46, 546, 62), "the highs take the peaks", 10.0));

    // Character
    auto* chr = new Panel (CRect (568, kRowTop, kViewRight, kRowTop + 92), "character");
    root->addView (chr);
    bind (chr, new Knob (knobRect (12, 22), this, kCharacter));
    chr->addView (new Label (CRect (80, 30, 316, 46), "a dip in the low mids, before the limiter,", 10.0));
    chr->addView (new Label (CRect (80, 46, 316, 62), "only when they get loud", 10.0));

    // the saturator before the limiter, with Smacheratr's displays above its controls
    tail = makeTail ();
    tail->add (root, layoutRegion ("tail", CRect (kViewLeft, kTailTop, kViewRight, kTailTop + smacheratr::TailPanel::kOpenHeight)));

    applyParamTooltips (&help::forParam);
    idle ();
}

smacheratr::TailBases Editor::tailBases () { return {kTailBase, kTailExtBase, kTailExt2Base, kTailExt3Base, kTailExt4Base}; }

std::unique_ptr<smacheratr::TailPanel> Editor::makeTail ()
{
    return std::make_unique<smacheratr::TailPanel> (this, tailBases (),
        [c = ctl] {
            auto* s = c->getShared ();
            return s ? s->sampleRate.load () : 48000.0;
        },
        [c = ctl] () -> const smacheratr::Meters* {
            auto* s = c->getShared ();
            return s ? &s->tailMeters : nullptr;
        }, "smacheratr  (before the limiter)");
}

pk::basic::Spec Editor::basicSpec ()
{
    // how hard into the limiter (Input), how its bands share the work (Smooth), the low mids' dip
    // (Character) and the release; the ceiling at the right (it is the output level)
    using namespace pk::basic;
    Spec s;
    s.title = "smoothr";
    s.capture = [c = ctl] () -> const pk::CaptureBuffer* { auto* sh = c->getShared (); return sh ? &sh->capture : nullptr; };
    s.displayHeight = 220;
    s.display = [this] (const CRect& r) -> CView* {
        history = new HistoryView (r, this, [c = ctl] () -> Meters* {
            auto* sh = c->getShared ();
            return sh ? &sh->meters : nullptr;
        });
        pk::setHelp (history, "History", help::kDisplay);
        return history;
    };
    s.rows = {{knob (kInput), knob (kSmooth), knob (kCharacter), knob (kRelease)}};
    s.output = {knob (kCeiling)};
    smacheratr::TailPanel::addToBasic (s, this, tailBases (), tail, [this] { return makeTail (); });
    s.menu = [this] (CPoint p) { showMenu (p); };
    s.help = &help::forParam;
    s.advancedSwitch = CRect (484, 6, 572, 28);
    return s;
}

void Editor::paramChanged (uint32_t id)
{
    pk::EditorBase::paramChanged (id);
    if (tail)
        tail->paramChanged (id);
    if (id == kCeiling && history)
        history->invalid ();
}

void Editor::idle ()
{
    if (tail)
        tail->idle ();
    if (history)
        history->idle ();
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
    // the history beside the limiter over the character; the end saturator under them
    s.panels = {
        {"history", "history", {kViewLeft, kViewTop, kViewRight, 300}, 0},
        {"limiter", "", {kViewLeft, kRowTop, 560, kRowTop + 92}, 0, 0},
        {"character", "", {568, kRowTop, kViewRight, kRowTop + 92}, 0, 0},
        {"tail", "end of the chain", {kViewLeft, 408, kViewRight, 408 + smacheratr::TailPanel::kOpenHeight}, 1, -1, true},
    };
    return s;
}

} // namespace smoothr
