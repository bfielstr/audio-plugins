#include "Editor.h"

#include "BandView.h"
#include "GestureView.h"
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
    sceneControls.clear ();
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

    // the gesture (0.28): ONE gesture moving many targets together. Gesture and File (a user gesture from the
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

    gestureView = new GestureView (layoutRegion ("gestureview", CRect (kGestureViewLeft, kRow4, kGestureViewRight, kRow4 + kRowH)), this,
                                   [c = ctl] () -> const Meters* { auto* s = c->getShared (); return s ? &s->meters : nullptr; },
                                   [c = ctl] { return c->userScene (); }, [c = ctl] { return c->userSceneData ().name; });
    pk::setHelp (gestureView, "Gesture", help::kGestureView);
    root->addView (gestureView);

    // the saturator at the end of the chain, with Smacheratr's displays above its controls
    tail = std::make_unique<smacheratr::TailPanel> (this, smacheratr::TailBases {kTailBase, kTailExtBase, kTailExt2Base, kTailExt3Base, kTailExt4Base},
                                                    [c = ctl] { auto* s = c->getShared (); return s ? s->sampleRate.load () : 48000.0; },
                                                    [c = ctl] () -> const smacheratr::Meters* { auto* s = c->getShared (); return s ? &s->tailMeters : nullptr; });
    tail->add (root, layoutRegion ("tail", CRect (8, kTailTop, kWidth - 8, kTailTop + smacheratr::TailPanel::kOpenHeight)));

    applyParamTooltips (&help::forParam);
    pickBell (pickedBell);
    updateLooks ();
    idle ();
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
    if (isGestureParam (id))
        return true;
    if (id == kBandCount || id == kShiftOn || id == kSeedBlend || id == kLiquid || id == kSweep || id == kShelf || id == kToneOn ||
        id == kCleanSub || id == kSubBoost)
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
    if (gestureView && isGestureParam (id))
        gestureView->invalid ();
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
    // a row of the gesture (gesture, wobble, the gesture display) and the end saturator in a row of its own at the
    // bottom
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
        {"tail", "end of the chain", {8, kTailTop, kWidth - 8, kTailTop + smacheratr::TailPanel::kOpenHeight}, 4, -1, true},
    };
    return s;
}

} // namespace moistr
