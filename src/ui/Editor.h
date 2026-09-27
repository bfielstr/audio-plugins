#pragma once

#include "Widgets.h"

#include "public.sdk/source/vst/vstguieditor.h"

#include <map>
#include <string>
#include <vector>

namespace simplr {

class Controller;
class WaveformView;
class FilterDisplay;
class EnvelopeDisplay;

class Editor : public Steinberg::Vst::VSTGUIEditor, public ParamHost
{
public:
    static constexpr double kWidth = 1110.0;
    static constexpr double kHeight = 724.0;

    explicit Editor (Controller* c);

    bool PLUGIN_API open (void* parent, const VSTGUI::PlatformType& platformType) override;
    void PLUGIN_API close () override;
    Steinberg::tresult PLUGIN_API canResize () override { return Steinberg::kResultTrue; }
    Steinberg::tresult PLUGIN_API checkSizeConstraint (Steinberg::ViewRect* rect) override;
    Steinberg::tresult PLUGIN_API onSize (Steinberg::ViewRect* newSize) override;
    VSTGUI::CMessageResult notify (VSTGUI::CBaseObject* sender, const char* message) override;

    void paramChanged (uint32_t id);
    void bridgeChanged ();

    // Builds the view hierarchy into an (unopened) frame. Used by open() and by the UI tests.
    void buildUI (VSTGUI::CFrame* f);
    void idle ();

    // ParamHost
    double norm (uint32_t id) override;
    double plainValue (uint32_t id) override;
    void beginEdit (uint32_t id) override;
    void setNorm (uint32_t id, double v) override;
    void endEdit (uint32_t id) override;
    std::string valueText (uint32_t id) override;

    // exposed for tests
    void setEnvTab (int t);
    void setTooltipsEnabled (bool on);
    void showMenu (VSTGUI::CPoint where);

private:
    template <typename T>
    T* bind (VSTGUI::CViewContainer* parent, T* view);
    void updateVisibility ();
    void browseForSample ();
    void stepSample (int dir);
    void loadFile (const std::string& path);
    void resizeTo (double scale);

    Controller* controller;
    std::map<uint32_t, std::vector<VSTGUI::CView*>> byParam;
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
    double scale = 1.0;
    std::string lastName;
};

} // namespace simplr
