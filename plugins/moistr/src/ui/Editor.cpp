#include "Editor.h"

#include "BandView.h"
#include "Help.h"
#include "SweepView.h"
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
    sweepView = nullptr;
    sweepControls.clear ();
    shelfControls.clear ();
    for (int b = 0; b < 2; ++b)
        rateViews[b] = syncRateViews[b] = nullptr;
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

    // ---- the SWEEP stage (the default sound): its switch and the saturator's Drive, the two bells, the High
    // Shelf and its display
    auto* sweep = new Panel (CRect (kSweepLeft, kSweepRow1, kSweepRight, kSweepRow1 + kRowH), "SWEEP");
    root->addView (sweep);
    bind (sweep, new Toggle (switchRect (), this, kSweep, "Sweep"));
    for (Knob* k : besides (sweep, {{kSweepDrive}}))
        sweepControls.push_back (k);

    // each bell: Sync, its Sync Rate and Phase in a compact column, then Rate, Low, High, Gain and Width
    for (int b = 0; b < 2; ++b)
    {
        const double left = b == 0 ? kBellALeft : kBellBLeft, right = b == 0 ? kBellARight : kBellBRight;
        const uint32_t base = b == 0 ? kARate : kBRate;
        auto* bell = new Panel (CRect (left, kSweepRow1, right, kSweepRow1 + kRowH), b == 0 ? "BELL A" : "BELL B");
        root->addView (bell);
        const double cx = kSwitchLeft, cr = kSwitchLeft + kBellColW;
        sweepControls.push_back (bind (bell, new Toggle (CRect (cx, kSwitchTop, cr, kSwitchTop + kSwitchH), this, base + 1, "Sync")));
        syncRateViews[b] = bind (bell, new pk::Choice (CRect (cx, 56, cr, 76), this, base + 2));
        sweepControls.push_back (syncRateViews[b]);
        bell->addView (new Label (CRect (cx, 84, cx + 38, 102), "Phase", 9.5));
        sweepControls.push_back (bind (bell, new pk::NumberBox (CRect (cx + 40, 84, cr, 102), this, base + 7)));
        int i = 0;
        for (uint32_t id : {base + 0, base + 3, base + 4, base + 5, base + 6})
        {
            const double x = kBellKnobLeft + kKnobStep * i++;
            auto* k = bind (bell, new Knob (CRect (x, kKnobTop, x + kKnobW, kKnobTop + kKnobH), this, id, nullptr, id == base + 5));
            sweepControls.push_back (k);
            if (id == base + 0)
                rateViews[b] = k;
        }
    }

    // the High Shelf: on, then its orbit's Rate, where its corner and gain go, its Q, Wander and Tilt
    auto* shelf = new Panel (CRect (kShelfLeft, kSweepRow2, kShelfRight, kSweepRow2 + kRowH), "HIGH SHELF");
    root->addView (shelf);
    sweepControls.push_back (bind (shelf, new Toggle (switchRect (), this, kShelf, "High Shelf")));
    for (Knob* k : besides (shelf, {{kShelfRate}, {kShelfLow}, {kShelfHigh}, {kShelfMin, true}, {kShelfMax, true}, {kShelfQ}, {kShelfWander},
                                     {kShelfTilt}}))
    {
        sweepControls.push_back (k);
        shelfControls.push_back (k);
    }

    sweepView = new SweepView (layoutRegion ("sweepview", CRect (kSweepViewLeft, kSweepRow2, kSweepViewRight, kSweepRow2 + kRowH)), this,
                               [c = ctl] () -> const Meters* { auto* s = c->getShared (); return s ? &s->meters : nullptr; });
    pk::setHelp (sweepView, "Sweep", help::kSweepView);
    root->addView (sweepView);

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
    const bool sweeping = plainValue (kSweep) >= 0.5, shelving = plainValue (kShelf) >= 0.5;
    for (pk::ParamView* v : sweepControls)
        v->setEnabledLook (sweeping);
    for (pk::ParamView* v : shelfControls)
        v->setEnabledLook (sweeping && shelving);
    for (int b = 0; b < 2; ++b)
    {
        const bool synced = plainValue (b == 0 ? kASync : kBSync) >= 0.5;
        if (rateViews[b])
            rateViews[b]->setEnabledLook (sweeping && !synced);
        if (syncRateViews[b])
            syncRateViews[b]->setEnabledLook (sweeping && synced);
    }
}

void Editor::paramChanged (uint32_t id)
{
    pk::EditorBase::paramChanged (id);
    if (tail)
        tail->paramChanged (id);
    if (id == kBandCount || id == kShiftOn || id == kSeedBlend || id == kLiquid || id == kSweep || id == kShelf || id == kASync ||
        id == kBSync)
        updateLooks ();
    if (sweepView && SweepView::shows (id))
    {
        sweepView->idle ();
        sweepView->invalid ();
    }
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
    if (sweepView)
        sweepView->idle ();
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
    // Wide: the SWEEP stage's row first (sweep; bell a over bell b; the high shelf over its display), then the
    // bands' display and columns of two panels (split over movement, levels over band move, shift over rise /
    // fall, glue over the output); a row of the later panels (seed b and link, low, extreme, liquid) under them,
    // and the end saturator in a row of its own at the bottom
    s.panels = {
        {"sweep", "", {kSweepLeft, kSweepRow1, kSweepRight, kSweepRow1 + kRowH}, 0},
        {"bell-a", "", {kBellALeft, kSweepRow1, kBellARight, kSweepRow1 + kRowH}, 0, 0},
        {"bell-b", "", {kBellBLeft, kSweepRow1, kBellBRight, kSweepRow1 + kRowH}, 0, 0},
        {"shelf", "", {kShelfLeft, kSweepRow2, kShelfRight, kSweepRow2 + kRowH}, 0, 1},
        {"sweepview", "sweep", {kSweepViewLeft, kSweepRow2, kSweepViewRight, kSweepRow2 + kRowH}, 0, 1, true},
        {"display", "bands", {d.left, d.top, d.right, d.bottom}, 1},
        {"split", "", {kSplitLeft, kRow1, kSplitRight, kRow1 + kRowH}, 1, 0},
        {"movement", "", {kMoveLeft, kRow2, kMoveRight, kRow2 + kRowH}, 1, 0},
        {"levels", "", {kLevelsLeft, kRow1, kLevelsRight, kRow1 + kRowH}, 1, 1},
        {"bandmove", "", {kBandMoveLeft, kRow2, kBandMoveRight, kRow2 + kRowH}, 1, 1},
        {"shift", "", {kShiftLeft, kRow1, kShiftRight, kRow1 + kRowH}, 1, 2},
        {"risefall", "", {kShapeLeft, kRow2, kShapeRight, kRow2 + kRowH}, 1, 2},
        {"glue", "", {kGlueLeft, kRow1, kGlueRight, kRow1 + kRowH}, 1, 3},
        {"output", "", {kOutLeft, kRow2, kOutRight, kRow2 + kRowH}, 1, 3},
        {"seedb", "", {kSeedBLeft, kRow3, kSeedBRight, kRow3 + kRowH}, 2},
        {"low", "", {kLowLeft, kRow3, kLowRight, kRow3 + kRowH}, 2},
        {"extreme", "", {kExtremeLeft, kRow3, kExtremeRight, kRow3 + kRowH}, 2},
        {"liquid", "", {kLiquidLeft, kRow3, kLiquidRight, kRow3 + kRowH}, 2},
        {"tail", "end of the chain", {8, kTailTop, kWidth - 8, kTailTop + smacheratr::TailPanel::kOpenHeight}, 3, -1, true},
    };
    return s;
}

} // namespace moistr
