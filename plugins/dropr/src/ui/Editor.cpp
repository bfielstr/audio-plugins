#include "Editor.h"

#include "ShapeView.h"
#include "Help.h"
#include "plugin/Controller.h"

#include "pluginkit/vst/Clipboard.h"
#include "pluginkit/ui/Theme.h"
#include "pluginkit/vst/PresetBar.h"

#include "vstgui/lib/cframe.h"
#include "vstgui/lib/controls/coptionmenu.h"

#include <cmath>
#include <cstdio>
#include <vector>

namespace dropr {

using namespace VSTGUI;
using pk::ActionButton;
using pk::Knob;
using pk::Label;
using pk::Panel;

namespace {
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
    display = nullptr;
    latencyLabel = nullptr;
    tailDisplays.reset ();
}

void Editor::buildUI (CFrame* f)
{
    auto* root = new Background (CRect (0, 0, kWidth, kHeight));
    f->addView (root);
    root->addView (new Label (CRect (12, 6, 200, 28), "dropr", 14.0, true));
    latencyLabel = new Label (CRect (200, 6, 430, 28), "", 10.5);
    root->addView (new pk::PresetBar (CRect (440, 6, 636, 28), ctl));
    latencyLabel->setDim (true);
    root->addView (latencyLabel);
    auto* helpBtn = new ActionButton (CRect (644, 6, 666, 28), "?", [this] { setTooltipsEnabled (!tooltipsEnabled ()); },
                                      [this] { return tooltipsEnabled (); });
    helpBtn->setTooltipText ("Show or hide these help tooltips.");
    root->addView (helpBtn);
    root->addView (new ActionButton (CRect (672, 6, 752, 28), "Menu", [this] { showMenu (CPoint (672, 28)); }));

    display = new ShapeView (CRect (8, 40, 752, 300), this,
                             [c = ctl] () -> const Meters* { auto* s = c->getShared (); return s ? &s->meters : nullptr; });
    display->setTooltipText (help::kDisplay);
    root->addView (display);

    // the shape: how deep, how long, how early
    auto* shapePanel = new Panel (CRect (8, 308, 360, 424), "SHAPE");
    root->addView (shapePanel);
    bind (shapePanel, new Knob (CRect (24, 12, 104, 112), this, kDepth));
    bind (shapePanel, new Knob (CRect (150, 28, 206, 92), this, kLength));
    bind (shapePanel, new Knob (CRect (240, 28, 296, 92), this, kPre));

    // what counts as a hit
    auto* hitsPanel = new Panel (CRect (368, 308, 560, 424), "HITS");
    root->addView (hitsPanel);
    bind (hitsPanel, new Knob (CRect (24, 28, 80, 92), this, kSensitivity));
    bind (hitsPanel, new Knob (CRect (104, 28, 160, 92), this, kRetrigger));

    auto* out = new Panel (CRect (568, 308, 752, 424), "OUTPUT");
    root->addView (out);
    bind (out, new Knob (CRect (24, 28, 80, 92), this, kMix));
    bind (out, new Knob (CRect (104, 28, 160, 92), this, kOutput, nullptr, true));

    // the saturator at the end of the chain, with Smacheratr's displays above its controls
    auto* tailPanel = addTailPanel (root, CRect (8, 432, 752, 506 + smacheratr::TailDisplays::kHeight), kTailBase, kTailExtBase, kTailExt2Base);
    tailDisplays = std::make_unique<smacheratr::TailDisplays> (this, kTailBase, kTailExtBase, kTailExt2Base,
                                                               [c = ctl] { auto* s = c->getShared (); return s ? s->sampleRate.load () : 48000.0; },
                                                               [c = ctl] () -> const smacheratr::Meters* { auto* s = c->getShared (); return s ? &s->tailMeters : nullptr; });
    tailDisplays->add (tailPanel, CRect (10, 24, 734, 24 + smacheratr::TailDisplays::kHeight - 22));
    tailDisplays->onBandPicked ([this] (int k) { showTailBand (k); });

    applyParamTooltips (&help::forParam);
    idle ();
}

void Editor::paramChanged (uint32_t id)
{
    pk::EditorBase::paramChanged (id);
    if (tailDisplays)
        tailDisplays->paramChanged (id);
    if (display && id < kTailBase)
        display->invalid ();
}

void Editor::idle ()
{
    if (tailDisplays)
        tailDisplays->idle ();
    if (display)
        display->idle ();
    if (latencyLabel)
        if (auto* s = ctl->getShared ())
        {
            char buf[64];
            std::snprintf (buf, sizeof (buf), "Latency %d samples", s->latency.load ());
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
    menu->popup (frame, where, [this, sizes, menu, settingsAt] (COptionMenu* m) {
        const int32_t r = m->getLastResult ();
        if (settingsMenuPicked (r, settingsAt))
            return;
        if (r >= 0 && r < (int32_t)sizes.size ())
            resizeTo (sizes[(size_t)r]);
    });
}

} // namespace dropr
