#include "Editor.h"

#include "FilterView.h"
#include "GainLock.h"
#include "Help.h"
#include "plugin/Controller.h"

#include "pluginkit/vst/Clipboard.h"
#include "pluginkit/ui/Theme.h"
#include "pluginkit/vst/PresetBar.h"

#include "vstgui/lib/cframe.h"
#include "vstgui/lib/controls/coptionmenu.h"
#include "vstgui/lib/events.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace para {

using namespace VSTGUI;
using pk::ActionButton;
using pk::Choice;
using pk::Knob;
using pk::Label;
using pk::Panel;
using pk::Segmented;
using pk::Toggle;

namespace {
constexpr double kKnobW = 56, kKnobH = 64;
CRect knobRect (double x, double y) { return CRect (x, y, x + kKnobW, y + kKnobH); }

// A slope's drop-down (eleven slopes): the mouse wheel over it also steps through the list, as over a
// knob (up: steeper).
class ScrollChoice : public Choice
{
public:
    using Choice::Choice;
    void onMouseWheelEvent (MouseWheelEvent& e) override
    {
        const int steps = host->table ().info (param).stepCount ();
        const double d = e.deltaY != 0.0 ? e.deltaY : e.deltaX;
        if (steps <= 0 || d == 0.0)
            return;
        host->setOnce (param, std::clamp (host->norm (param) + (d > 0 ? 1.0 : -1.0) / steps, 0.0, 1.0));
        invalid ();
        e.consumed = true;
    }
};

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

Editor::Editor (Controller* c) : pk::EditorBase (c, kWidth, kHeight), ctl (c), lockHost (std::make_unique<GainLockHost> (this)) {}

Editor::~Editor () = default;

void Editor::onClose ()
{
    tailDisplays.reset ();
    view = nullptr;
    lpResKnob = nullptr;
    hpDriveKnob = lpDriveKnob = drivePosView = nullptr;
}

void Editor::buildUI (CFrame* f)
{
    auto* root = new Background (CRect (0, 0, kWidth, kHeight));
    f->addView (root);
    root->addView (new Label (CRect (12, 6, 200, 28), "para", 14.0, true));
    root->addView (new pk::PresetBar (CRect (440, 6, 636, 28), ctl));
    auto* helpBtn = new ActionButton (CRect (644, 6, 666, 28), "?", [this] { setTooltipsEnabled (!tooltipsEnabled ()); },
                                      [this] { return tooltipsEnabled (); });
    helpBtn->setTooltipText ("Show or hide these help tooltips.");
    root->addView (helpBtn);
    root->addView (new ActionButton (CRect (672, 6, 752, 28), "Menu", [this] { showMenu (CPoint (672, 28)); }));

    // the gains, the display's handles and the locks go through lockHost: a locked gain stops at 0 dB
    GainLockHost* lh = lockHost.get ();
    view = new FilterView (CRect (kViewLeft, kViewTop, kViewRight, kViewBottom), lh, [c = ctl] () -> Meters* {
        auto* s = c->getShared ();
        return s ? &s->meters : nullptr;
    });
    view->setTooltipText (help::kDisplay);
    root->addView (view);
    // on the display, under the envelope meter: what dragging a handle up or down moves
    bind (root, new Toggle (CRect (kViewRight - 90, kViewTop + 22, kViewRight - 8, kViewTop + 40), this, kDragGain, "Drag Gain"));

    // first row: the two filters and the split, as sections of a Live device
    auto section = [&] (const CRect& r, const char* title) {
        auto* p = new Panel (r, title);
        root->addView (p);
        return p;
    };
    // each filter's gain with its Gain Lock under it (locked: the gain stops at 0 dB)
    auto* hpP = section (CRect (8, kRow1Top, 190, kRow1Bottom), "HIGH-PASS");
    bind (hpP, new Knob (knobRect (8, 22), this, kHpFreq, "Freq"));
    bind (hpP, new Knob (knobRect (66, 22), this, kHpRes, "Res"));
    bind (hpP, new Knob (knobRect (124, 22), lh, kHpGain, "Gain"));
    bind (hpP, new Toggle (CRect (128, 94, 176, 112), lh, kHpGainLock, "Lock"));
    auto* lpP = section (CRect (196, kRow1Top, 378, kRow1Bottom), "LOW-PASS");
    bind (lpP, new Knob (knobRect (8, 22), this, kLpFreq, "Freq"));
    lpResKnob = bind (lpP, new Knob (knobRect (66, 22), this, kLpRes, "Res"));
    bind (lpP, new Knob (knobRect (124, 22), lh, kLpGain, "Gain"));
    bind (lpP, new Toggle (CRect (128, 94, 176, 112), lh, kLpGainLock, "Lock"));
    // the slopes, one per filter, and the resonance link
    auto* spP = section (CRect (384, kRow1Top, 752, kRow1Bottom), "SPLIT");
    bind (spP, new ScrollChoice (CRect (10, 22, 100, 56), this, kHpSlope, "HP Slope"));
    bind (spP, new ScrollChoice (CRect (10, 60, 100, 94), this, kLpSlope, "LP Slope"));
    bind (spP, new Toggle (CRect (10, 98, 100, 116), this, kResLink, "Link Res"));
    const uint32_t splitIds[4] = {kSplit, kEnvAmount, kEnvAttack, kEnvDecay};
    const char* splitNames[4] = {"Split", "Env", "Attack", "Decay"};
    for (int i = 0; i < 4; ++i)
        bind (spP, new Knob (knobRect (116 + i * 62, 30), this, splitIds[i], splitNames[i], i < 2));

    // second row: movement and output
    auto* outP = section (CRect (8, kRow2Top, 752, kRow2Top + 102), "OUTPUT");
    bind (outP, new Knob (knobRect (10, 22), this, kDryWet));
    bind (outP, new Knob (knobRect (72, 22), this, kOutput, nullptr, true));
    outP->addView (new Label (CRect (150, 24, 290, 38), "Movement", 10.5, false, 1));
    bind (outP, new Segmented (CRect (150, 42, 290, 62), this, kMovement, {"Free", "Vocal"}));
    bind (outP, new Knob (knobRect (300, 22), this, kDipStart));
    bind (outP, new Knob (knobRect (362, 22), this, kFade));
    bind (outP, new Knob (knobRect (440, 22), this, kLpFloor));
    // the drives, one in each filter's branch: on and how hard, and (for both) before or after the filters
    outP->addView (new Label (CRect (516, 24, 606, 38), "Drive", 10.5, false, 1));
    drivePosView = bind (outP, new Segmented (CRect (516, 42, 606, 62), this, kDrivePos, {"Pre", "Post"}));
    bind (outP, new Toggle (CRect (516, 70, 558, 88), this, kHpDriveOn, "HP"));
    bind (outP, new Toggle (CRect (564, 70, 606, 88), this, kLpDriveOn, "LP"));
    hpDriveKnob = bind (outP, new Knob (knobRect (614, 22), this, kHpDrive, "HP Drive"));
    lpDriveKnob = bind (outP, new Knob (knobRect (676, 22), this, kLpDrive, "LP Drive"));

    // the saturator at the end of the chain, with Smacheratr's displays above its controls
    auto* tailPanel = addTailPanel (root, CRect (8, kRow2Top + 108, 752, kRow2Top + 188 + smacheratr::TailDisplays::kHeight), kTailBase,
                                    kTailExtBase, kTailExt2Base, kTailExt3Base);
    tailDisplays = std::make_unique<smacheratr::TailDisplays> (this, smacheratr::TailBases {kTailBase, kTailExtBase, kTailExt2Base, kTailExt3Base},
                                                               [c = ctl] { auto* s = c->getShared (); return s ? s->sampleRate.load () : 48000.0; },
                                                               [c = ctl] () -> const smacheratr::Meters* { auto* s = c->getShared (); return s ? &s->tailMeters : nullptr; });
    tailDisplays->add (tailPanel, CRect (10, 24, 734, 24 + smacheratr::TailDisplays::kHeight - 22));
    tailDisplays->onBandPicked ([this] (int k) { showTailBand (k); });

    applyParamTooltips (&help::forParam);
    updateLooks ();
    idle ();
}

void Editor::updateLooks ()
{
    if (lpResKnob)
        lpResKnob->setEnabledLook (plainValue (kResLink) < 0.5);
    const bool hpOn = plainValue (kHpDriveOn) >= 0.5, lpOn = plainValue (kLpDriveOn) >= 0.5;
    if (hpDriveKnob)
        hpDriveKnob->setEnabledLook (hpOn);
    if (lpDriveKnob)
        lpDriveKnob->setEnabledLook (lpOn);
    if (drivePosView)
        drivePosView->setEnabledLook (hpOn || lpOn);
}

void Editor::paramChanged (uint32_t id)
{
    pk::EditorBase::paramChanged (id);
    if (tailDisplays)
        tailDisplays->paramChanged (id);
    if (view)
        view->invalid ();
    if (id == kResLink || id == kHpDriveOn || id == kLpDriveOn)
        updateLooks ();
}

void Editor::idle ()
{
    if (tailDisplays)
        tailDisplays->idle ();
    if (view)
        view->idle ();
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

} // namespace para
