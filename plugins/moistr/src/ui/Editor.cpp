#include "Editor.h"

#include "BandView.h"
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

namespace moistr {

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

// a panel's knobs in a row (bipolar: drawn from the centre)
struct KnobDef
{
    uint32_t id;
    bool bipolar = false;
    const char* label = nullptr; // (nullptr: the parameter's short name)
};
} // namespace

Editor::Editor (Controller* c) : pk::EditorBase (c, kWidth, kHeight), ctl (c) {}

void Editor::onClose ()
{
    display = nullptr;
    liquidKnobs.clear ();
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
    root->addView (new Label (CRect (12, 6, 200, 28), "moistr", 14.0, true));
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

    display = new BandView (displayRect (arrangedLayout ()), this,
                              [c = ctl] () -> const Meters* { auto* s = c->getShared (); return s ? &s->meters : nullptr; });
    pk::setHelp (display, "Bands", help::kBandView);
    root->addView (display);

    auto switchRect = [] { return CRect (kSwitchLeft, kSwitchTop, kSwitchLeft + kSwitchW, kSwitchTop + kSwitchH); };
    // knobs beside a panel's switch
    auto besides = [this] (Panel* panel, std::initializer_list<KnobDef> defs) {
        std::vector<Knob*> made;
        int i = 0;
        for (const KnobDef& d : defs)
        {
            const double x = kKnobBeside + kKnobStep * i++;
            made.push_back (bind (panel, new Knob (CRect (x, kKnobTop, x + kKnobW, kKnobTop + kKnobH), this, d.id, d.label, d.bipolar)));
        }
        return made;
    };
    // a panel of knobs only, centred in it
    auto knobs = [this] (Panel* panel, double width, std::initializer_list<KnobDef> defs) {
        std::vector<Knob*> made;
        const double left = centredLeft (width, (int)defs.size ());
        int i = 0;
        for (const KnobDef& d : defs)
        {
            const double x = left + kKnobStep * i++;
            made.push_back (bind (panel, new Knob (CRect (x, kKnobTop, x + kKnobW, kKnobTop + kKnobH), this, d.id, d.label, d.bipolar)));
        }
        return made;
    };

    // the split: 3 or 4 bands, Drive before it, the upper crossovers (the Low one is Seed's)
    auto* split = new Panel (CRect (kSplitLeft, kRow1, kSplitRight, kRow1 + kRowH), "SPLIT");
    root->addView (split);
    bind (split, new Segmented (switchRect (), this, kBandCount, {"3 Bands", "4 Bands"}));
    highXKnob = besides (split, {{kDrive}, {kXoverMid}, {kXoverHigh}})[2];

    // each band's level (Low: locked, the others' top when they rise)
    auto* levels = new Panel (CRect (kLevelsLeft, kRow1, kLevelsRight, kRow1 + kRowH), "LEVELS");
    root->addView (levels);
    airLevelKnob = knobs (levels, kLevelsRight - kLevelsLeft,
                          {{kLowLevel, true, "Low"}, {kMidLevel, true, "Mid"}, {kHighLevel, true, "High"}, {kAirLevel, true, "Air"}})[3];

    // how quickly the moving bands rise and fall, and how far they fall
    auto* shape = new Panel (CRect (kShapeLeft, kRow2, kShapeRight, kRow2 + kRowH), "RISE / FALL");
    root->addView (shape);
    knobs (shape, kShapeRight - kShapeLeft, {{kRise}, {kFall}, {kDepth}});

    // the movement: Sync and its rate, how much, how fast and which pattern
    auto* move = new Panel (CRect (kMoveLeft, kRow2, kMoveRight, kRow2 + kRowH), "MOVEMENT");
    root->addView (move);
    bind (move, new Toggle (switchRect (), this, kSync, "Sync"));
    bind (move, new pk::Choice (CRect (kSwitchLeft, 62, kSwitchLeft + kSwitchW, 82), this, kSyncRate));
    besides (move, {{kMovement}, {kRate}, {kSeed}});

    // each moving band's share of the movement
    auto* bandMove = new Panel (CRect (kBandMoveLeft, kRow2, kBandMoveRight, kRow2 + kRowH), "BAND MOVE");
    root->addView (bandMove);
    airMoveKnob = knobs (bandMove, kBandMoveRight - kBandMoveLeft, {{kMidMove}, {kHighMove}, {kAirMove}})[2];

    // the glue after the bands: Passes, the compressor and the soft clipping
    auto* glue = new Panel (CRect (kGlueLeft, kRow1, kGlueRight, kRow1 + kRowH), "GLUE");
    root->addView (glue);
    bind (glue, new Segmented (switchRect (), this, kPasses, {"1 Pass", "2 Passes"}));
    besides (glue, {{kGlue}, {kGrit}});

    // the frequency shifter on the bands above Low: on, how far and how much
    auto* shift = new Panel (CRect (kShiftLeft, kRow1, kShiftRight, kRow1 + kRowH), "SHIFT");
    root->addView (shift);
    bind (shift, new Toggle (CRect (kKnobLeft, kSwitchTop, kKnobLeft + kShiftSwitchW, kSwitchTop + kSwitchH), this, kShiftOn, "On"));
    for (int i = 1; i <= 2; ++i)
    {
        const double x = kKnobLeft + kKnobStep * i;
        auto* k = bind (shift, new Knob (CRect (x, kKnobTop, x + kKnobW, kKnobTop + kKnobH), this, i == 1 ? kShift : kShiftMix, nullptr, i == 1));
        (i == 1 ? shiftKnob : shiftMixKnob) = k;
    }

    // the second pattern: Seed B and how the two blend; Link, how much the moving bands share one pattern
    auto* seedB = new Panel (CRect (kSeedBLeft, kRow3, kSeedBRight, kRow3 + kRowH), "SEED B / LINK");
    root->addView (seedB);
    seedBKnob = knobs (seedB, kSeedBRight - kSeedBLeft, {{kSeedB}, {kSeedBlend}, {kLink}})[0];

    // the Low band: how far it comes forward (Push) and dips back (Dip) on its own events
    auto* low = new Panel (CRect (kLowLeft, kRow3, kLowRight, kRow3 + kRowH), "LOW");
    root->addView (low);
    knobs (low, kLowRight - kLowLeft, {{kLowPush}, {kLowDip}});

    // more extreme movement: falls to silence, more events, faster ramps (a switch and two knobs, centred)
    auto* extreme = new Panel (CRect (kExtremeLeft, kRow3, kExtremeRight, kRow3 + kRowH), "EXTREME");
    root->addView (extreme);
    const double ex = centredSwitchLeft (kExtremeRight - kExtremeLeft, 2);
    bind (extreme, new Toggle (CRect (ex, kSwitchTop, ex + kSwitchW, kSwitchTop + kSwitchH), this, kDropOut, "Drop Out"));
    for (int i = 0; i < 2; ++i)
    {
        const double x = ex + kKnobBeside - kSwitchLeft + kKnobStep * i;
        bind (extreme, new Knob (CRect (x, kKnobTop, x + kKnobW, kKnobTop + kKnobH), this, i == 0 ? kDensity : kSpeed));
    }

    // the moving resonance on the bands above Low: how strong, how sharp, and where it may go
    auto* liquid = new Panel (CRect (kLiquidLeft, kRow3, kLiquidRight, kRow3 + kRowH), "LIQUID");
    root->addView (liquid);
    const auto made = knobs (liquid, kLiquidRight - kLiquidLeft, {{kLiquid}, {kLiquidRes}, {kLiquidLow}, {kLiquidHigh}});
    liquidKnobs.assign (made.begin () + 1, made.end ());

    // the output: Mix and Output
    auto* out = new Panel (CRect (kOutLeft, kRow2, kOutRight, kRow2 + kRowH), "OUTPUT");
    root->addView (out);
    knobs (out, kOutRight - kOutLeft, {{kMix}, {kOutput, true}});

    // the saturator at the end of the chain, with Smacheratr's displays above its controls
    tail = std::make_unique<smacheratr::TailPanel> (this, smacheratr::TailBases {kTailBase, kTailExtBase, kTailExt2Base, kTailExt3Base, kTailExt4Base},
                                                    [c = ctl] { auto* s = c->getShared (); return s ? s->sampleRate.load () : 48000.0; },
                                                    [c = ctl] () -> const smacheratr::Meters* { auto* s = c->getShared (); return s ? &s->tailMeters : nullptr; });
    tail->add (root, layoutRegion ("tail", CRect (8, kTailTop, kWidth - 8, kTailTop + smacheratr::TailPanel::kOpenHeight)));

    applyParamTooltips (&help::forParam);
    updateLooks ();
    idle ();
}

void Editor::updateLooks ()
{
    const bool four = std::lround (plainValue (kBandCount)) == kBands4;
    for (Knob* k : {highXKnob, airLevelKnob, airMoveKnob})
        if (k)
            k->setEnabledLook (four);
    const bool shifting = plainValue (kShiftOn) >= 0.5;
    for (Knob* k : {shiftKnob, shiftMixKnob})
        if (k)
            k->setEnabledLook (shifting);
    if (seedBKnob)
        seedBKnob->setEnabledLook (plainValue (kSeedBlend) > 0.0);
    for (Knob* k : liquidKnobs)
        k->setEnabledLook (plainValue (kLiquid) > 0.0);
}

void Editor::paramChanged (uint32_t id)
{
    pk::EditorBase::paramChanged (id);
    if (tail)
        tail->paramChanged (id);
    if (id == kBandCount || id == kShiftOn || id == kSeedBlend || id == kLiquid)
        updateLooks ();
    if (display && BandView::shows (id))
    {
        display->idle (); // (the snapshot: levels, crossovers, bands)
        display->invalid ();
    }
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
    // Wide: the display, then columns of two panels (split over movement, levels over band move, shift over
    // rise / fall, glue over the output); a row of the later panels (seed b and link, low, extreme, liquid)
    // under them, and the end saturator in a row of its own at the bottom
    s.panels = {
        {"display", "bands", {d.left, d.top, d.right, d.bottom}, 0},
        {"split", "", {kSplitLeft, kRow1, kSplitRight, kRow1 + kRowH}, 0, 0},
        {"movement", "", {kMoveLeft, kRow2, kMoveRight, kRow2 + kRowH}, 0, 0},
        {"levels", "", {kLevelsLeft, kRow1, kLevelsRight, kRow1 + kRowH}, 0, 1},
        {"bandmove", "", {kBandMoveLeft, kRow2, kBandMoveRight, kRow2 + kRowH}, 0, 1},
        {"shift", "", {kShiftLeft, kRow1, kShiftRight, kRow1 + kRowH}, 0, 2},
        {"risefall", "", {kShapeLeft, kRow2, kShapeRight, kRow2 + kRowH}, 0, 2},
        {"glue", "", {kGlueLeft, kRow1, kGlueRight, kRow1 + kRowH}, 0, 3},
        {"output", "", {kOutLeft, kRow2, kOutRight, kRow2 + kRowH}, 0, 3},
        {"seedb", "", {kSeedBLeft, kRow3, kSeedBRight, kRow3 + kRowH}, 1},
        {"low", "", {kLowLeft, kRow3, kLowRight, kRow3 + kRowH}, 1},
        {"extreme", "", {kExtremeLeft, kRow3, kExtremeRight, kRow3 + kRowH}, 1},
        {"liquid", "", {kLiquidLeft, kRow3, kLiquidRight, kRow3 + kRowH}, 1},
        {"tail", "end of the chain", {8, kTailTop, kWidth - 8, kTailTop + smacheratr::TailPanel::kOpenHeight}, 2, -1, true},
    };
    return s;
}

} // namespace moistr
