#pragma once

#include "UiKit.h"

#include "pluginkit/vst/EditorBase.h"

#include <map>
#include <string>
#include <vector>

namespace simplr {

class Controller;
class WaveformView;
class FilterDisplay;
class EnvelopeDisplay;

class Editor : public pk::EditorBase
{
public:
    static constexpr double kWidth = 1110.0;
    static constexpr double kHeight = 724.0;

    explicit Editor (Controller* c);

    void buildUI (VSTGUI::CFrame* f) override;
    void idle () override;
    void paramChanged (uint32_t id) override;
    void bridgeChanged ();

    // exposed for tests
    void setEnvTab (int t);
    void showMenu (VSTGUI::CPoint where);

private:
    void updateVisibility ();
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
    std::vector<VSTGUI::CView*> tabButtons;
    int envTab = 0;
    std::string lastName;
};

} // namespace simplr
