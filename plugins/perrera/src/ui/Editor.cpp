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

namespace perrera {

using namespace VSTGUI;
using pk::ActionButton;
using pk::Knob;
using pk::Label;
using pk::NumberBox;
using pk::Panel;
using pk::Segmented;

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

void Editor::onClose () { view = nullptr; }

void Editor::buildUI (CFrame* f)
{
    auto* root = new Background (CRect (0, 0, kWidth, kHeight));
    f->addView (root);
    root->addView (new Label (CRect (12, 6, 200, 28), "PERRERA", 14.0, true));
    root->addView (new pk::PresetBar (CRect (440, 6, 636, 28), ctl));
    auto* helpBtn = new ActionButton (CRect (644, 6, 666, 28), "?", [this] { setTooltipsEnabled (!tooltipsEnabled ()); },
                                      [this] { return tooltipsEnabled (); });
    helpBtn->setTooltipText ("Show or hide these help tooltips.");
    root->addView (helpBtn);
    root->addView (new ActionButton (CRect (672, 6, 752, 28), "Menu", [this] { showMenu (CPoint (672, 28)); }));

    view = new FilterView (CRect (kViewLeft, kViewTop, kViewRight, kViewBottom), this, ctl);
    view->setTooltipText (help::kDisplay);
    root->addView (view);

    auto* p = new Panel (CRect (8, 308, 752, 432), "FILTERS");
    root->addView (p);
    p->addView (new Label (CRect (14, 24, 92, 38), "Slope", 10.5, false, 1));
    bind (p, new Segmented (CRect (14, 42, 92, 64), this, kSlope, {"12", "24"}));
    const uint32_t ids[11] = {kHpFreq, kHpRes, kLpFreq, kLpRes, kSplit, kEnvAmount, kEnvAttack, kEnvDecay, kKey, kDryWet, kOutput};
    const bool bipolar[11] = {false, false, false, false, true, true, false, false, false, false, true};
    for (int i = 0; i < 11; ++i)
        bind (p, new Knob (knobRect (100 + i * 58, 22), this, ids[i], nullptr, bipolar[i]));
    p->addView (new Label (CRect (14, 96, 84, 110), "Transpose", 9.5, false, 1));
    bind (p, new NumberBox (CRect (14, 100, 84, 118), this, kTranspose));
    p->addView (new Label (CRect (92, 96, 162, 110), "Bend", 9.5, false, 1));
    bind (p, new NumberBox (CRect (92, 100, 162, 118), this, kPbRange));
    p->addView (new Label (CRect (170, 96, 240, 110), "Root", 9.5, false, 1));
    bind (p, new NumberBox (CRect (170, 100, 240, 118), this, kRoot));

    applyParamTooltips (&help::forParam);
    idle ();
}

void Editor::paramChanged (uint32_t id)
{
    pk::EditorBase::paramChanged (id);
    if (view)
        view->invalid ();
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

} // namespace perrera
