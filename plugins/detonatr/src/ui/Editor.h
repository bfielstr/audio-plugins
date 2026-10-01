#pragma once

#include "smacheratr/src/ui/TailDisplays.h"

#include "../core/Params.h"

#include "pluginkit/vst/EditorBase.h"

#include <array>
#include <initializer_list>
#include <memory>

namespace detonatr {

class Controller;
class StageStrip;
class HitView;

// The chain (a strip of the ten stages and the Smacheratr at the end) and the master knobs on top,
// the hit display, then the selected stage's page (its controls and a display).
class Editor : public pk::EditorBase
{
public:
    static constexpr double kWidth = 900.0;
    static constexpr double kHeight = 516.0;
    // layout (also used by the host test)
    static constexpr double kStripLeft = 8.0, kStripTop = 42.0, kStripBottom = 100.0, kStripRight = 756.0;
    static constexpr double kHitTop = 108.0, kHitBottom = 250.0;
    static constexpr double kPageLeft = 8.0, kStageTop = 258.0, kStageBottom = 506.0;
    // inside a page: the On button, and the knobs' grid
    static constexpr double kKnobX = 10.0, kKnobY = 46.0, kKnobDx = 60.0, kKnobDy = 70.0;
    static constexpr int kPages = kNumStages + 1; // the stages, then the Smacheratr

    explicit Editor (Controller* c);
    void buildUI (VSTGUI::CFrame* f) override;
    void idle () override;
    void paramChanged (uint32_t id) override;
    void showPage (int page);
    int shownPage () const { return shown; }

private:
    void onClose () override;
    void showMenu (VSTGUI::CPoint where);
    // knobs in a grid from (x0, y0), perRow to a row
    void knobs (VSTGUI::CViewContainer* p, std::initializer_list<uint32_t> ids, int perRow, double x0 = kKnobX, double y0 = kKnobY,
                std::initializer_list<uint32_t> bipolar = {});
    void buildVocoder (VSTGUI::CViewContainer* p);
    void buildSpike (VSTGUI::CViewContainer* p);
    void buildMotion (VSTGUI::CViewContainer* p);
    void buildTransient (VSTGUI::CViewContainer* p, int i);
    void buildLimiter (VSTGUI::CViewContainer* p, int i);
    void buildComp (VSTGUI::CViewContainer* p, int i);
    void buildTape (VSTGUI::CViewContainer* p);

    Controller* ctl;
    int shown = kStageVocoder;
    StageStrip* strip = nullptr;
    HitView* hit = nullptr;
    std::array<VSTGUI::CViewContainer*, kPages> panels {};
    std::array<VSTGUI::CView*, kNumStages> displays {}; // each stage's display
    std::unique_ptr<smacheratr::TailDisplays> tailDisplays;
};

} // namespace detonatr
