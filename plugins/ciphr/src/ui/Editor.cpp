#include "Editor.h"

#include "CipherView.h"
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

namespace ciphr {

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

// a panel's knobs in a row (bipolar: drawn from the centre)
struct KnobDef
{
    uint32_t id;
    bool bipolar = false;
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
    // arranged: as tall as the columns of two panels beside it (their blocks' strips and the gap between)
    const double arrangedH = 2.0 * kRowH + pk::layout::kGap + pk::layout::kStrip;
    return arranged ? CRect (8, kDisplayTop, 8 + kArrangedDisplayW, kDisplayTop + arrangedH) : CRect (8, kDisplayTop, kWidth - 8, kDisplayBottom);
}

void Editor::buildUI (CFrame* f)
{
    auto* root = new Background (CRect (0, 0, kWidth, kHeight));
    f->addView (root);
    root->addView (new Label (CRect (12, 6, 200, 28), "ciphr", 14.0, true));
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

    display = new CipherView (displayRect (arrangedLayout ()), this,
                              [c = ctl] () -> const Meters* { auto* s = c->getShared (); return s ? &s->meters : nullptr; });
    pk::setHelp (display, "Cluster and taps", help::kCipherView);
    root->addView (display);

    auto knobs = [this] (Panel* panel, std::initializer_list<KnobDef> defs) {
        int i = 0;
        for (const KnobDef& d : defs)
        {
            const double x = kKnobLeft + kKnobStep * i++;
            bind (panel, new Knob (CRect (x, kKnobTop, x + kKnobW, kKnobTop + kKnobH), this, d.id, nullptr, d.bipolar));
        }
    };

    auto* gen = new Panel (CRect (kGenLeft, kRow1, kGenRight, kRow1 + kRowH), "GENERATOR");
    root->addView (gen);
    knobs (gen, {{kTimbre}, {kCross, true}, {kCharacter}, {kVariant}, {kDrift}, {kTune, true}});

    auto* input = new Panel (CRect (kInputLeft, kRow1, kInputRight, kRow1 + kRowH), "INPUT");
    root->addView (input);
    bind (input, new Segmented (CRect (kPathLeft, kPathTop, kPathLeft + kPathW, kPathTop + kPathH), this, kInputPath, {"Direct", "Voices"}));
    bind (input, new Knob (CRect (136, kKnobTop, 136 + kKnobW, kKnobTop + kKnobH), this, kInput));

    auto* filter = new Panel (CRect (kFilterLeft, kRow1, kFilterRight, kRow1 + kRowH), "FILTER");
    root->addView (filter);
    knobs (filter, {{kCutoff}, {kResonance}, {kFilterType}, {kKeyTrack}, {kEnvAmount, true}});

    auto* amp = new Panel (CRect (kAmpLeft, kRow2, kAmpRight, kRow2 + kRowH), "AMP ENVELOPE");
    root->addView (amp);
    knobs (amp, {{kAttack}, {kDecay}, {kSustain}, {kRelease}, {kVelocity}});

    auto* fenv = new Panel (CRect (kFenvLeft, kRow2, kFenvRight, kRow2 + kRowH), "FILTER ENVELOPE");
    root->addView (fenv);
    knobs (fenv, {{kFilterAttack}, {kFilterDecay}, {kFilterSustain}, {kFilterRelease}});

    auto* proc = new Panel (CRect (kProcLeft, kRow2, kProcRight, kRow2 + kRowH), "PROCESSOR");
    root->addView (proc);
    knobs (proc, {{kSpace}, {kLength}, {kMovement}, {kRegen, true}, {kShift, true}});

    // the output beside both rows (its knobs on their rows)
    auto* out = new Panel (CRect (kOutLeft, kRow1, kOutRight, kRow2 + kRowH), "OUTPUT");
    root->addView (out);
    bind (out, new Knob (CRect (18, kKnobTop, 18 + kKnobW, kKnobTop + kKnobH), this, kBlend));
    bind (out, new Knob (CRect (18, kKnobTop + kRow2 - kRow1, 18 + kKnobW, kKnobTop + kKnobH + kRow2 - kRow1), this, kOutput, nullptr, true));

    // the saturator at the end of the chain, with Smacheratr's displays above its controls
    tail = std::make_unique<smacheratr::TailPanel> (this, smacheratr::TailBases {kTailBase, kTailExtBase, kTailExt2Base, kTailExt3Base, kTailExt4Base},
                                                    [c = ctl] { auto* s = c->getShared (); return s ? s->sampleRate.load () : 48000.0; },
                                                    [c = ctl] () -> const smacheratr::Meters* { auto* s = c->getShared (); return s ? &s->tailMeters : nullptr; });
    tail->add (root, layoutRegion ("tail", CRect (8, kTailTop, kWidth - 8, kTailTop + smacheratr::TailPanel::kOpenHeight)));

    applyParamTooltips (&help::forParam);
    idle ();
}

void Editor::paramChanged (uint32_t id)
{
    pk::EditorBase::paramChanged (id);
    if (tail)
        tail->paramChanged (id);
    if (display && (id == kVariant || id == kCharacter || id == kSpace || id == kLength || id == kRegen || id == kShift || id == kTimbre))
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
    // Wide: the display, then columns of two panels (generator over amp envelope, input over filter
    // envelope, filter over processor) and the output; the end saturator in a row of its own under them
    s.panels = {
        {"display", "cluster and taps", {d.left, d.top, d.right, d.bottom}, 0},
        {"generator", "", {kGenLeft, kRow1, kGenRight, kRow1 + kRowH}, 0, 0},
        {"amp", "", {kAmpLeft, kRow2, kAmpRight, kRow2 + kRowH}, 0, 0},
        {"input", "", {kInputLeft, kRow1, kInputRight, kRow1 + kRowH}, 0, 1},
        {"fenv", "", {kFenvLeft, kRow2, kFenvRight, kRow2 + kRowH}, 0, 1},
        {"filter", "", {kFilterLeft, kRow1, kFilterRight, kRow1 + kRowH}, 0, 2},
        {"processor", "", {kProcLeft, kRow2, kProcRight, kRow2 + kRowH}, 0, 2},
        {"output", "", {kOutLeft, kRow1, kOutRight, kRow2 + kRowH}, 0},
        {"tail", "end of the chain", {8, kTailTop, kWidth - 8, kTailTop + smacheratr::TailPanel::kOpenHeight}, 1, -1, true},
    };
    return s;
}

} // namespace ciphr
