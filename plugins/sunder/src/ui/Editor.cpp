#include "Editor.h"

#include "Help.h"
#include "SunderView.h"
#include "plugin/Controller.h"

#include "pluginkit/ui/Theme.h"
#include "pluginkit/vst/Clipboard.h"
#include "pluginkit/vst/PresetBar.h"

#include "vstgui/lib/cframe.h"
#include "vstgui/lib/controls/coptionmenu.h"

#include <cmath>
#include <cstdio>
#include <vector>

namespace sunder {

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

SunderView* Editor::makeDisplay (const CRect& r)
{
    display = new SunderView (r, this, [c = ctl] () -> const Meters* { auto* s = c->getShared (); return s ? &s->meters : nullptr; });
    pk::setHelp (display, "Display", help::kSunderView);
    return display;
}

void Editor::buildUI (CFrame* f)
{
    auto* root = new Background (CRect (0, 0, kWidth, kHeight));
    f->addView (root);
    root->addView (new Label (CRect (12, 6, 200, 28), "sunder", 14.0, true));
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

    // COMPRESSOR: Curve, SC Filter and Link at the top, then Input, Ratio, Attack and Release, then Ext SC
    // and SC Gain
    auto* comp = new Panel (CRect (kCompLeft, kRowTop, kCompRight, kRowBottom), "COMPRESSOR");
    root->addView (comp);
    bind (comp, new Choice (CRect (12, kChoiceTop, 152, kChoiceTop + kChoiceH), this, kCurve, "Curve"));
    bind (comp, new Choice (CRect (160, kChoiceTop, 280, kChoiceTop + kChoiceH), this, kScFilter, "SC Filter"));
    bind (comp, new Toggle (CRect (300, kSwitchTop, 380, kSwitchTop + kSwitchH), this, kLink, "Link"));
    for (int k = 0; k < 4; ++k)
    {
        const uint32_t id = k == 0 ? kInput : (k == 1 ? kRatio : (k == 2 ? kAttack : kRelease));
        bind (comp, new Knob (CRect (kCompX[k], kKnobTop, kCompX[k] + kCompKnobW, kKnobTop + kKnobH), this, id, nullptr, id == kInput));
    }
    // the sidechain input: Ext SC and its level
    bind (comp, new Toggle (CRect (12, kKnobRow2 + 22, 92, kKnobRow2 + 22 + kSwitchH), this, kExtSc, "Ext SC"));
    bind (comp, new Knob (CRect (kCompX[1], kKnobRow2, kCompX[1] + kCompKnobW, kKnobRow2 + kKnobH), this, kScGain, nullptr, true));

    // GERMANIUM: the output stage, Drive and Feedback, then Saturation
    auto* germ = new Panel (CRect (kGermLeft, kRowTop, kGermRight, kRowBottom), "GERMANIUM");
    root->addView (germ);
    bind (germ, new Knob (CRect (kGermX[0], kKnobTop, kGermX[0] + kKnobW, kKnobTop + kKnobH), this, kDrive));
    bind (germ, new Knob (CRect (kGermX[1], kKnobTop, kGermX[1] + kKnobW, kKnobTop + kKnobH), this, kFeedback));
    bind (germ, new Knob (CRect (kGermX[0], kKnobRow2, kGermX[0] + kKnobW, kKnobRow2 + kKnobH), this, kSaturation));

    // OUTPUT: Mode, then Mix and Output, then Soft Clip and Hard Clip (the end of the chain)
    auto* out = new Panel (CRect (kOutLeft, kRowTop, kOutRight, kRowBottom), "OUTPUT");
    root->addView (out);
    bind (out, new Choice (CRect (12, kChoiceTop, kOutRight - kOutLeft - 12, kChoiceTop + kChoiceH), this, kMode, "Mode"));
    bind (out, new Knob (CRect (kOutX[0], kKnobTop, kOutX[0] + kKnobW, kKnobTop + kKnobH), this, kMix));
    bind (out, new Knob (CRect (kOutX[1], kKnobTop, kOutX[1] + kKnobW, kKnobTop + kKnobH), this, kOutput, nullptr, true));
    bind (out, new Toggle (CRect (kOutX[0], kClipTop, kOutX[0] + kClipW, kClipTop + kSwitchH), this, kSoftClip, "Soft Clip"));
    bind (out, new Toggle (CRect (kOutX[0], kClipTop + kSwitchH + 6, kOutX[0] + kClipW, kClipTop + 2 * kSwitchH + 6), this, kHardClip, "Hard Clip"));

    // BASS: the bass enhancer (ahead of the compressor, or after it: Bass Position). Bass, Bass Boost, Bass Freq,
    // Q-Factor, Intensity and Bass Drive, then x2 and Oversample; Bass Gate, Gate Ratio, Gate Attack, Gate Release
    // and Low Cut, then Bass Position under Oversample
    auto* bassPanel = new Panel (CRect (kBassLeft, kBassTop, kBassRight, kBassBottom), "BASS");
    root->addView (bassPanel);
    const uint32_t row1[6] = {kBass, kBassBoost, kBassFreq, kBassQ, kBassIntensity, kBassDrive};
    for (int k = 0; k < 6; ++k)
    {
        const double x = 12.0 + k * kBassPitch;
        bind (bassPanel, new Knob (CRect (x, kChoiceTop, x + kBassKnobW, kChoiceTop + kKnobH), this, row1[k]));
    }
    const double sx = 12.0 + 6 * kBassPitch;
    bind (bassPanel, new Toggle (CRect (sx, kChoiceTop + 22, sx + 60, kChoiceTop + 22 + kSwitchH), this, kBassDriveX2, "x2"));
    bind (bassPanel, new Choice (CRect (sx + 72, kChoiceTop + 8, kBassRight - kBassLeft - 12, kChoiceTop + 8 + kChoiceH), this, kBassOversample,
                                 "Oversample"));
    const uint32_t row2[5] = {kBassGate, kBassRatio, kBassAttack, kBassRelease, kBassLowCut};
    for (int k = 0; k < 5; ++k)
    {
        const double x = 12.0 + k * kBassPitch, y = kChoiceTop + kKnobH + 16;
        bind (bassPanel, new Knob (CRect (x, y, x + kBassKnobW, y + kKnobH), this, row2[k], nullptr, row2[k] == kBassGate));
    }
    const double py = kChoiceTop + kKnobH + 16 + 8;
    bind (bassPanel, new Choice (CRect (sx + 72, py, kBassRight - kBassLeft - 12, py + kChoiceH), this, kBassPosition, "Bass Position"));

    applyParamTooltips (&help::forParam);
    idle ();
}

pk::basic::Spec Editor::basicSpec ()
{
    // the compressor's Input, Ratio and Release, the output stage's Drive and Feedback, and Bass (the enhancer
    // ahead of the compressor); Mix, Output, Soft Clip and Hard Clip
    using namespace pk::basic;
    Spec s;
    s.title = "sunder";
    s.capture = [c = ctl] () -> const pk::CaptureBuffer* { auto* sh = c->getShared (); return sh ? &sh->capture : nullptr; };
    s.displayHeight = 180;
    s.display = [this] (const CRect& r) -> CView* { return makeDisplay (r); };
    s.rows = {{knob (kInput, {}, true), knob (kRatio), knob (kRelease)}, {knob (kDrive), knob (kFeedback), knob (kBass)}};
    s.output = {knob (kMix), knob (kOutput, {}, true), toggle (kSoftClip, "Soft Clip"), toggle (kHardClip, "Hard Clip")};
    s.menu = [this] (CPoint p) { showMenu (p); };
    s.help = &help::forParam;
    s.advancedSwitch = CRect (300, 6, 384, 28);
    return s;
}

void Editor::paramChanged (uint32_t id)
{
    pk::EditorBase::paramChanged (id);
    if ((id == kDrive || id == kFeedback || id == kCurve || id == kRatio) && display)
        display->invalid ();
}

void Editor::idle ()
{
    if (display)
        display->idle ();
    if (latencyLabel)
        if (auto* s = ctl->getShared ())
            latencyLabel->setTexts (pk::latencyTexts (s->latency.load ())); // (the oversampling's, Bass Oversample's)
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
    // Wide: the display (narrower, as tall as the panels), COMPRESSOR, GERMANIUM and OUTPUT side by side, BASS under them
    s.panels = {
        {"display", "display", {d.left, d.top, d.right, d.bottom}, 0},
        {"compressor", "", {kCompLeft, kRowTop, kCompRight, kRowBottom}, 0},
        {"germanium", "", {kGermLeft, kRowTop, kGermRight, kRowBottom}, 0},
        {"output", "", {kOutLeft, kRowTop, kOutRight, kRowBottom}, 0},
        {"bass", "", {kBassLeft, kBassTop, kBassRight, kBassBottom}, 1},
    };
    return s;
}

} // namespace sunder
