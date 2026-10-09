#include "Editor.h"

#include "BandView.h"
#include "GestureView.h"
#include "Help.h"
#include "SweepView.h"
#include "plugin/Controller.h"

#include "smemplr/src/core/FxSlot.h"

#include "pluginkit/ui/Theme.h"
#include "pluginkit/vst/Clipboard.h"
#include "pluginkit/vst/PresetBar.h"

#include "vstgui/lib/cframe.h"
#include "vstgui/lib/controls/coptionmenu.h"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#include <shellapi.h>
#else
#include <spawn.h>
#include <sys/wait.h>
extern char** environ;
#endif

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

// A LAB slot's values through its kind's own table and units (the kind the LAB shows there): a control on it edits the
// slot's value; turned while the slot holds another kind (Empty), it loads the kind there first (from the block's
// values, the kind's defaults until they are changed).
class LabSlotHost : public pk::ParamHost
{
public:
    LabSlotHost (pk::ParamHost* host, Controller* c, int s, int k) : in (host), ctl (c), slot (s), kind (k) {}
    const pk::ParamTable& table () override { return smemplr::fxTable (kind); }
    double norm (uint32_t id) override { return in->norm (idOf (id)); }
    double plainValue (uint32_t id) override { return table ().toPlain (id, norm (id)); }
    void beginEdit (uint32_t id) override
    {
        load ();
        in->beginEdit (idOf (id));
    }
    void setNorm (uint32_t id, double v) override { in->setNorm (idOf (id), v); }
    void endEdit (uint32_t id) override { in->endEdit (idOf (id)); }
    std::string valueText (uint32_t id) override { return table ().toText (id, plainValue (id)); }
    int64_t sourceParam (uint32_t id) override { return idOf (id); }

protected:
    uint32_t idOf (uint32_t id) const { return labBlockParam (slot, (uint32_t)smemplr::fxBlockOf (kind, id)); }
    void load ()
    {
        if (ctl->labKind (slot) != kind)
            in->setOnce (labSlotParam (slot, kLabType), paramTable ().toNormalized (labSlotParam (slot, kLabType), kind));
    }
    pk::ParamHost* in;
    Controller* ctl;
    int slot, kind;
};

// POST's OTT Up and Down (ids 0 and 1, 0 .. 100 %) on its multidyn's bands' Below and Above ratios: 100 % at OTT's own
// ratios (multidyn's defaults), 0 % at 1:1 (no upward or downward compression), in between the strength (1 - 1/r)
// that multidyn's OTT style reads, scaled. Every band moves together; the first band's ratio is what they show.
class OttRatioHost : public LabSlotHost
{
public:
    OttRatioHost (pk::ParamHost* host, Controller* c, int s) : LabSlotHost (host, c, s, smemplr::kFxMultidyn) {}
    const pk::ParamTable& table () override
    {
        using namespace pk::make;
        static const pk::ParamTable t (std::vector<pk::ParamInfo> {percent (0, "OTT Up", "Up", 1.0), percent (1, "OTT Down", "Down", 1.0)});
        return t;
    }
    double norm (uint32_t id) override
    {
        const uint32_t f = field (id);
        const double r = multidyn::toPlain (multidyn::bandParam (0, f), in->norm (ratioId (0, f)));
        return std::clamp ((1.0 - 1.0 / std::max (r, 1e-3)) / (1.0 - 1.0 / own (0, f)), 0.0, 1.0);
    }
    void beginEdit (uint32_t id) override
    {
        load ();
        for (int b = 0; b < multidyn::kMaxBands; ++b)
            in->beginEdit (ratioId (b, field (id)));
    }
    void setNorm (uint32_t id, double v) override
    {
        const uint32_t f = field (id);
        for (int b = 0; b < multidyn::kMaxBands; ++b)
        {
            const double r = 1.0 / std::max (1e-6, 1.0 - std::clamp (v, 0.0, 1.0) * (1.0 - 1.0 / own (b, f)));
            in->setNorm (ratioId (b, f), multidyn::toNormalized (multidyn::bandParam (b, f), r));
        }
    }
    void endEdit (uint32_t id) override
    {
        for (int b = 0; b < multidyn::kMaxBands; ++b)
            in->endEdit (ratioId (b, field (id)));
    }
    std::string valueText (uint32_t id) override { return table ().toText (id, norm (id)); }
    int64_t sourceParam (uint32_t id) override { return ratioId (0, field (id)); }

private:
    static uint32_t field (uint32_t id) { return id == 0 ? (uint32_t)multidyn::kBelowRatio : (uint32_t)multidyn::kAboveRatio; }
    static double own (int b, uint32_t f) { return multidyn::paramTable ().info (multidyn::bandParam (b, (int)f)).def; }
    uint32_t ratioId (int b, uint32_t f) const { return idOf (multidyn::bandParam (b, (int)f)); }
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
    splitControls.clear ();
    boostControls.clear ();
    toneKnob = nullptr;
    for (int b = 0; b < kNumBells; ++b)
    {
        rateViews[b] = syncRateViews[b] = nullptr;
        bellViews[b].clear ();
        bellKnobs[b].clear ();
    }
    liquidKnobs.clear ();
    labViews.clear ();
    for (int c = 0; c <= kNumBandChains; ++c)
    {
        labSat[c].clear ();
        labOtt[c].clear ();
        labAll[c].clear ();
    }
    sceneControls.clear ();
    loopControls.clear ();
    paraControls.clear ();
    guardControls.clear ();
    sceneSpeed = nullptr;
    wobbleKnobs.clear ();
    gestureView = nullptr;
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

    // ---- the SWEEP stage (the default sound): its switch, the saturator's Drive and Curve and the Tone after it;
    // the eight bells (one at a time); the sub options; the High Shelf and the display
    auto* sweep = new Panel (CRect (kSweepLeft, kSweepRow1, kSweepRight, kSweepRow1 + kRowH), "SWEEP");
    root->addView (sweep);
    const double sc = kSwitchLeft, scr = kSwitchLeft + kSweepColW;
    bind (sweep, new Toggle (CRect (sc, kSwitchTop, scr, kSwitchTop + kSwitchH), this, kSweep, "Sweep"));
    sweepControls.push_back (bind (sweep, new Segmented (CRect (sc, kCurveTop, scr, kCurveTop + kSwitchH), this, kSweepCurve, {"Hard", "Soft"})));
    sweepControls.push_back (bind (sweep, new Toggle (CRect (sc, kToneTop, scr, kToneTop + kSwitchH), this, kToneOn, "Tone")));
    for (int i = 0; i < 2; ++i)
    {
        const double x = kSweepKnobLeft + kKnobStep * i;
        auto* k = bind (sweep, new Knob (CRect (x, kKnobTop, x + kKnobW, kKnobTop + kKnobH), this, i == 0 ? kSweepDrive : kTone));
        sweepControls.push_back (k);
        if (i == 1)
            toneKnob = k;
    }

    // the bells: a picker (A .. H) and every bell's On under it; the picked bell's Sync and Sync Rate, then its Rate,
    // Low, High, Gain, Width and Phase
    auto* bells = new Panel (CRect (kBellsLeft, kSweepRow1, kBellsRight, kSweepRow1 + kRowH), "BELLS");
    root->addView (bells);
    static const char* const letters[kNumBells] = {"A", "B", "C", "D", "E", "F", "G", "H"};
    for (int b = 0; b < kNumBells; ++b)
    {
        const double x = kSwitchLeft + kBellCellW * b, xr = x + kBellCellW - 2.0;
        auto* pick = new ActionButton (CRect (x, kBellPickTop, xr, kBellPickTop + kBellRowH), letters[b], [this, b] { pickBell (b); },
                                       [this, b] { return pickedBell == b; });
        char tip[96];
        std::snprintf (tip, sizeof (tip), "Shows bell %s's controls here (the curves of every bell on are in the display).", letters[b]);
        pick->setTooltipText (tip);
        bells->addView (pick);
        sweepControls.push_back (bind (bells, new Toggle (CRect (x, kBellOnTop, xr, kBellOnTop + kBellRowH), this, bellOnId (b), letters[b])));
        // (the picked bell's own controls, all in the same places: only the picked one's are shown)
        auto keep = [&] (pk::ParamView* v) {
            sweepControls.push_back (v);
            bellViews[b].push_back (v);
            return v;
        };
        keep (bind (bells, new Toggle (CRect (kSwitchLeft, kBellSyncTop, kSwitchLeft + kBellSyncW, kBellSyncTop + kSwitchH), this,
                                       bellId (b, kBellSync), "Sync")));
        syncRateViews[b] = keep (bind (bells, new pk::Choice (CRect (kSwitchLeft + kBellSyncW + 6.0, kBellSyncTop,
                                                                     kSwitchLeft + kBellCellW * kNumBells - 2.0, kBellSyncTop + kSwitchH),
                                                              this, bellId (b, kBellSyncRate))));
        int i = 0;
        for (BellField f : {kBellRate, kBellLow, kBellHigh, kBellGain, kBellWidth, kBellPhase})
        {
            const double kx = kBellKnobLeft + kKnobStep * i++;
            auto* k = bind (bells, new Knob (CRect (kx, kKnobTop, kx + kKnobW, kKnobTop + kKnobH), this, bellId (b, f), nullptr, f == kBellGain));
            keep (k);
            bellKnobs[b].push_back (k);
            if (f == kBellRate)
                rateViews[b] = k;
        }
    }

    // the sub options: Clean Sub (the lows around the saturator) and Sub Boost (the lows added back after it)
    auto* sub = new Panel (CRect (kSubLeft, kSweepRow1, kSubRight, kSweepRow1 + kRowH), "SUB");
    root->addView (sub);
    auto subColumn = [&] (int col, uint32_t sw, const char* name, std::initializer_list<std::pair<uint32_t, const char*>> boxes,
                          std::vector<pk::ParamView*>& views) {
        const double x = kSwitchLeft + (kSubColW + 8.0) * col, xr = x + kSubColW;
        sweepControls.push_back (bind (sub, new Toggle (CRect (x, kSwitchTop, xr, kSwitchTop + kSwitchH), this, sw, name)));
        int i = 0;
        for (const auto& [id, label] : boxes)
        {
            const double y = kSubBoxTop + kSubBoxStep * i++;
            sub->addView (new Label (CRect (x, y, x + kSubLabelW, y + kSubBoxH), label, 9.5));
            auto* box = bind (sub, new pk::NumberBox (CRect (x + kSubLabelW + 2.0, y, xr, y + kSubBoxH), this, id));
            sweepControls.push_back (box);
            views.push_back (box);
        }
    };
    subColumn (0, kCleanSub, "Clean Sub", {{kSplitFreq, "Split"}, {kSplitLevel, "Level"}, {kSplitDrive, "Drive"}}, splitControls);
    subColumn (1, kSubBoost, "Sub Boost", {{kSubFreq, "Freq"}, {kSubLevel, "Level"}}, boostControls);

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

    // the gesture (0.30): ONE gesture moving many targets together. Gesture and File (a user gesture from the
    // Gestures folder); Mode, Length and Speed; Position, Smooth and Amount
    auto* gestures = new Panel (CRect (kGesturesLeft, kRow4, kGesturesRight, kRow4 + kRowH), "GESTURE");
    root->addView (gestures);
    {
        auto label = [&] (double x0, double y, double w, const char* t) { gestures->addView (new Label (CRect (x0, y + 3.0, x0 + w, y + 17.0), t, 9.5)); };
        label (kSwitchLeft, kGestureTop, kGestureLabelW, "Gesture");
        bind (gestures, new pk::Choice (CRect (kSwitchLeft + kGestureLabelW, kGestureTop, kGestureMenuRight, kGestureTop + kGestureRowH), this, kScene));
        auto* file = new ActionButton (CRect (kSwitchLeft, kGestureFileTop, kGestureMenuRight, kGestureFileTop + kGestureRowH), "File",
                                       [this] { showGestureFiles (layoutPoint (CPoint (kGesturesLeft + kSwitchLeft, kRow4 + kGestureFileTop + kGestureRowH))); });
        pk::setHelp (file, "File", help::kGestureFile);
        gestures->addView (file);
        sceneControls.push_back (bind (gestures, new Segmented (CRect (kGestureColLeft, kGestureTop, kGestureColRight, kGestureTop + kGestureRowH), this,
                                                                kSceneMode, {"Loop", "Walk"})));
        label (kGestureColLeft, kGestureLengthTop, kGestureColLabelW, "Length");
        sceneControls.push_back (bind (gestures, new pk::Choice (CRect (kGestureColLeft + kGestureColLabelW, kGestureLengthTop, kGestureColRight,
                                                                        kGestureLengthTop + kGestureRowH),
                                                                 this, kSceneLength)));
        label (kGestureColLeft, kGestureSpeedTop, kGestureColLabelW, "Speed");
        sceneSpeed = bind (gestures, new pk::Choice (CRect (kGestureColLeft + kGestureColLabelW, kGestureSpeedTop, kGestureColRight,
                                                            kGestureSpeedTop + kGestureRowH),
                                                     this, kSceneSpeed));
        int i = 0;
        for (uint32_t id : {kScenePosition, kSceneSmooth, kSceneAmount})
        {
            const double kx = kGestureKnobLeft + kKnobStep * i++;
            sceneControls.push_back (bind (gestures, new Knob (CRect (kx, kKnobTop, kx + kKnobW, kKnobTop + kKnobH), this, id)));
        }
    }

    // Wobble: the tremolo on the bands above Low (its rate in cycles per beat, how deep)
    auto* wobble = new Panel (CRect (kWobbleLeft, kRow4, kWobbleRight, kRow4 + kRowH), "WOBBLE");
    root->addView (wobble);
    wobbleKnobs = knobs (wobble, kWobbleRight - kWobbleLeft, {{kWobbleRate}, {kWobbleAmount}});

    root->addView (makeGestureView (layoutRegion ("gestureview", CRect (kGestureViewLeft, kRow4, kGestureViewRight, kRow4 + kRowH))));

    buildLab (root);
    buildRow6 (root);

    // the saturator at the end of the chain, with Smacheratr's displays above its controls
    tail = makeTail ();
    tail->add (root, layoutRegion ("tail", CRect (8, kTailTop, kWidth - 8, kTailTop + smacheratr::TailPanel::kOpenHeight)));

    applyParamTooltips (&help::forParam);
    pickBell (pickedBell);
    updateLooks ();
    idle ();
}

GestureView* Editor::makeGestureView (const CRect& r)
{
    gestureView = new GestureView (r, this, [c = ctl] () -> const Meters* { auto* s = c->getShared (); return s ? &s->meters : nullptr; },
                                   [c = ctl] { return c->userScene (); }, [c = ctl] { return c->userSceneData ().name; });
    pk::setHelp (gestureView, "Gesture", help::kGestureView);
    return gestureView;
}

std::unique_ptr<smacheratr::TailPanel> Editor::makeTail ()
{
    return std::make_unique<smacheratr::TailPanel> (this, tailBases (), [c = ctl] { auto* s = c->getShared (); return s ? s->sampleRate.load () : 48000.0; },
                                                    [c = ctl] () -> const smacheratr::Meters* { auto* s = c->getShared (); return s ? &s->tailMeters : nullptr; });
}

void Editor::buildRow6 (CViewContainer* root)
{
    // (0.30) INPUT: the level going in
    auto* input = new Panel (CRect (kInputLeft, kRow6, kInputRight, kRow6 + kRowH), "INPUT");
    root->addView (input);
    {
        const double x = centredLeft (kInputRight - kInputLeft, 1);
        bind (input, new Knob (CRect (x, kKnobTop, x + kKnobW, kKnobTop + kKnobH), this, kInput));
    }
    auto column = [&] (Panel* panel, double top, pk::ParamView* v) {
        (void)top;
        bind (panel, v);
        return v;
    };
    auto knobsFrom = [&] (Panel* panel, std::initializer_list<uint32_t> ids, std::vector<pk::ParamView*>& group) {
        int i = 0;
        for (uint32_t id : ids)
        {
            const double x = kKnobBeside + kKnobStep * i++;
            group.push_back (bind (panel, new Knob (CRect (x, kKnobTop, x + kKnobW, kKnobTop + kKnobH), this, id)));
        }
    };
    auto rowRect = [] (double top) { return CRect (kSwitchLeft, top, kSwitchLeft + kSwitchW, top + kSwitchH); };
    // LOOP LOCK: on, Shape and Length; Position and Window
    auto* loop = new Panel (CRect (kLoopLeft, kRow6, kLoopRight, kRow6 + kRowH), "LOOP LOCK");
    root->addView (loop);
    column (loop, kSwitchTop, new Toggle (rowRect (kSwitchTop), this, kLoopLock, "Loop Lock"));
    loopControls.push_back (column (loop, kColTop2, new Segmented (rowRect (kColTop2), this, kLoopShape, {"Wrap", "Bounce"})));
    loopControls.push_back (column (loop, kColTop3, new pk::Choice (rowRect (kColTop3), this, kLoopLength)));
    knobsFrom (loop, {kLoopPosition, kLoopWindow}, loopControls);
    // PARA: on, Rate and Mix; the paths' corners and how they move
    auto* para = new Panel (CRect (kParaLeft, kRow6, kParaRight, kRow6 + kRowH), "PARA");
    root->addView (para);
    column (para, kSwitchTop, new Toggle (rowRect (kSwitchTop), this, kParaOn, "Split"));
    paraControls.push_back (column (para, kColTop2, new pk::Choice (rowRect (kColTop2), this, kParaRate)));
    para->addView (new Label (CRect (kSwitchLeft, kColTop3 + 2.0, kSwitchLeft + 30.0, kColTop3 + 18.0), "Mix", 9.5));
    paraControls.push_back (column (para, kColTop3, new pk::NumberBox (CRect (kSwitchLeft + 32.0, kColTop3, kSwitchLeft + kSwitchW, kColTop3 + kSwitchH), this, kParaMix)));
    knobsFrom (para, {kParaLpFreq, kParaHpFreq, kParaLpMove, kParaHpMove, kParaHpLevelMove}, paraControls);
    // SUB GUARD: on, Guard Bells; Freq and Floor
    auto* guard = new Panel (CRect (kGuardLeft, kRow6, kGuardRight, kRow6 + kRowH), "SUB GUARD");
    root->addView (guard);
    column (guard, kSwitchTop, new Toggle (rowRect (kSwitchTop), this, kSubGuard, "Sub Guard"));
    guardControls.push_back (column (guard, kColTop2, new Toggle (rowRect (kColTop2), this, kGuardBells, "Guard Bells")));
    knobsFrom (guard, {kSubGuardFreq, kSubFloor}, guardControls);
}

pk::basic::Spec Editor::basicSpec ()
{
    // Input (the level into the saturator: its crunch), Drive (the SWEEP stage's: how hard it crunches) and Movement (how
    // far the bands move); Loop Lock, Position (which moment of the movement it holds) and Sub Guard (the sub steady); Mix
    // and Output. The display: the gesture with Loop Lock's segment on it.
    using namespace pk::basic;
    Spec s;
    s.title = "moistr";
    s.capture = [c = ctl] () -> const pk::CaptureBuffer* { auto* sh = c->getShared (); return sh ? &sh->capture : nullptr; };
    s.displayHeight = 150;
    s.display = [this] (const CRect& r) -> CView* { return makeGestureView (r); };
    s.rows = {{knob (kInput), knob (kSweepDrive), knob (kMovement)}, {toggle (kLoopLock, "Loop Lock"), knob (kLoopPosition), toggle (kSubGuard, "Sub Guard")}};
    s.output = {knob (kMix), knob (kOutput, {}, true)};
    smacheratr::TailPanel::addToBasic (s, this, tailBases (), tail, [this] { return makeTail (); });
    s.menu = [this] (CPoint p) { showMenu (p); };
    s.help = &help::forParam;
    s.advancedSwitch = CRect (440, 6, 524, 28);
    return s;
}

void Editor::buildLab (CViewContainer* root)
{
    // the LAB (0.30): per chain (MID, HIGH, AIR) its first slot's smacheratr (Grit: Drive, Curve: Post Clip), its second
    // slot's multidyn (OTT: Amount) and its Level, with Mute, Solo and Mono under them; POST's first slot's multidyn
    // (Depth: Amount, Time, Up, Down). A slot's knobs show its kind's own values (the hosts map them onto the slot).
    static const char* const titles[kNumBandChains + 1] = {"MID", "HIGH", "AIR", "POST"};
    for (int c = 0; c <= kNumBandChains; ++c)
    {
        const double left = kLabLeft + kLabStep * c;
        auto* panel = new Panel (CRect (left, kRow5, left + kLabW, kRow5 + kLabRowH), titles[c]);
        root->addView (panel);
        const double x0 = centredLeft (kLabW, 4);
        auto at = [&] (int i) { return CRect (x0 + kKnobStep * i, kKnobTop, x0 + kKnobStep * i + kKnobW, kKnobTop + kKnobH); };
        auto slotKnob = [&] (pk::ParamHost* h, int i, uint32_t id, const char* label, const char* help, std::vector<pk::ParamView*>& group) {
            auto* k = new Knob (at (i), h, id, label);
            pk::setHelp (k, label, help);
            panel->addView (k);
            labViews.push_back (k);
            group.push_back (k);
            labAll[c].push_back (k);
            return k;
        };
        auto own = [&] (pk::ParamView* v, const char* title) {
            bind (panel, v);
            labViews.push_back (v);
            labAll[c].push_back (v);
            (void)title;
        };
        if (c < kNumBandChains)
        {
            labHosts.push_back (std::make_unique<LabSlotHost> (this, ctl, chainSlot (c, 0), smemplr::kFxSmacheratr));
            pk::ParamHost* sat = labHosts.back ().get ();
            labHosts.push_back (std::make_unique<LabSlotHost> (this, ctl, chainSlot (c, 1), smemplr::kFxMultidyn));
            pk::ParamHost* ott = labHosts.back ().get ();
            slotKnob (sat, 0, smacheratr::kDrive, "Grit", help::kLabGrit, labSat[c]);
            slotKnob (sat, 1, smacheratr::kPostClip, "Curve", help::kLabCurve, labSat[c]);
            slotKnob (ott, 2, multidyn::kAmount, "OTT", help::kLabOtt, labOtt[c]);
            own (new Knob (at (3), this, chainId (c, kChainLevel), "Level", true), "Level");
            const ChainField switches[3] = {kChainMute, kChainSolo, kChainMono};
            static const char* const names[3] = {"Mute", "Solo", "Mono"};
            for (int i = 0; i < 3; ++i)
                own (new Toggle (CRect (x0 + kKnobStep * i, kLabSwitchTop, x0 + kKnobStep * i + kKnobW, kLabSwitchTop + kLabSwitchH), this,
                                 chainId (c, switches[i]), names[i]),
                     names[i]);
        }
        else
        {
            labHosts.push_back (std::make_unique<LabSlotHost> (this, ctl, postSlot (0), smemplr::kFxMultidyn));
            pk::ParamHost* ott = labHosts.back ().get ();
            labHosts.push_back (std::make_unique<OttRatioHost> (this, ctl, postSlot (0)));
            pk::ParamHost* ratios = labHosts.back ().get ();
            slotKnob (ott, 0, multidyn::kAmount, "Depth", help::kPostDepth, labOtt[c]);
            slotKnob (ott, 1, multidyn::kTime, "Time", help::kPostTime, labOtt[c]);
            slotKnob (ratios, 2, 0, "Up", help::kPostUp, labOtt[c]);
            slotKnob (ratios, 3, 1, "Down", help::kPostDown, labOtt[c]);
        }
    }
}

void Editor::pickBell (int b)
{
    pickedBell = std::clamp (b, 0, kNumBells - 1);
    for (int i = 0; i < kNumBells; ++i)
        for (CView* v : bellViews[i])
            v->setVisible (i == pickedBell);
    if (frame)
        frame->invalid ();
}

void Editor::showGestureFiles (CPoint where)
{
    if (!frame)
        return;
    auto menu = makeOwned<COptionMenu> ();
    const auto files = ctl->gestureFiles ();
    for (const auto& f : files)
        menu->addEntry (f.name.c_str ());
    if (files.empty ())
        menu->addEntry ("No gesture files in the Gestures folder", -1, CMenuItem::kDisabled);
    menu->addSeparator ();
    const bool loaded = !ctl->userSceneData ().empty ();
    menu->addEntry ("Clear the User Gesture", -1, loaded ? CMenuItem::kNoFlags : CMenuItem::kDisabled);
    menu->addEntry ("Open Gestures Folder");
    const int32_t clearAt = (int32_t)std::max<size_t> (files.size (), 1) + 1;
    menu->popup (frame, where, [this, files, clearAt] (COptionMenu* m) {
        const int32_t r = m->getLastResult ();
        if (r >= 0 && r < (int32_t)files.size ())
        {
            std::string error;
            if (!ctl->loadUserScene (files[(size_t)r].path, error) && gestureView)
                gestureView->setNote (files[(size_t)r].name + ": " + error);
        }
        else if (r == clearAt)
            ctl->clearUserScene ();
        else if (r == clearAt + 1)
            openGestureFolder ();
        if (gestureView)
            gestureView->invalid ();
    });
}

void Editor::openGestureFolder ()
{
    const std::string folder = ctl->makeGestureFolder ();
    if (folder.empty ())
        return;
#if defined(_WIN32)
    const std::wstring path = std::filesystem::path (folder).wstring ();
    ShellExecuteW (nullptr, L"open", path.c_str (), nullptr, nullptr, SW_SHOWNORMAL);
#else
#if defined(__APPLE__)
    const char* argv[] = {"/usr/bin/open", folder.c_str (), nullptr};
#else
    const char* argv[] = {"xdg-open", folder.c_str (), nullptr};
#endif
    pid_t pid;
    if (posix_spawnp (&pid, argv[0], nullptr, nullptr, const_cast<char**> (argv), environ) == 0)
        waitpid (pid, nullptr, WNOHANG);
#endif
}

bool Editor::affectsLooks (uint32_t id)
{
    if (isGestureParam (id) || (isLabSlotParam (id) && labFieldOf (id) == kLabType))
        return true;
    if (id == kBandCount || id == kShiftOn || id == kSeedBlend || id == kLiquid || id == kSweep || id == kShelf || id == kToneOn ||
        id == kCleanSub || id == kSubBoost || id == kLoopLock || id == kParaOn || id == kSubGuard)
        return true;
    for (int b = 0; b < kNumBells; ++b)
        if (id == bellOnId (b) || id == bellId (b, kBellSync))
            return true;
    return false;
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
    if (toneKnob)
        toneKnob->setEnabledLook (sweeping && plainValue (kToneOn) >= 0.5);
    for (pk::ParamView* v : splitControls)
        v->setEnabledLook (sweeping && plainValue (kCleanSub) >= 0.5);
    for (pk::ParamView* v : boostControls)
        v->setEnabledLook (sweeping && plainValue (kSubBoost) >= 0.5);
    bool wobbleDriven = plainValue (kWobbleAmount) > 0.0;
    for (int g = 0; g < kNumGestureSlots; ++g)
        wobbleDriven = wobbleDriven || std::lround (plainValue (gestureId (g, kGestureTarget))) == kTargetWobbleAmount;
    {
        const Scene* sc = GestureView::sceneOf (this, ctl->userScene ());
        const bool on = sc && sc->count > 0, walk = std::lround (plainValue (kSceneMode)) == kModeWalk;
        for (pk::ParamView* v : sceneControls)
            v->setEnabledLook (on);
        if (sceneSpeed)
            sceneSpeed->setEnabledLook (on && walk);
        for (int i = 0; on && i < sc->count; ++i)
            wobbleDriven = wobbleDriven || sc->lane[i].target == kTargetWobbleAmount;
    }
    for (Knob* k : wobbleKnobs)
        k->setEnabledLook (wobbleDriven);
    for (pk::ParamView* v : loopControls)
        v->setEnabledLook (plainValue (kLoopLock) >= 0.5);
    for (pk::ParamView* v : paraControls)
        v->setEnabledLook (plainValue (kParaOn) >= 0.5);
    for (pk::ParamView* v : guardControls)
        v->setEnabledLook (plainValue (kSubGuard) >= 0.5);
    // the LAB: a slot's knobs while it holds the kind they show; the Air chain with 4 bands only (with 3 Air goes into
    // the High chain)
    for (int c = 0; c <= kNumBandChains; ++c)
    {
        const bool heard = c != 2 || four;
        for (pk::ParamView* v : labAll[c])
            v->setEnabledLook (heard);
        const bool sat = c < kNumBandChains && ctl->labKind (chainSlot (c, 0)) == smemplr::kFxSmacheratr;
        const bool ott = ctl->labKind (c < kNumBandChains ? chainSlot (c, 1) : postSlot (0)) == smemplr::kFxMultidyn;
        for (pk::ParamView* v : labSat[c])
            v->setEnabledLook (heard && sat);
        for (pk::ParamView* v : labOtt[c])
            v->setEnabledLook (heard && ott);
    }
    for (int b = 0; b < kNumBells; ++b)
    {
        const bool on = sweeping && plainValue (bellOnId (b)) >= 0.5, synced = plainValue (bellId (b, kBellSync)) >= 0.5;
        for (pk::ParamView* k : bellKnobs[b])
            k->setEnabledLook (on);
        if (rateViews[b])
            rateViews[b]->setEnabledLook (on && !synced);
        if (syncRateViews[b])
            syncRateViews[b]->setEnabledLook (on && synced);
    }
}

void Editor::paramChanged (uint32_t id)
{
    pk::EditorBase::paramChanged (id);
    if (tail)
        tail->paramChanged (id);
    if (affectsLooks (id))
        updateLooks ();
    if (sweepView && SweepView::shows (id))
    {
        sweepView->idle ();
        sweepView->invalid ();
    }
    if (gestureView && (isGestureParam (id) || (id >= kLoopLock && id <= kLoopShape)))
        gestureView->invalid ();
    if (isLabParam (id))
        for (pk::ParamView* v : labViews)
            v->invalid (); // (the slots' knobs show LAB parameters under their kinds' IDs)
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
    if (gestureView)
        gestureView->idle ();
    if (latencyLabel)
        if (auto* s = ctl->getShared ())
        {
            char buf[64];
            // (the LAB's and the end saturator's; -1: not known yet)
            const int tailNow = s->tailMeters.latency.load (), labNow = s->meters.labLatency.load ();
            std::snprintf (buf, sizeof (buf), "Latency %d samples", tailNow >= 0 && labNow >= 0 ? tailNow + labNow : s->latency.load ());
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
    menu->addSeparator ();
    const int32_t folderAt = (int32_t)sizes.size () + 1;
    menu->addEntry ("Open Gestures Folder");
    const int settingsAt = pk::addSettingsMenuEntries (menu);
    addLayoutMenu (menu);
    menu->popup (frame, where, [this, sizes, menu, settingsAt, folderAt] (COptionMenu* m) {
        const int32_t r = m->getLastResult ();
        if (pickedInSubMenu (m)) // (Layout: its entries act by themselves)
            return;
        if (settingsMenuPicked (r, settingsAt))
            return;
        if (r >= 0 && r < (int32_t)sizes.size ())
            resizeTo (sizes[(size_t)r]);
        else if (r == folderAt)
            openGestureFolder ();
    });
}

pk::layout::Spec Editor::layoutSpec (bool arranged) const
{
    pk::layout::Spec s;
    const CRect d = displayRect (arranged);
    // Wide: the SWEEP stage's row first (sweep; bells over sub; the high shelf over its display), then the
    // bands' display and columns of two panels (split over movement, levels over band move, shift over rise /
    // fall, glue over the output); a row of the later panels (seed b and link, low, extreme, liquid) under them,
    // a row of the gesture (gesture, wobble, the gesture display), a row of the LAB (its three chains and POST), a row of
    // input, loop lock, para and sub guard, and the end saturator in a row of its own at the bottom
    s.panels = {
        {"sweep", "", {kSweepLeft, kSweepRow1, kSweepRight, kSweepRow1 + kRowH}, 0},
        {"bells", "", {kBellsLeft, kSweepRow1, kBellsRight, kSweepRow1 + kRowH}, 0, 0},
        {"sub", "", {kSubLeft, kSweepRow1, kSubRight, kSweepRow1 + kRowH}, 0, 0},
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
        {"gestures", "", {kGesturesLeft, kRow4, kGesturesRight, kRow4 + kRowH}, 3},
        {"wobble", "", {kWobbleLeft, kRow4, kWobbleRight, kRow4 + kRowH}, 3},
        {"gestureview", "gesture", {kGestureViewLeft, kRow4, kGestureViewRight, kRow4 + kRowH}, 3, -1, true},
        {"lab-mid", "", {kLabLeft, kRow5, kLabLeft + kLabW, kRow5 + kLabRowH}, 4},
        {"lab-high", "", {kLabLeft + kLabStep, kRow5, kLabLeft + kLabStep + kLabW, kRow5 + kLabRowH}, 4},
        {"lab-air", "", {kLabLeft + 2 * kLabStep, kRow5, kLabLeft + 2 * kLabStep + kLabW, kRow5 + kLabRowH}, 4},
        {"lab-post", "", {kLabLeft + 3 * kLabStep, kRow5, kLabLeft + 3 * kLabStep + kLabW, kRow5 + kLabRowH}, 4},
        {"input", "", {kInputLeft, kRow6, kInputRight, kRow6 + kRowH}, 5},
        {"looplock", "", {kLoopLeft, kRow6, kLoopRight, kRow6 + kRowH}, 5},
        {"para", "", {kParaLeft, kRow6, kParaRight, kRow6 + kRowH}, 5},
        {"subguard", "", {kGuardLeft, kRow6, kGuardRight, kRow6 + kRowH}, 5},
        {"tail", "end of the chain", {8, kTailTop, kWidth - 8, kTailTop + smacheratr::TailPanel::kOpenHeight}, 6, -1, true},
    };
    return s;
}

} // namespace moistr
