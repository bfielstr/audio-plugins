#include "Editor.h"

#include "Help.h"
#include "TapeView.h"
#include "plugin/Controller.h"

#include "pluginkit/ui/Theme.h"
#include "pluginkit/vst/Clipboard.h"
#include "pluginkit/vst/PresetBar.h"

#include "vstgui/lib/cframe.h"
#include "vstgui/lib/controls/coptionmenu.h"

#include <cmath>
#include <cstdio>
#include <vector>

namespace tapr {

using namespace VSTGUI;
using pk::ActionButton;
using pk::Choice;
using pk::Knob;
using pk::Label;
using pk::Panel;
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

CRect Editor::displayRect (bool arranged) const
{
    // (arranged: as tall as the panels' row, from the top, so its region holds no other panel)
    return arranged ? CRect (8, kTop, 8 + kArrangedDisplayW, kTop + kRowBottom - kRowTop) : CRect (8, kTop, kWidth - 8, kDisplayBottom);
}

TapeView* Editor::makeDisplay (const CRect& r)
{
    display = new TapeView (r, this, [c = ctl] () -> const Meters* { auto* s = c->getShared (); return s ? &s->meters : nullptr; });
    pk::setHelp (display, "Display", help::kTapeView);
    return display;
}

void Editor::buildUI (CFrame* f)
{
    auto* root = new Background (CRect (0, 0, kWidth, kHeight));
    f->addView (root);
    root->addView (new Label (CRect (12, 6, 200, 28), "tapr", 14.0, true));
    latencyLabel = new Label (CRect (120, 6, 260, 28), "", 10.5);
    latencyLabel->setDim (true);
    root->addView (latencyLabel);
    const double hx = kWidth - 320; // the header's controls at the right, as in the other plug-ins
    root->addView (new pk::PresetBar (CRect (hx, 6, hx + 196, 28), ctl));
    auto* helpBtn = new ActionButton (CRect (hx + 204, 6, hx + 226, 28), "?", [this] { setTooltipsEnabled (!tooltipsEnabled ()); },
                                      [this] { return tooltipsEnabled (); });
    helpBtn->setTooltipText ("Show or hide the floating help tooltips (the info box at the bottom shows the same help either way).");
    root->addView (helpBtn);
    root->addView (new ActionButton (CRect (hx + 232, 6, hx + 312, 28), "Menu", [this, hx] { showMenu (layoutPoint (CPoint (hx + 232, 28))); }));

    root->addView (makeDisplay (displayRect (arrangedLayout ())));

    // TAPE: the switch and Compression's place, then Drive, Speed and Compression
    auto* tape = new Panel (CRect (kTapeLeft, kRowTop, kTapeRight, kRowBottom), "TAPE");
    root->addView (tape);
    const double tw = kTapeRight - kTapeLeft;
    bind (tape, new Toggle (CRect (12, kSwitchTop, 132, kSwitchTop + kSwitchH), this, kTapeOn, "Tape"));
    bind (tape, new Choice (CRect (tw - 152, kSwitchTop, tw - 12, kSwitchTop + kSwitchH), this, kCompPlace, "Comp"));
    for (int k = 0; k < 3; ++k)
    {
        const uint32_t id = k == 0 ? kDrive : (k == 1 ? kSpeed : kCompression);
        bind (tape, new Knob (CRect (kTapeX[k], kKnobTop, kTapeX[k] + kTapeKnobW, kKnobTop + kKnobH), this, id));
    }

    // CUT: the switch and its place, then Lo, Mid and Hi
    auto* cutPanel = new Panel (CRect (kCutLeft, kRowTop, kCutRight, kRowBottom), "CUT");
    root->addView (cutPanel);
    const double cw = kCutRight - kCutLeft;
    bind (cutPanel, new Toggle (CRect (12, kSwitchTop, 102, kSwitchTop + kSwitchH), this, kCutOn, "Cut"));
    bind (cutPanel, new Choice (CRect (cw - 142, kSwitchTop, cw - 12, kSwitchTop + kSwitchH), this, kCutPlace, "Place"));
    for (int k = 0; k < 3; ++k)
        bind (cutPanel, new Knob (CRect (kCutX[k], kKnobTop, kCutX[k] + kKnobW, kKnobTop + kKnobH), this, kCutLo + (uint32_t)k, nullptr, true));

    // OUTPUT: Mix and Output
    auto* out = new Panel (CRect (kOutLeft, kRowTop, kOutRight, kRowBottom), "OUTPUT");
    root->addView (out);
    bind (out, new Knob (CRect (kOutX[0], kKnobTop, kOutX[0] + kKnobW, kKnobTop + kKnobH), this, kMix));
    bind (out, new Knob (CRect (kOutX[1], kKnobTop, kOutX[1] + kKnobW, kKnobTop + kKnobH), this, kOutput, nullptr, true));

    // the saturator at the end of the chain (off in a new instance: the tape stage is the saturator)
    tail = makeTail ();
    tail->add (root, layoutRegion ("tail", CRect (8, kTailTop, kWidth - 8, kTailTop + smacheratr::TailPanel::kOpenHeight)));

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
    // the tape's three knobs and the Cut's three bells; Mix and Output
    using namespace pk::basic;
    Spec s;
    s.title = "tapr";
    s.capture = [c = ctl] () -> const pk::CaptureBuffer* { auto* sh = c->getShared (); return sh ? &sh->capture : nullptr; };
    s.displayHeight = 180;
    s.display = [this] (const CRect& r) -> CView* { return makeDisplay (r); };
    s.rows = {{knob (kDrive), knob (kSpeed), knob (kCompression)}, {knob (kCutLo, {}, true), knob (kCutMid, {}, true), knob (kCutHi, {}, true)}};
    s.output = {knob (kMix), knob (kOutput, {}, true)};
    smacheratr::TailPanel::addToBasic (s, this, tailBases (), tail, [this] { return makeTail (); });
    s.menu = [this] (CPoint p) { showMenu (p); };
    s.help = &help::forParam;
    s.advancedSwitch = CRect (300, 6, 384, 28);
    return s;
}

void Editor::paramChanged (uint32_t id)
{
    pk::EditorBase::paramChanged (id);
    if (tail)
        tail->paramChanged (id);
    if (id < kMix && display)
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
            // its own (the oversampling) and the end saturator's (-1: not known yet)
            const int tailNow = s->tailMeters.latency.load ();
            latencyLabel->setTexts (pk::latencyTexts (tailNow >= 0 ? s->ownLatency.load () + tailNow : s->latency.load ()));
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
    // Wide: the display (narrower, as tall as the panels), TAPE, CUT and OUTPUT side by side; the end
    // saturator in a row of its own
    s.panels = {
        {"display", "display", {d.left, d.top, d.right, d.bottom}, 0},
        {"tape", "", {kTapeLeft, kRowTop, kTapeRight, kRowBottom}, 0},
        {"cut", "", {kCutLeft, kRowTop, kCutRight, kRowBottom}, 0},
        {"output", "", {kOutLeft, kRowTop, kOutRight, kRowBottom}, 0},
        {"tail", "end of the chain", {8, kTailTop, kWidth - 8, kTailTop + smacheratr::TailPanel::kOpenHeight}, 1, -1, true},
    };
    return s;
}

} // namespace tapr
