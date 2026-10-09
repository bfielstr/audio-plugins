#pragma once

#include "Params.h"

#include "smacheratr/src/ui/TailPanel.h"

#include <memory>
#include <vector>

#include "pluginkit/vst/EditorBase.h"

namespace moistr {

class Controller;
class BandView;
class SweepView;
class GestureView;
class LoopView;

class Editor : public pk::EditorBase
{
public:
    // Classic: two rows of the SWEEP stage at the top (SWEEP, BELLS, SUB over HIGH SHELF and the sweep display),
    // the bands' display across, three rows of panels (SPLIT, LEVELS, SHIFT, GLUE over MOVEMENT,
    // BAND MOVE, RISE / FALL, OUTPUT over SEED B / LINK, LOW, EXTREME, LIQUID), a row of the gesture (GESTURE,
    // WOBBLE and the gesture display), a row of the LAB (MID, HIGH, AIR chains and POST), the end saturator's
    // section at the bottom
    static constexpr double kWidth = 1132.0;
    static constexpr double kRowH = 116.0;
    static constexpr double kSweepRow1 = 40.0, kSweepRow2 = kSweepRow1 + kRowH + 8.0;
    static constexpr double kRow1 = kSweepRow2 + kRowH + 8.0 + 198.0, kRow2 = kRow1 + kRowH + 8.0, kRow3 = kRow2 + kRowH + 8.0;
    static constexpr double kRow4 = kRow3 + kRowH + 8.0;
    // the LAB's row (0.30): a row of knobs with a row of switches under them
    static constexpr double kLabRowH = 128.0, kRow5 = kRow4 + kRowH + 8.0;
    // a row of INPUT, LOOP LOCK, PARA and SUB GUARD (0.30), Loop Lock's window across (LoopView), then the end saturator
    static constexpr double kRow6 = kRow5 + kLabRowH + 8.0, kRow7 = kRow6 + kRowH + 8.0, kLoopViewH = 116.0;
    // row 7: DRIFT (Drift Seed, Start Drift, Speed Drift) and Loop Lock's window (LoopView)
    static constexpr double kDriftLeft = 8.0, kDriftRight = 224.0, kLoopViewLeft = 232.0, kLoopViewRight = 1124.0;
    static constexpr double kTailTop = kRow7 + kLoopViewH + 8.0;
    static constexpr double kHeight = kTailTop + smacheratr::TailPanel::kOpenHeight + 8.0;
    // the panels' places (left, right) in the Classic layout, for the host test
    static constexpr double kSweepLeft = 8.0, kSweepRight = 258.0;          // sweep row 1: Sweep, Curve, Tone (on); Drive, Tone
    static constexpr double kBellsLeft = 266.0, kBellsRight = 854.0;        // sweep row 1: the bell picker, the On strip, Sync, Sync Rate; the picked bell's knobs
    static constexpr double kSubLeft = 862.0, kSubRight = 1124.0;           // sweep row 1: Clean Sub (Split, Level, Drive), Sub Boost (Freq, Level)
    static constexpr double kShelfLeft = 8.0, kShelfRight = 662.0;          // sweep row 2: High Shelf; Rate, Low, High, Min, Max, Q, Wander, Tilt
    static constexpr double kSweepViewLeft = 670.0, kSweepViewRight = 1124.0; // sweep row 2: the sweep display
    // SWEEP's column of switches (Sweep, Curve, Tone; from kSwitchLeft) and its knobs' left (panel coordinates)
    static constexpr double kSweepColW = 90.0, kSweepKnobLeft = 116.0, kCurveTop = 56.0, kToneTop = 82.0;
    // BELLS (panel coordinates): a cell per bell (A .. H from kSwitchLeft) in the picker's row and the On strip's
    // under it, then the picked bell's Sync and Sync Rate; its knobs (Rate, Low, High, Gain, Width, Phase) from
    // kBellKnobLeft
    static constexpr double kBellCellW = 22.0, kBellPickTop = 30.0, kBellOnTop = 54.0, kBellRowH = 18.0, kBellSyncTop = 80.0;
    static constexpr double kBellSyncW = 56.0, kBellKnobLeft = 198.0;
    // SUB (panel coordinates): two columns (Clean Sub, Sub Boost), each its switch over its value boxes
    static constexpr double kSubColW = 113.0, kSubBoxTop = 56.0, kSubBoxStep = 20.0, kSubBoxH = 18.0, kSubLabelW = 36.0;
    static constexpr double kSplitLeft = 8.0, kSplitRight = 342.0;          // row 1: Bands, Drive, Mid X, High X
    static constexpr double kLevelsLeft = 350.0, kLevelsRight = 626.0;      // row 1: Low, Mid, High, Air (Level)
    static constexpr double kShiftLeft = 634.0, kShiftRight = 846.0;        // row 1: On, Shift, Shift Mix
    static constexpr double kGlueLeft = 854.0, kGlueRight = 1124.0;         // row 1: Passes, Glue, Grit
    static constexpr double kMoveLeft = 8.0, kMoveRight = 342.0;            // row 2: Sync, Sync Rate, Movement, Rate, Seed
    static constexpr double kBandMoveLeft = 350.0, kBandMoveRight = 626.0;  // row 2: Mid Move, High Move, Air Move
    static constexpr double kShapeLeft = 634.0, kShapeRight = 846.0;        // row 2: Rise, Fall, Depth
    static constexpr double kOutLeft = 854.0, kOutRight = 1124.0;           // row 2: Mix, Output
    static constexpr double kSeedBLeft = 8.0, kSeedBRight = 290.0;          // row 3: Seed B, Blend, Link
    static constexpr double kLowLeft = 298.0, kLowRight = 470.0;            // row 3: Push, Dip (the Low band's)
    static constexpr double kExtremeLeft = 478.0, kExtremeRight = 770.0;    // row 3: Drop Out, Density, Speed (centred)
    static constexpr double kLiquidLeft = 778.0, kLiquidRight = 1124.0;     // row 3: Liquid, Res, Low, High
    static constexpr double kGesturesLeft = 8.0, kGesturesRight = 530.0;    // row 4: Gesture, File; Mode, Length, Speed; Position, Smooth, Amount
    static constexpr double kWobbleLeft = 538.0, kWobbleRight = 682.0;      // row 4: Wobble's Rate and Amount
    static constexpr double kGestureViewLeft = 690.0, kGestureViewRight = 1124.0; // row 4: the gesture display (its lanes)
    // GESTURE (panel coordinates): the Gesture menu (with its label) at kGestureTop, File under it; the column of
    // Mode, Length and Speed from kGestureColLeft; the knobs (Position, Smooth, Amount) from kGestureKnobLeft
    static constexpr double kGestureTop = 28.0, kGestureRowH = 20.0, kGestureFileTop = 54.0, kGestureMenuRight = 206.0, kGestureLabelW = 46.0;
    static constexpr double kGestureLengthTop = 54.0, kGestureSpeedTop = 80.0;
    static constexpr double kGestureColLeft = 214.0, kGestureColRight = 318.0, kGestureColLabelW = 40.0, kGestureKnobLeft = 330.0;
    // the LAB (row 5): MID, HIGH, AIR (a chain each: Grit, Curve, OTT, Level over Mute, Solo, Mono) and POST (Depth,
    // Time, Up, Down), kLabW wide from kLabLeft, kLabStep apart; the switches' row (panel coordinates)
    static constexpr double kLabLeft = 8.0, kLabW = 273.0, kLabStep = kLabW + 8.0, kLabSwitchTop = 100.0, kLabSwitchH = 18.0;
    static_assert (kLabLeft + 3 * kLabStep + kLabW == kWidth - 8.0, "the LAB's four panels fill the row");
    // row 6 (0.30): INPUT (Input), LOOP LOCK (Loop Lock, Shape and Length over each other; Start, End), PARA (Split,
    // Rate and Mix over each other; LP Freq, HP Freq, LP Move, HP Move, HP Level), SUB GUARD (Sub Guard, Guard Bells;
    // Freq, Floor)
    static constexpr double kInputLeft = 8.0, kInputRight = 88.0;
    static constexpr double kLoopLeft = 96.0, kLoopRight = 366.0;
    static constexpr double kParaLeft = 374.0, kParaRight = 836.0;
    static constexpr double kGuardLeft = 844.0, kGuardRight = 1124.0;
    static constexpr double kColTop2 = 56.0, kColTop3 = 82.0; // (a switch column's second and third rows)
    // a switch at the top left of its panel (Bands in SPLIT, Sync in MOVEMENT, Passes in GLUE; panel coordinates)
    static constexpr double kSwitchLeft = 14.0, kSwitchTop = 30.0, kSwitchW = 110.0, kSwitchH = 20.0;
    // SHIFT's On switch: a knob wide, in the first knob's place (its knobs follow it from kKnobLeft + kKnobStep)
    static constexpr double kShiftSwitchW = 56.0;
    // the knobs: 56 x 64 from y 28, one every 64 px (from x 136 beside a switch; centred in a panel of
    // knobs only: centredLeft)
    static constexpr double kKnobLeft = 14.0, kKnobBeside = 136.0, kKnobStep = 64.0, kKnobTop = 28.0, kKnobW = 56.0, kKnobH = 64.0;
    // the first knob's left in a panel `width` wide holding `n` knobs only (panel coordinates)
    static constexpr double centredLeft (double width, int n) { return (width - (n - 1) * kKnobStep - kKnobW) / 2.0; }
    // a switch and `n` knobs beside it, centred in a panel `width` wide: the switch's left (panel coordinates;
    // the knobs from there + kKnobBeside - kSwitchLeft)
    static constexpr double centredSwitchLeft (double width, int n)
    {
        return (width - (kKnobBeside - kSwitchLeft + (n - 1) * kKnobStep + kKnobW)) / 2.0;
    }
    // the display (Classic; an arranged layout builds it narrower: kArrangedDisplayW)
    static constexpr double kDisplayTop = kSweepRow2 + kRowH + 8.0, kDisplayBottom = kDisplayTop + 190.0, kArrangedDisplayW = 400.0;
    static_assert (kDisplayBottom + 8.0 == kRow1, "the bands' display ends a gap above row 1");

    explicit Editor (Controller* c);
    void buildUI (VSTGUI::CFrame* f) override;
    // the panels for Menu > Layout (pluginkit/Layout.h): the Wide template's rows by purpose
    pk::layout::Spec layoutSpec (bool arranged) const override;
    // The Basic page (pluginkit/ui/BasicView.h): Loop Lock's window (LoopView); Input, Drive (SWEEP's) and Movement; Loop
    // Lock, Length and Sub Guard; Mix and Output; the end saturator in the extras
    pk::basic::Spec basicSpec () override;
    void idle () override;
    void paramChanged (uint32_t id) override;

private:
    std::unique_ptr<smacheratr::TailPanel> tail;
    std::unique_ptr<smacheratr::TailPanel> makeTail ();
    static smacheratr::TailBases tailBases () { return {kTailBase, kTailExtBase, kTailExt2Base, kTailExt3Base, kTailExt4Base}; }
    GestureView* makeGestureView (const VSTGUI::CRect& r);
    LoopView* makeLoopView (const VSTGUI::CRect& r);
    LoopView* loopView = nullptr;
    void buildRow6 (VSTGUI::CViewContainer* root);
    void onClose () override;
    void showMenu (VSTGUI::CPoint where);
    // High X, Air Level and Air Move dimmed with 3 Bands; Shift and Shift Mix while it is off; Seed B at Blend 0;
    // Liquid's Res, Low and High at Liquid 0; the SWEEP stage's controls while Sweep is off, the shelf's while High
    // Shelf is off, Tone while it is off, Clean Sub's and Sub Boost's while they are off, a bell's knobs while it is
    // off, its Rate while its Sync is on and its Sync Rate while it is off
    void updateLooks ();
    static bool affectsLooks (uint32_t id);
    // BELLS shows one bell's controls at a time
    void pickBell (int b);
    void showGestureFiles (VSTGUI::CPoint where); // File: the gesture files (the one gesture's User)
    void openGestureFolder ();                    // (made when it is missing)
    VSTGUI::CRect displayRect (bool arranged) const;

    Controller* ctl;
    BandView* display = nullptr;
    SweepView* sweepView = nullptr;
    std::vector<pk::ParamView*> sweepControls, shelfControls; // (everything in the stage but the Sweep switch; the shelf's knobs)
    std::vector<pk::ParamView*> splitControls, boostControls; // (Clean Sub's and Sub Boost's value boxes)
    pk::ParamView* toneKnob = nullptr;
    // per bell: every control BELLS shows for it (shown while it is picked), its knobs, Rate and Sync Rate
    std::vector<VSTGUI::CView*> bellViews[kNumBells];
    std::vector<pk::ParamView*> bellKnobs[kNumBells];
    pk::ParamView *rateViews[kNumBells] {}, *syncRateViews[kNumBells] {};
    int pickedBell = 0;
    // GESTURE: the controls dimmed with Gesture None, and Speed (dimmed in Loop)
    std::vector<pk::ParamView*> sceneControls;
    pk::ParamView* sceneSpeed = nullptr;
    std::vector<pk::Knob*> wobbleKnobs;
    GestureView* gestureView = nullptr;
    pk::Label* latencyLabel = nullptr;
    std::vector<pk::ParamView*> loopControls, paraControls, guardControls, driftKnobs; // (dimmed while their stage is off)
    pk::Knob *highXKnob = nullptr, *airLevelKnob = nullptr, *airMoveKnob = nullptr, *shiftKnob = nullptr, *shiftMixKnob = nullptr,
              *seedBKnob = nullptr;
    std::vector<pk::Knob*> liquidKnobs; // Res, Low, High
    // the LAB: the hosts its knobs show slots' values through (a slot's kind's table; POST's OTT Up and Down), every
    // control in it (repainted when a LAB parameter moves), and per chain (MID, HIGH, AIR; then POST) the controls of its
    // smacheratr, its multidyn and all of them (dimmed: the slot not holding that kind; Air with 3 bands)
    std::vector<std::unique_ptr<pk::ParamHost>> labHosts;
    std::vector<pk::ParamView*> labViews;
    std::vector<pk::ParamView*> labSat[kNumBandChains + 1], labOtt[kNumBandChains + 1], labAll[kNumBandChains + 1];
    void buildLab (VSTGUI::CViewContainer* root);
};

} // namespace moistr
