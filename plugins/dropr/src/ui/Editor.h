#pragma once

#include "smacheratr/src/ui/TailPanel.h"

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
    // the end saturator's section (smacheratr::TailPanel, both parts open) at the bottom, from y 524, 8 px clear under it
    static constexpr double kHeight = 524.0 + smacheratr::TailPanel::kOpenHeight + 8.0;
    // the layout (the host test clicks on it)
    static constexpr double kDisplayLeft = 8.0, kDisplayTop = 40.0, kDisplayRight = 620.0, kDisplayBottom = 300.0;
    static constexpr double kSideLeft = 628.0, kSideTop = 40.0;           // the right column; its Bands selector at
    static constexpr double kBandsSelLeft = 8.0, kBandsSelTop = 34.0, kBandsSelW = 108.0; // (in the column)
    static constexpr double kDynLeft = 8.0, kDynTop = 308.0, kDynColumn = 66.0, kDynFirst = 10.0; // the knob row
    static constexpr double kNegToggleTop = 89.0;                          // the Negative toggle (in the row, column 4)

    explicit Editor (Controller* c);
    void buildUI (VSTGUI::CFrame* f) override;
    // the panels for Menu > Layout (pluginkit/Layout.h): the Wide template's rows by purpose
    pk::layout::Spec layoutSpec (bool arranged) const override;
    void idle () override;
    void paramChanged (uint32_t id) override;
    std::string valueText (uint32_t id) override; // the Negative Ratio reads "1 : -x"
    BandView* bandView () const { return display; }

private:
    std::unique_ptr<smacheratr::TailPanel> tail;
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
