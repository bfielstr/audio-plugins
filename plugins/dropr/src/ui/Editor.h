#pragma once

#include <memory>
#include <string>

#include "pluginkit/vst/EditorBase.h"

namespace dropr {

class Controller;
class BandView;

class Editor : public pk::EditorBase
{
public:
    static constexpr double kWidth = 760.0;
    // the LEVELS row at the bottom (to y 516), 8 px clear under it
    static constexpr double kHeight = 524.0;
    // the layout (the host test clicks on it)
    static constexpr double kDisplayLeft = 8.0, kDisplayTop = 40.0, kDisplayRight = 620.0, kDisplayBottom = 300.0;
    static constexpr double kSideLeft = 628.0, kSideTop = 40.0;           // the right column; its Bands selector at
    static constexpr double kBandsSelLeft = 8.0, kBandsSelTop = 34.0, kBandsSelW = 108.0; // (in the column)
    static constexpr double kDynLeft = 8.0, kDynTop = 308.0, kDynColumn = 66.0, kDynFirst = 10.0; // the knob row
    static constexpr double kNegToggleTop = 89.0;                          // the Negative toggle (in the row, column 4)
    // LEVELS (from y 428): Input, Dry/Wet and Output, then Soft Clip and Hard Clip side by side (the end of the chain)
    static constexpr double kLevelsTop = 428.0, kClipLeft = 270.0, kClipTop = 22.0, kClipW = 96.0, kClipH = 19.0;

    explicit Editor (Controller* c);
    void buildUI (VSTGUI::CFrame* f) override;
    // the panels for Menu > Layout (pluginkit/Layout.h): the Wide template's rows by purpose
    pk::layout::Spec layoutSpec (bool arranged) const override;
    // The Basic page (pluginkit/ui/BasicView.h): the bands' display, Bands, the down and up thresholds and Release; Mix, Output,
    // Soft Clip and Hard Clip
    pk::basic::Spec basicSpec () override;
    void idle () override;
    void paramChanged (uint32_t id) override;
    std::string valueText (uint32_t id) override; // the Negative Ratio reads "1 : -x"
    BandView* bandView () const { return display; }

private:
    void onClose () override;
    void showMenu (VSTGUI::CPoint where);

    Controller* ctl;
    void showRatio (); // the ratio knob of the mode in use
    BandView* display = nullptr;
    pk::Knob* ratioKnob = nullptr;
    pk::Knob* negRatioKnob = nullptr;
    pk::Knob* rangeKnob = nullptr;
    pk::Label* latencyLabel = nullptr;
};

} // namespace dropr
