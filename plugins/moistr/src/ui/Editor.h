#pragma once

#include "smacheratr/src/ui/TailPanel.h"

#include <memory>
#include <vector>

#include "pluginkit/vst/EditorBase.h"

namespace moistr {

class Controller;
class BandView;
class SweepView;

class Editor : public pk::EditorBase
{
public:
    // Classic: two rows of the SWEEP stage at the top (SWEEP, BELL A, BELL B over HIGH SHELF and the sweep
    // display), the bands' display across, three rows of panels (SPLIT, LEVELS, SHIFT, GLUE over MOVEMENT,
    // BAND MOVE, RISE / FALL, OUTPUT over SEED B / LINK, LOW, EXTREME, LIQUID), the end saturator's section at
    // the bottom
    static constexpr double kWidth = 1132.0;
    static constexpr double kRowH = 116.0;
    static constexpr double kSweepRow1 = 40.0, kSweepRow2 = kSweepRow1 + kRowH + 8.0;
    static constexpr double kRow1 = kSweepRow2 + kRowH + 8.0 + 198.0, kRow2 = kRow1 + kRowH + 8.0, kRow3 = kRow2 + kRowH + 8.0;
    static constexpr double kTailTop = kRow3 + kRowH + 8.0;
    static constexpr double kHeight = kTailTop + smacheratr::TailPanel::kOpenHeight + 8.0;
    // the panels' places (left, right) in the Classic layout, for the host test
    static constexpr double kSweepLeft = 8.0, kSweepRight = 214.0;          // sweep row 1: Sweep, Drive
    static constexpr double kBellALeft = 222.0, kBellARight = 669.0;        // sweep row 1: Sync, Sync Rate, Phase; Rate, Low, High, Gain, Width
    static constexpr double kBellBLeft = 677.0, kBellBRight = 1124.0;       // sweep row 1: as BELL A
    static constexpr double kShelfLeft = 8.0, kShelfRight = 662.0;          // sweep row 2: High Shelf; Rate, Low, High, Min, Max, Q, Wander, Tilt
    static constexpr double kSweepViewLeft = 670.0, kSweepViewRight = 1124.0; // sweep row 2: the sweep display
    // a bell's compact column (Sync, Sync Rate, Phase; panel coordinates) and its first knob's left
    static constexpr double kBellColW = 99.0, kBellKnobLeft = 121.0;
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
    void idle () override;
    void paramChanged (uint32_t id) override;

private:
    std::unique_ptr<smacheratr::TailPanel> tail;
    void onClose () override;
    void showMenu (VSTGUI::CPoint where);
    // High X, Air Level and Air Move dimmed with 3 Bands; Shift and Shift Mix while it is off; Seed B at Blend 0;
    // Liquid's Res, Low and High at Liquid 0; the SWEEP stage's controls while Sweep is off, the shelf's while High
    // Shelf is off, a bell's Rate while its Sync is on and its Sync Rate while it is off
    void updateLooks ();
    VSTGUI::CRect displayRect (bool arranged) const;

    Controller* ctl;
    BandView* display = nullptr;
    SweepView* sweepView = nullptr;
    std::vector<pk::ParamView*> sweepControls, shelfControls; // (everything in the stage but the Sweep switch; the shelf's knobs)
    pk::ParamView *rateViews[2] {}, *syncRateViews[2] {};
    pk::Label* latencyLabel = nullptr;
    pk::Knob *highXKnob = nullptr, *airLevelKnob = nullptr, *airMoveKnob = nullptr, *shiftKnob = nullptr, *shiftMixKnob = nullptr,
              *seedBKnob = nullptr;
    std::vector<pk::Knob*> liquidKnobs; // Res, Low, High
};

} // namespace moistr
