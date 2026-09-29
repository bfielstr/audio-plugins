#pragma once

#include "smacheratr/src/ui/TailDisplays.h"

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
    static constexpr double kHeight = 704.0;
    // layout (also used by the host test)
    static constexpr double kBandTop = 40.0, kBandBottom = 220.0, kRowTop = 228.0, kShapeTop = 258.0, kShapeBottom = 440.0;
    static constexpr double kShapeLeft = 8.0, kShapeRight = 592.0;

    explicit Editor (Controller* c);
    void buildUI (VSTGUI::CFrame* f) override;
    void idle () override;
    void paramChanged (uint32_t id) override;
    void showBand (int band);

private:
    void onClose () override;
    void showMenu (VSTGUI::CPoint where);
    void updateLooks ();

    Controller* ctl;
    std::unique_ptr<smacheratr::TailDisplays> tailDisplays;
    BandView* bands = nullptr;
    ShapeView* shapes[kBands] {};
    int shown = 0;
    std::vector<VSTGUI::CView*> bandViews[kBands], bandButtons;
    pk::ParamView* syncViews[kBands] {};
    pk::ParamView* hzViews[kBands] {};
    std::vector<pk::ParamView*> envViews; // dimmed in LFO mode
    pk::ParamView* sensView = nullptr;
};

} // namespace wubr
