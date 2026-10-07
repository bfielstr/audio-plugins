#include "Editor.h"

#include "Help.h"
#include "PinkView.h"
#include "plugin/Controller.h"

#include "pluginkit/ui/Theme.h"
#include "pluginkit/vst/Clipboard.h"
#include "pluginkit/vst/PresetBar.h"

#include "vstgui/lib/cframe.h"
#include "vstgui/lib/controls/coptionmenu.h"

#include <cmath>
#include <cstdio>
#include <vector>

namespace smeezr {

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

CRect Editor::displayRect (bool arranged) const
{
    // (arranged: as tall as the knobs' row, from the top, so its region holds no other panel)
    return arranged ? CRect (8, kTop, 8 + kArrangedDisplayW, kTop + kRowBottom - kRowTop) : CRect (8, kTop, kWidth - 8, kDisplayBottom);
}

void Editor::onClose ()
{
    display = nullptr;
    latencyLabel = nullptr;
    stageLabel = nullptr;
    tail.reset ();
}

void Editor::buildUI (CFrame* f)
{
    auto* root = new Background (CRect (0, 0, kWidth, kHeight));
    f->addView (root);
    root->addView (new Label (CRect (12, 6, 200, 28), "smeezr", 14.0, true));
    latencyLabel = new Label (CRect (200, 6, 430, 28), "", 10.5);
    latencyLabel->setDim (true);
    root->addView (latencyLabel);
    const double hx = kWidth - 320; // the header's controls at the right, as in the other plug-ins
    root->addView (new pk::PresetBar (CRect (hx, 6, hx + 196, 28), ctl));
    auto* helpBtn = new ActionButton (CRect (hx + 204, 6, hx + 226, 28), "?", [this] { setTooltipsEnabled (!tooltipsEnabled ()); },
                                      [this] { return tooltipsEnabled (); });
    helpBtn->setTooltipText ("Show or hide the floating help tooltips (the info box at the bottom shows the same help either way).");
    root->addView (helpBtn);
    root->addView (new ActionButton (CRect (hx + 232, 6, hx + 312, 28), "Menu", [this, hx] { showMenu (layoutPoint (CPoint (hx + 232, 28))); }));

    display = new PinkView (displayRect (arrangedLayout ()),
                            [c = ctl] () -> const Meters* { auto* s = c->getShared (); return s ? &s->meters : nullptr; });
    pk::setHelp (display, "Pink balance", help::kPinkView);
    root->addView (display);

    // MIX: the blend with the dry signal, and the pink stage's Speed
    auto* mix = new Panel (CRect (kMixLeft, kRowTop, kMixRight, kRowBottom), "MIX");
    root->addView (mix);
    bind (mix, new Knob (CRect (kKnobLeft, kKnobTop, kKnobLeft + kKnobW, kKnobTop + kKnobH), this, kMix));
    auto* speedLabel = new Label (CRect (kSpeedLeft, kSpeedTop - 18, kSpeedLeft + kSpeedW, kSpeedTop - 3), "Speed", 10.5, false, 1);
    speedLabel->setDim (true);
    mix->addView (speedLabel);
    bind (mix, new Segmented (CRect (kSpeedLeft, kSpeedTop, kSpeedLeft + kSpeedW, kSpeedTop + kSpeedH), this, kSpeed, {"Fast", "Slow"}));

    // the one knob, centre stage (the panel's title is its label)
    auto* squeeze = new Panel (CRect (kSqueezeLeft, kRowTop, kSqueezeRight, kRowBottom), "SQUEEZE");
    root->addView (squeeze);
    bind (squeeze, new Knob (CRect (kBigLeft, kBigTop, kBigLeft + kBigW, kBigTop + kBigH), this, kSqueeze, ""));
    stageLabel = new Label (CRect (8, kStageTop, kSqueezeRight - kSqueezeLeft - 8, kStageTop + 16), "", 10.5, false, 1);
    stageLabel->setDim (true);
    pk::setHelp (stageLabel, "Stage", help::kStage);
    squeeze->addView (stageLabel);

    // OUTPUT
    auto* out = new Panel (CRect (kOutLeft, kRowTop, kOutRight, kRowBottom), "OUTPUT");
    root->addView (out);
    bind (out, new Knob (CRect (kKnobLeft, kKnobTop, kKnobLeft + kKnobW, kKnobTop + kKnobH), this, kOutput, nullptr, true));

    // the saturator at the end of the chain, with Smacheratr's displays above its controls
    tail = std::make_unique<smacheratr::TailPanel> (this, smacheratr::TailBases {kTailBase, kTailExtBase, kTailExt2Base, kTailExt3Base, kTailExt4Base},
                                                    [c = ctl] { auto* s = c->getShared (); return s ? s->sampleRate.load () : 48000.0; },
                                                    [c = ctl] () -> const smacheratr::Meters* { auto* s = c->getShared (); return s ? &s->tailMeters : nullptr; });
    tail->add (root, layoutRegion ("tail", CRect (8, kTailTop, kWidth - 8, kTailTop + smacheratr::TailPanel::kOpenHeight)));

    applyParamTooltips (&help::forParam);
    updateStage ();
    idle ();
}

void Editor::updateStage ()
{
    if (!stageLabel)
        return;
    const double u = plainValue (kSqueeze);
    const int pink = (int)std::lround (100.0 * Engine::pinkAmount (u)), ott = (int)std::lround (100.0 * Engine::ottDepth (u));
    char buf[64];
    if (u <= 0.0)
        std::snprintf (buf, sizeof (buf), "untouched");
    else if (u <= 0.5)
        std::snprintf (buf, sizeof (buf), "toward pink %d %%", pink);
    else
        std::snprintf (buf, sizeof (buf), "pink + OTT boost %d %%", ott);
    stageLabel->setText (buf);
}

void Editor::paramChanged (uint32_t id)
{
    pk::EditorBase::paramChanged (id);
    if (tail)
        tail->paramChanged (id);
    if (id == kSqueeze)
        updateStage ();
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
            char buf[64];
            const int tailNow = s->tailMeters.latency.load (); // (the end saturator's is all of it; -1: not known yet)
            std::snprintf (buf, sizeof (buf), "Latency %d samples", tailNow >= 0 ? tailNow : s->latency.load ());
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

pk::layout::Spec Editor::layoutSpec (bool arranged) const
{
    pk::layout::Spec s;
    const CRect d = displayRect (arranged);
    // Wide: the display (narrower, as tall as the knobs' row), MIX, SQUEEZE and OUTPUT side by side; the end
    // saturator in a row of its own
    s.panels = {
        {"display", "display", {d.left, d.top, d.right, d.bottom}, 0},
        {"mix", "", {kMixLeft, kRowTop, kMixRight, kRowBottom}, 0},
        {"squeeze", "", {kSqueezeLeft, kRowTop, kSqueezeRight, kRowBottom}, 0},
        {"output", "", {kOutLeft, kRowTop, kOutRight, kRowBottom}, 0},
        {"tail", "end of the chain", {8, kTailTop, kWidth - 8, kTailTop + smacheratr::TailPanel::kOpenHeight}, 1, -1, true},
    };
    return s;
}

} // namespace smeezr
