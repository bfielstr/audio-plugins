#pragma once

#include "Params.h"

#include "pluginkit/vst/EditorBase.h"

namespace bubr {

class Controller;
class BubrView;

class Editor : public pk::EditorBase
{
public:
    // Classic: the display across the top (the effect's response at the left, the gate's curve at the right);
    // under it BAND, EFFECT, GATE and OUTPUT side by side. An arranged layout (Wide) builds the display narrower
    // and as tall as the panels' row, beside them.
    static constexpr double kWidth = 760.0;
    static constexpr double kTop = 40.0, kDisplayBottom = 240.0;
    static constexpr double kArrangedDisplayW = 420.0;
    static constexpr double kRowTop = 248.0, kRowBottom = 464.0;
    static constexpr double kHeight = kRowBottom + 8.0;
    // the panels' places (left, right)
    static constexpr double kBandLeft = 8.0, kBandRight = 160.0;
    static constexpr double kEffLeft = 168.0, kEffRight = 320.0;
    static constexpr double kGateLeft = 328.0, kGateRight = 480.0;
    static constexpr double kOutLeft = 488.0, kOutRight = 752.0;
    // inside a panel: the knobs in two rows of two, then the switches
    static constexpr double kKnobW = 56.0, kKnobH = 64.0;
    static constexpr double kKnobTop = 30.0, kKnobRow2 = 112.0;
    static constexpr double kKnobX[2] = {16.0, 80.0};
    static constexpr double kSwitchH = 19.0;
    static constexpr double kChoiceH = 34.0;

    explicit Editor (Controller* c);
    void buildUI (VSTGUI::CFrame* f) override;
    pk::layout::Spec layoutSpec (bool arranged) const override;
    // The Basic page (pluginkit/ui/BasicView.h): the display; Boost, Freq and Intensity, Q-Factor, Drive and
    // Gate; Dry/Wet, Output and the clips
    pk::basic::Spec basicSpec () override;
    void idle () override;
    void paramChanged (uint32_t id) override;

private:
    void onClose () override;
    void showMenu (VSTGUI::CPoint where);
    BubrView* makeDisplay (const VSTGUI::CRect& r);
    VSTGUI::CRect displayRect (bool arranged) const;

    Controller* ctl;
    BubrView* display = nullptr;
    pk::Label* latencyLabel = nullptr;
};

} // namespace bubr
