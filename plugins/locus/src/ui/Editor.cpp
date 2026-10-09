#include "Editor.h"

#include "Help.h"
#include "SpectrumView.h"
#include "plugin/Controller.h"

#include "pluginkit/vst/Clipboard.h"
#include "pluginkit/ui/Theme.h"
#include "pluginkit/vst/PresetBar.h"

#include "vstgui/lib/cframe.h"
#include "vstgui/lib/controls/coptionmenu.h"

#include <cmath>
#include <cstdio>
#include <vector>

namespace locus {

using namespace VSTGUI;
using pk::ActionButton;
using pk::Knob;
using pk::Label;
using pk::Panel;
using pk::Segmented;
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
    spectrum = nullptr;
    latencyLabel = nullptr; // (the Basic page has none)
    tail.reset ();
}

void Editor::buildUI (CFrame* f)
{
    auto* root = new Background (CRect (0, 0, kWidth, kHeight));
    f->addView (root);
    root->addView (new Label (CRect (12, 6, 200, 28), "locus", 14.0, true));
    latencyLabel = new Label (CRect (200, 6, 340, 28), "", 10.5);
    root->addView (new pk::PresetBar (CRect (440, 6, 636, 28), ctl));
    latencyLabel->setDim (true);
    root->addView (latencyLabel);
    auto* helpBtn = new ActionButton (CRect (644, 6, 666, 28), "?", [this] { setTooltipsEnabled (!tooltipsEnabled ()); },
                                      [this] { return tooltipsEnabled (); });
    helpBtn->setTooltipText ("Show or hide the floating help tooltips (the info box at the bottom shows the same help either way).");
    root->addView (helpBtn);
    root->addView (new ActionButton (CRect (672, 6, 752, 28), "Menu", [this] { showMenu (layoutPoint (CPoint (672, 28))); }));

    spectrum = new SpectrumView (CRect (8, 40, 752, 300), this, ctl);
    pk::setHelp (spectrum, "Spectrum", help::kSpectrum);
    root->addView (spectrum);

    auto* p = new Panel (CRect (8, 308, 752, 432), "FOCUS");
    root->addView (p);
    p->addView (new Label (CRect (14, 24, 164, 38), "Mode", 10.5, false, 1));
    bind (p, new Segmented (CRect (14, 42, 164, 66), this, kMode, {"Punchy", "Smooth"}));
    bind (p, new Knob (CRect (184, 18, 264, 118), this, kContrast, nullptr, true));
    bind (p, new Knob (CRect (284, 30, 340, 94), this, kGain, nullptr, true));
    bind (p, new Knob (CRect (360, 30, 416, 94), this, kLowFreq));
    bind (p, new Knob (CRect (436, 30, 492, 94), this, kHighFreq));
    bind (p, new Toggle (CRect (516, 50, 580, 70), this, kSolo, "Solo"));
    bind (p, new Knob (CRect (600, 30, 656, 94), this, kOutput, nullptr, true));

    // the saturator at the end of the chain, with Smacheratr's displays above its controls
    tail = makeTail ();
    tail->add (root, layoutRegion ("tail", CRect (8, 440, 752, 440 + smacheratr::TailPanel::kOpenHeight)));

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
    // the focus's character (Mode) and strength (Contrast), and the range it works on (Low Freq, High
    // Freq); Output
    using namespace pk::basic;
    Spec s;
    s.title = "locus";
    s.capture = [c = ctl] () -> const pk::CaptureBuffer* { auto* sh = c->getShared (); return sh ? &sh->capture : nullptr; };
    s.displayHeight = 220;
    s.display = [this] (const CRect& r) -> CView* {
        spectrum = new SpectrumView (r, this, ctl);
        pk::setHelp (spectrum, "Spectrum", help::kSpectrum);
        return spectrum;
    };
    s.rows = {{segmented (kMode, "Mode", {"Punchy", "Smooth"})}, {knob (kContrast, {}, true), knob (kLowFreq), knob (kHighFreq)}};
    s.output = {knob (kOutput, {}, true)};
    smacheratr::TailPanel::addToBasic (s, this, tailBases (), tail, [this] { return makeTail (); });
    s.menu = [this] (CPoint p) { showMenu (p); };
    s.help = &help::forParam;
    s.advancedSwitch = CRect (348, 6, 432, 28);
    return s;
}

void Editor::paramChanged (uint32_t id)
{
    pk::EditorBase::paramChanged (id);
    if (tail)
        tail->paramChanged (id);
    if (spectrum)
        spectrum->invalid ();
}

void Editor::idle ()
{
    if (tail)
        tail->idle ();
    if (spectrum)
        spectrum->idle ();
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
    // the spectrum beside the focus controls; the end saturator under them
    s.panels = {
        {"spectrum", "spectrum", {8, 40, 752, 300}, 0},
        {"focus", "", {8, 308, 752, 432}, 0},
        {"tail", "end of the chain", {8, 440, 752, 440 + smacheratr::TailPanel::kOpenHeight}, 1, -1, true},
    };
    return s;
}

} // namespace locus
