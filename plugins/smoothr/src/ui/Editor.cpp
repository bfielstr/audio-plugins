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
    tailDisplays.reset ();
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
    root->addView (new ActionButton (CRect (812, 6, 892, 28), "Menu", [this] { showMenu (CPoint (812, 28)); }));

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
    auto* tailPanel = addTailPanel (root, CRect (kViewLeft, kTailTop, kViewRight, kTailTop + 78 + smacheratr::TailDisplays::kHeight),
                                    kTailBase, kTailExtBase, kTailExt2Base, kTailExt3Base, "smacheratr  (before the limiter)");
    tailDisplays = std::make_unique<smacheratr::TailDisplays> (
        this, smacheratr::TailBases {kTailBase, kTailExtBase, kTailExt2Base, kTailExt3Base, kTailExt4Base},
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
    idle ();
}

void Editor::paramChanged (uint32_t id)
{
    pk::EditorBase::paramChanged (id);
    if (tailDisplays)
        tailDisplays->paramChanged (id);
    if (id == kCeiling && history)
        history->invalid ();
}

void Editor::idle ()
{
    if (tailDisplays)
        tailDisplays->idle ();
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
    menu->popup (frame, where, [this, sizes, menu, settingsAt] (COptionMenu* m) {
        const int32_t r = m->getLastResult ();
        if (settingsMenuPicked (r, settingsAt))
            return;
        if (r >= 0 && r < (int32_t)sizes.size ())
            resizeTo (sizes[(size_t)r]);
    });
}

} // namespace smoothr
