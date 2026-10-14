#include "Editor.h"

#include "CurveView.h"
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

namespace sweepr {

using namespace VSTGUI;
using pk::ActionButton;
using pk::Choice;
using pk::Knob;
using pk::Label;
using pk::NumberBox;
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
constexpr double kLfoKnobX[2] = {134.0, 208.0}; // Depth, Phase
} // namespace

Editor::Editor (Controller* c) : pk::EditorBase (c, kWidth, kHeight), ctl (c) {}

void Editor::onClose ()
{
    display = nullptr;
    latencyLabel = nullptr;
    for (int b = 0; b < kBands; ++b)
        divViews[b] = rateViews[b] = nullptr;
    for (auto*& v : bLfoViews)
        v = nullptr;
    bShape = nullptr;
    tail.reset ();
}

CRect Editor::displayRect (bool arranged) const
{
    // (arranged: as tall as the panels' row, from the top, so its region holds no other panel)
    return arranged ? CRect (8, kTop, 8 + kArrangedDisplayW, kTop + kRowBottom - kRowTop) : CRect (8, kTop, kWidth - 8, kDisplayBottom);
}

CurveView* Editor::makeCurve (const CRect& r)
{
    display = new CurveView (r, this, [c = ctl] () -> const Meters* { auto* s = c->getShared (); return s ? &s->meters : nullptr; });
    pk::setHelp (display, "Curve", help::kCurveView);
    return display;
}

void Editor::buildUI (CFrame* f)
{
    auto* root = new Background (CRect (0, 0, kWidth, kHeight));
    f->addView (root);
    root->addView (new Label (CRect (12, 6, 200, 28), "sweepr", 14.0, true));
    latencyLabel = new Label (CRect (200, 6, 340, 28), "", 10.5);
    latencyLabel->setDim (true);
    root->addView (latencyLabel);
    const double hx = kWidth - 320; // the header's controls at the right, as in the other plug-ins
    root->addView (new pk::PresetBar (CRect (hx, 6, hx + 196, 28), ctl));
    auto* helpBtn = new ActionButton (CRect (hx + 204, 6, hx + 226, 28), "?", [this] { setTooltipsEnabled (!tooltipsEnabled ()); },
                                      [this] { return tooltipsEnabled (); });
    helpBtn->setTooltipText ("Show or hide the floating help tooltips (the info box at the bottom shows the same help either way).");
    root->addView (helpBtn);
    root->addView (new ActionButton (CRect (hx + 232, 6, hx + 312, 28), "Menu", [this, hx] { showMenu (layoutPoint (CPoint (hx + 232, 28))); }));

    root->addView (makeCurve (displayRect (arrangedLayout ())));

    // the bands: the bell's Freq, Q and Gain; under them the LFO
    for (int b = 0; b < kBands; ++b)
    {
        auto id = [b] (uint32_t f) { return bandParam (b, f); };
        auto* p = new Panel (CRect (kBandLeft[b], kRowTop, kBandLeft[b] + kBandW, kRowBottom), b == 0 ? "BAND A" : "BAND B");
        root->addView (p);
        for (int k = 0; k < 3; ++k)
        {
            const uint32_t f = k == 0 ? kFreq : (k == 1 ? kQ : kGain);
            bind (p, new Knob (CRect (kKnobX[k], kKnobTop, kKnobX[k] + kKnobW, kKnobTop + kKnobH), this, id (f), nullptr, f == kGain));
        }
        bind (p, new Toggle (CRect (kColLeft, kLfoTop - 22, kColLeft + kColW, kLfoTop - 4), this, lfoOnParam (b), "LFO"));
        auto row = [] (int i) { return CRect (kColLeft, kLfoTop + i * kRowStep, kColLeft + kColW, kLfoTop + i * kRowStep + kRowH); };
        auto* sync = bind (p, new Toggle (row (0), this, id (kLfoSync), "Sync"));
        divViews[b] = bind (p, new Choice (row (1), this, id (kLfoDiv)));
        rateViews[b] = bind (p, new NumberBox (row (1), this, id (kLfoRate)));
        auto* shape = bind (p, new Choice (row (2), this, id (kLfoShape)));
        bind (p, new Knob (CRect (kLfoKnobX[0], kLfoTop, kLfoKnobX[0] + kKnobW, kLfoTop + kKnobH), this, id (kLfoDepth), nullptr, true));
        bind (p, new Knob (CRect (kLfoKnobX[1], kLfoTop, kLfoKnobX[1] + kKnobW, kLfoTop + kKnobH), this, id (kLfoPhase)));
        if (b == 1)
        {
            bind (p, new Toggle (row (3), this, kLink, "Link"));
            bLfoViews[0] = sync;
            bLfoViews[1] = static_cast<pk::ParamView*> (divViews[b]);
            bLfoViews[2] = static_cast<pk::ParamView*> (rateViews[b]);
            bShape = shape;
        }
    }

    // TAPE: the switch, then Drive, Speed and Compression
    auto* tape = new Panel (CRect (kTapeLeft, kRowTop, kTapeRight, kRowBottom), "TAPE");
    root->addView (tape);
    bind (tape, new Toggle (CRect (16, 28, kTapeRight - kTapeLeft - 16, 48), this, kTapeOn, "Tape"));
    bind (tape, new Knob (CRect (kTapeX[0], kTapeRow[0], kTapeX[0] + kTapeKnobW, kTapeRow[0] + kKnobH), this, kDrive));
    bind (tape, new Knob (CRect (kTapeX[1], kTapeRow[0], kTapeX[1] + kTapeKnobW, kTapeRow[0] + kKnobH), this, kSpeed));
    const double cx = 0.5 * (kTapeRight - kTapeLeft - kTapeKnobW);
    bind (tape, new Knob (CRect (cx, kTapeRow[1], cx + kTapeKnobW, kTapeRow[1] + kKnobH), this, kCompression));

    // OUTPUT: Mix and Output
    auto* out = new Panel (CRect (kOutLeft, kRowTop, kOutRight, kRowBottom), "OUTPUT");
    root->addView (out);
    const double ox = 0.5 * (kOutRight - kOutLeft - kKnobW);
    bind (out, new Knob (CRect (ox, kKnobTop, ox + kKnobW, kKnobTop + kKnobH), this, kMix));
    bind (out, new Knob (CRect (ox, kLfoTop, ox + kKnobW, kLfoTop + kKnobH), this, kOutput, nullptr, true));

    // the saturator at the end of the chain (off in a new instance: the tape stage is the saturator)
    tail = makeTail ();
    tail->add (root, layoutRegion ("tail", CRect (8, kTailTop, kWidth - 8, kTailTop + smacheratr::TailPanel::kOpenHeight)));

    applyParamTooltips (&help::forParam);
    updateRates ();
    idle ();
}

void Editor::updateRates ()
{
    // each band shows the rate it uses: Division while synced, Rate while free; B's own LFO settings dim
    // while Link makes it follow A's
    const bool link = plainValue (kLink) >= 0.5;
    for (int b = 0; b < kBands; ++b)
    {
        if (!divViews[b] || !rateViews[b])
            continue;
        const bool sync = plainValue (bandParam (b, kLfoSync)) >= 0.5;
        divViews[b]->setVisible (sync);
        rateViews[b]->setVisible (!sync);
    }
    for (auto* v : bLfoViews)
        if (v)
            v->setEnabledLook (!link);
    if (bShape)
        bShape->setEnabledLook (!link);
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
    // the two bands' Gain and how far each LFO sweeps them, how hard the tape is driven and the blend; Output
    using namespace pk::basic;
    Spec s;
    s.title = "sweepr";
    s.capture = [c = ctl] () -> const pk::CaptureBuffer* { auto* sh = c->getShared (); return sh ? &sh->capture : nullptr; };
    s.displayHeight = 200;
    s.display = [this] (const CRect& r) -> CView* { return makeCurve (r); };
    s.rows = {{knob (bandParam (0, kGain), "A Gain", true), knob (bandParam (1, kGain), "B Gain", true),
               knob (bandParam (0, kLfoDepth), "A Depth", true), knob (bandParam (1, kLfoDepth), "B Depth", true)},
              {knob (kDrive), knob (kMix)}};
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
    if ((id < kLink || id == kLfoOnA || id == kLfoOnB) && display)
        display->invalid ();
    if (id == kLink || id == bandParam (0, kLfoSync) || id == bandParam (1, kLfoSync))
        updateRates ();
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
    // Wide: the curve (narrower, as tall as the panels), the bands, TAPE and OUTPUT side by side; the end
    // saturator in a row of its own
    s.panels = {
        {"display", "display", {d.left, d.top, d.right, d.bottom}, 0},
        {"band-a", "", {kBandLeft[0], kRowTop, kBandLeft[0] + kBandW, kRowBottom}, 0},
        {"band-b", "", {kBandLeft[1], kRowTop, kBandLeft[1] + kBandW, kRowBottom}, 0},
        {"tape", "", {kTapeLeft, kRowTop, kTapeRight, kRowBottom}, 0},
        {"output", "", {kOutLeft, kRowTop, kOutRight, kRowBottom}, 0},
        {"tail", "end of the chain", {8, kTailTop, kWidth - 8, kTailTop + smacheratr::TailPanel::kOpenHeight}, 1, -1, true},
    };
    return s;
}

} // namespace sweepr
