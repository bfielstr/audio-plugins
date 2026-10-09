#pragma once

#include "smacheratr/src/ui/TailPanel.h"

#include "../core/Params.h"

#include "pluginkit/vst/EditorBase.h"

#include <memory>
#include <vector>

namespace wubr {

class Controller;
class BandView;
class ShapeView;

// The bands on top, a row of controls, the selected band's shape and knobs, then the end Smacheratr.
class Editor : public pk::EditorBase
{
public:
    static constexpr double kWidth = 900.0;
    // the end saturator's section (smacheratr::TailPanel, both parts open) at the bottom, from y 448, 8 px clear under it
    static constexpr double kHeight = 448.0 + smacheratr::TailPanel::kOpenHeight + 8.0;
    // layout (also used by the host test)
    static constexpr double kBandTop = 40.0, kBandBottom = 220.0, kRowTop = 228.0, kShapeTop = 258.0, kShapeBottom = 440.0;
    static constexpr double kShapeLeft = 8.0, kShapeRight = 592.0;

    explicit Editor (Controller* c);
    void buildUI (VSTGUI::CFrame* f) override;
    // the panels for Menu > Layout (pluginkit/Layout.h): the Wide template's rows by purpose
    pk::layout::Spec layoutSpec (bool arranged) const override;
    // The Basic page (pluginkit/ui/BasicView.h): the bands' display, Mode, each band's Depth; Dry/Wet and Output; the end saturator in the extras
    pk::basic::Spec basicSpec () override;
    void idle () override;
    void paramChanged (uint32_t id) override;
    void showBand (int band);

private:
    void onClose () override;
    void showMenu (VSTGUI::CPoint where);
    void updateLooks ();

    Controller* ctl;
    std::unique_ptr<smacheratr::TailPanel> tail;
    std::unique_ptr<smacheratr::TailPanel> makeTail ();
    static smacheratr::TailBases tailBases ();
    BandView* bands = nullptr;
    ShapeView* shapes[kBands] {};
    int shown = 0;
    std::vector<VSTGUI::CView*> bandViews[kBands], bandButtons;
    pk::ParamView* rateModeViews[kBands] {};
    pk::ParamView* syncViews[kBands] {};
    pk::ParamView* hzViews[kBands] {};
    std::vector<pk::ParamView*> envViews; // dimmed in LFO mode
    pk::ParamView* sensView = nullptr;
};

} // namespace wubr
