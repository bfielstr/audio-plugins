#include "Editor.h"

#include "FilterView.h"
#include "Help.h"
#include "plugin/Controller.h"

#include "pluginkit/ui/Theme.h"
#include "pluginkit/vst/PresetBar.h"

#include "vstgui/lib/cframe.h"
#include "vstgui/lib/controls/coptionmenu.h"

#include <cmath>
#include <cstdio>

namespace para {

using namespace VSTGUI;
using pk::ActionButton;
using pk::Knob;
using pk::Label;
using pk::Panel;
using pk::Segmented;
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
        ctx->setFillColor (pk::theme::kBackground);
        ctx->drawRect (CRect (0, 0, getViewSize ().getWidth (), getViewSize ().getHeight ()), kDrawFilled);
        ctx->setFillColor (pk::theme::kHeader);
        ctx->drawRect (CRect (0, 0, getViewSize ().getWidth (), 34), kDrawFilled);
    }
};
} // namespace

Editor::Editor (Controller* c) : pk::EditorBase (c, kWidth, kHeight), ctl (c) {}

void Editor::onClose ()
{
    view = nullptr;
    lpResKnob = nullptr;
}

void Editor::buildUI (CFrame* f)
{
    auto* root = new Background (CRect (0, 0, kWidth, kHeight));
    f->addView (root);
    root->addView (new Label (CRect (12, 6, 200, 28), "PARA", 14.0, true));
    root->addView (new pk::PresetBar (CRect (440, 6, 636, 28), ctl));
    auto* helpBtn = new ActionButton (CRect (644, 6, 666, 28), "?", [this] { setTooltipsEnabled (!tooltipsEnabled ()); },
                                      [this] { return tooltipsEnabled (); });
    helpBtn->setTooltipText ("Show or hide these help tooltips.");
    root->addView (helpBtn);
    root->addView (new ActionButton (CRect (672, 6, 752, 28), "Menu", [this] { showMenu (CPoint (672, 28)); }));

    view = new FilterView (CRect (kViewLeft, kViewTop, kViewRight, kViewBottom), this, [c = ctl] () -> Meters* {
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
    auto* hpP = section (CRect (8, 298, 190, 404), "HIGH-PASS");
    bind (hpP, new Knob (knobRect (8, 22), this, kHpFreq, "Freq"));
    bind (hpP, new Knob (knobRect (66, 22), this, kHpRes, "Res"));
    bind (hpP, new Knob (knobRect (124, 22), this, kHpGain, "Gain"));
    auto* lpP = section (CRect (196, 298, 378, 404), "LOW-PASS");
    bind (lpP, new Knob (knobRect (8, 22), this, kLpFreq, "Freq"));
    lpResKnob = bind (lpP, new Knob (knobRect (66, 22), this, kLpRes, "Res"));
    bind (lpP, new Knob (knobRect (124, 22), this, kLpGain, "Gain"));
    auto* spP = section (CRect (384, 298, 752, 404), "SPLIT");
    spP->addView (new Label (CRect (10, 24, 100, 38), "Slope", 10.5, false, 1));
    bind (spP, new Segmented (CRect (10, 42, 100, 62), this, kSlope, {"12", "18", "24"}));
    bind (spP, new Toggle (CRect (10, 70, 100, 88), this, kResLink, "Link Res"));
    const uint32_t splitIds[4] = {kSplit, kEnvAmount, kEnvAttack, kEnvDecay};
    const char* splitNames[4] = {"Split", "Env", "Attack", "Decay"};
    for (int i = 0; i < 4; ++i)
        bind (spP, new Knob (knobRect (116 + i * 62, 22), this, splitIds[i], splitNames[i], i < 2));

    // second row: movement and output
    auto* outP = section (CRect (8, 410, 752, 512), "OUTPUT");
    bind (outP, new Knob (knobRect (10, 22), this, kDryWet));
    bind (outP, new Knob (knobRect (72, 22), this, kOutput, nullptr, true));
    outP->addView (new Label (CRect (150, 24, 290, 38), "Movement", 10.5, false, 1));
    bind (outP, new Segmented (CRect (150, 42, 290, 62), this, kMovement, {"Free", "Vocal"}));
    bind (outP, new Toggle (CRect (296, 42, 358, 62), this, kLiquid, "Liquid"));

    addTailPanel (root, CRect (8, 518, 752, 598), kTailBase);

    applyParamTooltips (&help::forParam);
    updateLooks ();
    idle ();
}

void Editor::updateLooks ()
{
    if (lpResKnob)
        lpResKnob->setEnabledLook (plainValue (kResLink) < 0.5);
}

void Editor::paramChanged (uint32_t id)
{
    pk::EditorBase::paramChanged (id);
    if (view)
        view->invalid ();
    if (id == kResLink)
        updateLooks ();
}

void Editor::idle ()
{
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
    menu->popup (frame, where, [this, sizes, menu] (COptionMenu* m) {
        const int32_t r = m->getLastResult ();
        if (r >= 0 && r < (int32_t)sizes.size ())
            resizeTo (sizes[(size_t)r]);
    });
}

} // namespace para
