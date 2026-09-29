#pragma once

#include "UiKit.h"

#include "pluginkit/vst/EditorBase.h"

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace para {
class FilterView;
}
namespace multidyn {
class DynDisplay;
}
namespace smacheratr {
class ShaperView;
}
namespace pk {
class ScopeView;
}

namespace smempler {

class Controller;
class WaveformView;
class FilterDisplay;
class EnvelopeDisplay;
class MsView;

class Editor : public pk::EditorBase
{
public:
    // every edit from the editor: Multidyn's thresholds of a band cannot cross
    void setNorm (uint32_t id, double v) override;
    static constexpr double kWidth = 1110.0;
    static constexpr double kHeight = 988.0;
    // the effects strip at the bottom (also used by the host test)
    static constexpr int kFxTabs = 4; // Para, Multidyn, M/S EQ, Smacheratr
    static constexpr double kFxTabTop = 722.0, kFxTabWidth = 104.0;

    explicit Editor (Controller* c);

    void buildUI (VSTGUI::CFrame* f) override;
    void idle () override;
    void paramChanged (uint32_t id) override;
    void bridgeChanged ();

    // exposed for tests
    void setEnvTab (int t);
    void setFxTab (int t);
    void showMenu (VSTGUI::CPoint where);

private:
    void updateVisibility ();
    void updateMdLayout ();
    void browseForSample ();
    void stepSample (int dir);
    void loadFile (const std::string& path);

    void onClose () override;

    Controller* ctl;
    WaveformView* waveform = nullptr;
    FilterDisplay* filterDisplay = nullptr;
    EnvelopeDisplay* envDisplay = nullptr;
    Label* nameLabel = nullptr;
    Label* bpmLabel = nullptr;
    Label* warpInfo = nullptr;
    Label* hostLabel = nullptr;
    ActionButton* warpBeatsBox = nullptr;
    VSTGUI::CViewContainer *classicGroup = nullptr, *oneShotGroup = nullptr, *sliceGroup = nullptr;
    VSTGUI::CViewContainer *warpOnGroup = nullptr, *warpOffGroup = nullptr;
    VSTGUI::CViewContainer *beatsGroup = nullptr, *tonesGroup = nullptr, *textureGroup = nullptr, *cproGroup = nullptr;
    VSTGUI::CView *sensKnob = nullptr, *divisionChoice = nullptr, *regionsChoice = nullptr, *manualHint = nullptr;
    VSTGUI::CViewContainer* slicePolyGroup = nullptr;
    VSTGUI::CView *driveKnob = nullptr, *morphKnob = nullptr;
    VSTGUI::CView *lfoRateHz = nullptr, *lfoRateSync = nullptr;
    VSTGUI::CViewContainer* envTabs[3] {};
    VSTGUI::CView *ampLoopTime = nullptr, *ampLoopRate = nullptr;
    std::vector<VSTGUI::CView*> tabButtons, fxTabButtons;
    int envTab = 0, fxTab = 0;
    std::string lastName;

    // the effects strip: the plug-ins' own displays on this plug-in's parameters
    std::unique_ptr<pk::MappedParamHost> paraHost, mdHost, satHost;
    VSTGUI::CViewContainer* fxTabs[kFxTabs] {};
    para::FilterView* fxFilterView = nullptr;
    multidyn::DynDisplay* fxDynDisplay = nullptr;
    smacheratr::ShaperView* fxShaperView = nullptr;
    MsView* msView = nullptr;
    pk::ScopeView* scope = nullptr;
    Label* mdNames[4] {};
    VSTGUI::CView *mdOn[4] {}, *mdSolo[4] {}, *mdIn[4] {}, *mdOut[4] {}, *mdBoxes[4][6] {};
};

} // namespace smempler
