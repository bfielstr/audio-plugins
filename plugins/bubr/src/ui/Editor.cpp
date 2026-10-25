#include "Editor.h"

#include "BubrView.h"
#include "Help.h"
#include "plugin/Controller.h"

#include "pluginkit/ui/Theme.h"
#include "pluginkit/vst/Clipboard.h"
#include "pluginkit/vst/PresetBar.h"

#include "vstgui/lib/cframe.h"
#include "vstgui/lib/controls/coptionmenu.h"

#include <cmath>
#include <cstdio>
#include <vector>

namespace bubr {

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
}

CRect Editor::displayRect (bool arranged) const
{
    // (arranged: as tall as the panels' row, from the top, so its region holds no other panel)
    return arranged ? CRect (8, kTop, 8 + kArrangedDisplayW, kTop + kRowBottom - kRowTop) : CRect (8, kTop, kWidth - 8, kDisplayBottom);
}

BubrView* Editor::makeDisplay (const CRect& r)
{
    display = new BubrView (r, this, [c = ctl] () -> const Meters* { auto* s = c->getShared (); return s ? &s->meters : nullptr; });
    pk::setHelp (display, "Display", help::kBubrView);
    return display;
}

void Editor::buildUI (CFrame* f)
{
    auto* root = new Background (CRect (0, 0, kWidth, kHeight));
    f->addView (root);
    root->addView (new Label (CRect (12, 6, 200, 28), "bubr", 14.0, true));
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

    auto knob = [this] (Panel* p, int col, int row, uint32_t id, bool bipolar = false) {
        const double x = kKnobX[col], y = row == 0 ? kKnobTop : kKnobRow2;
        bind (p, new Knob (CRect (x, y, x + kKnobW, y + kKnobH), this, id, nullptr, bipolar));
    };

    // BAND: Freq and Q-Factor, then Low Cut
    auto* band = new Panel (CRect (kBandLeft, kRowTop, kBandRight, kRowBottom), "BAND");
    root->addView (band);
    knob (band, 0, 0, kFreq);
    knob (band, 1, 0, kQFactor);
    knob (band, 0, 1, kLowCut);

    // EFFECT: Boost and Intensity, then Drive and its x2
    auto* eff = new Panel (CRect (kEffLeft, kRowTop, kEffRight, kRowBottom), "EFFECT");
    root->addView (eff);
    knob (eff, 0, 0, kBoost);
    knob (eff, 1, 0, kIntensity);
    knob (eff, 0, 1, kDrive);
    bind (eff, new Toggle (CRect (kKnobX[1], kKnobRow2 + 22, kKnobX[1] + kKnobW, kKnobRow2 + 22 + kSwitchH), this, kDriveX2, "x2"));

    // GATE: Gate and Ratio, then Attack and Release
    auto* gate = new Panel (CRect (kGateLeft, kRowTop, kGateRight, kRowBottom), "GATE");
    root->addView (gate);
    knob (gate, 0, 0, kGate);
    knob (gate, 1, 0, kRatio);
    knob (gate, 0, 1, kAttack);
    knob (gate, 1, 1, kRelease);

    // OUTPUT: Dry/Wet and Output, then Oversample and Lookahead, then Soft Clip and Hard Clip (the end of the chain)
    auto* out = new Panel (CRect (kOutLeft, kRowTop, kOutRight, kRowBottom), "OUTPUT");
    root->addView (out);
    knob (out, 0, 0, kDryWet, true);
    knob (out, 1, 0, kOutput);
    const double cx = 152.0, cw = kOutRight - kOutLeft - cx - 12.0;
    bind (out, new Choice (CRect (cx, kKnobTop, cx + cw, kKnobTop + kChoiceH), this, kOversample, "Oversample"));
    bind (out, new Toggle (CRect (cx, kKnobTop + kChoiceH + 10, cx + cw, kKnobTop + kChoiceH + 10 + kSwitchH), this, kLookahead, "Lookahead"));
    bind (out, new Toggle (CRect (16, kKnobRow2 + 22, 116, kKnobRow2 + 22 + kSwitchH), this, kSoftClip, "Soft Clip"));
    bind (out, new Toggle (CRect (124, kKnobRow2 + 22, 224, kKnobRow2 + 22 + kSwitchH), this, kHardClip, "Hard Clip"));

    applyParamTooltips (&help::forParam);
    idle ();
}

pk::basic::Spec Editor::basicSpec ()
{
    // the effect's level and place, its character, the gate; Dry/Wet, Output and the clips
    using namespace pk::basic;
    Spec s;
    s.title = "bubr";
    s.capture = [c = ctl] () -> const pk::CaptureBuffer* { auto* sh = c->getShared (); return sh ? &sh->capture : nullptr; };
    s.displayHeight = 180;
    s.display = [this] (const CRect& r) -> CView* { return makeDisplay (r); };
    s.rows = {{knob (kBoost), knob (kFreq), knob (kIntensity)}, {knob (kQFactor), knob (kDrive), knob (kGate)}};
    s.output = {knob (kDryWet, {}, true), knob (kOutput), toggle (kSoftClip, "Soft Clip"), toggle (kHardClip, "Hard Clip")};
    s.menu = [this] (CPoint p) { showMenu (p); };
    s.help = &help::forParam;
    s.advancedSwitch = CRect (300, 6, 384, 28);
    return s;
}

void Editor::paramChanged (uint32_t id)
{
    pk::EditorBase::paramChanged (id);
    if (display)
        display->invalid ();
}

void Editor::idle ()
{
    if (display)
        display->idle ();
    if (latencyLabel)
        if (auto* s = ctl->getShared ())
            latencyLabel->setTexts (pk::latencyTexts (s->latency.load ())); // (Oversample's and Lookahead's)
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
    // Wide: the display (narrower, as tall as the panels), BAND, EFFECT, GATE and OUTPUT side by side
    s.panels = {
        {"display", "display", {d.left, d.top, d.right, d.bottom}, 0},
        {"band", "", {kBandLeft, kRowTop, kBandRight, kRowBottom}, 0},
        {"effect", "", {kEffLeft, kRowTop, kEffRight, kRowBottom}, 0},
        {"gate", "", {kGateLeft, kRowTop, kGateRight, kRowBottom}, 0},
        {"output", "", {kOutLeft, kRowTop, kOutRight, kRowBottom}, 0},
    };
    return s;
}

} // namespace bubr
