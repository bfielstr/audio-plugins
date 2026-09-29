#pragma once

#include "smacheratr/src/ui/TailDisplays.h"

#include "../core/Params.h"
#include "../core/Tone.h"

#include "pluginkit/vst/EditorBase.h"

#include <array>
#include <memory>
#include <vector>

namespace multidyn {
class DynDisplay;
}

namespace detonatr {

class Controller;
class StageStrip;
class HitView;
class RecordingSlot;
class TransientCurve;

// The chain (a strip of the five stages) and the master knobs on top, the hit display, then the
// selected stage's controls.
class Editor : public pk::EditorBase
{
public:
    static constexpr double kWidth = 900.0;
    static constexpr double kHeight = 516.0;
    // layout (also used by the host test)
    static constexpr double kStripTop = 42.0, kStripBottom = 100.0, kStripRight = 712.0;
    static constexpr double kHitTop = 108.0, kHitBottom = 250.0;
    static constexpr double kStageTop = 258.0, kStageBottom = 506.0;

    explicit Editor (Controller* c);
    void buildUI (VSTGUI::CFrame* f) override;
    void idle () override;
    void paramChanged (uint32_t id) override;
    void showStage (int stage);
    int shownStage () const { return shown; }

private:
    void onClose () override;
    void showMenu (VSTGUI::CPoint where);
    void buildClean (VSTGUI::CViewContainer* p);
    void buildTone (VSTGUI::CViewContainer* p);
    void buildMultiband (VSTGUI::CViewContainer* p);
    void buildTransient (VSTGUI::CViewContainer* p);
    void updateMbLayout ();
    void updateRootNote ();
    void refreshRecordings ();
    void browseRecording (int slot);
    void loadRecording (int slot, const std::string& path);

    Controller* ctl;
    int shown = kStageTone;
    StageStrip* strip = nullptr;
    HitView* hit = nullptr;
    std::array<VSTGUI::CViewContainer*, kNumStages> panels {};
    std::unique_ptr<smacheratr::TailDisplays> tailDisplays;
    // Tone
    std::array<RecordingSlot*, kCarrierSlots> slots {};
    pk::Label* rootNote = nullptr;
    uint32_t recordingsSeen = ~0u;
    int idleCount = 0;
    // Multiband: Multidyn's controls on Detonatr's parameters
    std::unique_ptr<pk::MappedParamHost> mbHost;
    multidyn::DynDisplay* dyn = nullptr;
    pk::Label* mbNames[4] {};
    VSTGUI::CView *mbOn[4] {}, *mbSolo[4] {}, *mbIn[4] {}, *mbOut[4] {};
    VSTGUI::CView* mbBoxes[4][6] {};
    // Transient
    TransientCurve* curve = nullptr;
};

} // namespace detonatr
