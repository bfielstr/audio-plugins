#pragma once

#include "Params.h"
#include "UiKit.h"

#include "pluginkit/vst/EditorBase.h"

#include <array>
#include <map>
#include <memory>
#include <string>
#include <set>
#include <vector>

namespace para {
class FilterView;
}
namespace multidyn {
class DynDisplay;
}
namespace smacheratr {
class ShaperView;
class ColorView;
}
namespace widr {
class GonioView;
}
namespace wubr {
class BandView;
class ShapeView;
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
    static constexpr double kHeight = 1012.0;
    // the effects rack at the bottom (also used by the host test): the slots' tabs in chain order (drag
    // one sideways to move the effect) and "+"; the selected slot's controls; its panel
    static constexpr double kFxTabTop = 722.0, kFxTabWidth = 96.0, kFxCtlTop = 746.0, kFxPanelTop = 770.0;
    static constexpr int kFxNone = kRackSlots; // setFxTab: no slot (the rack is empty)

    explicit Editor (Controller* c);

    void buildUI (VSTGUI::CFrame* f) override;
    void idle () override;
    void paramChanged (uint32_t id) override;
    void bridgeChanged ();

    // exposed for tests
    void setEnvTab (int t);
    void setFxTab (int t); // a slot, or kFxNone
    // the rack, as the user edits it (tests use these too): add an effect after the chain; take one
    // out (the ones after it move up); move the effect in slot `from` to slot `to` (the ones between
    // move over by one, towards where it was)
    void addFx (int type);
    void removeFx (int slot);
    void moveFx (int from, int to);
    void showMenu (VSTGUI::CPoint where);

private:
    std::set<uint32_t> rackPageParams; // the effect parameters with a control on the current rack page
    void updateVisibility ();
    void updateMdLayout ();
    void rebuildRack ();   // the tabs and the selected slot's controls and panel
    void buildBody ();     // the selected slot's panel
    void clearBody ();     // takes the panel's views away (before the rack's slots change under them)
    // dragging a slot's tab: `pos` is its place in the row, `target` where it would land (the marker
    // shows it); dropped there, the effect moves
    void dragTab (int pos, int target);
    void dropTab (int pos, int target);
    void moveOldEndIntoRack (); // an old project's saturator after a full rack, into the rack
    void showAddMenu (VSTGUI::CPoint where);
    void copySlot (int from, int to);
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
    int envTab = 0, fxTab = 0;
    std::string lastName;

    // the effects rack: the plug-ins' own displays on this plug-in's parameters, through a host per
    // slot that maps the effect's IDs onto the slot's block (made anew when the slot's effect changes).
    // A replaced host is kept until the panel is rebuilt: the views on screen may still hold it (they
    // would draw with a deleted host before the next idle otherwise).
    std::array<std::unique_ptr<pk::MappedParamHost>, kRackSlots> slotHosts;
    std::vector<std::unique_ptr<pk::MappedParamHost>> retiredHosts;
    std::array<int, kRackSlots> slotHostType {}, shownTypes {};
    pk::MappedParamHost* hostFor (int slot);
    bool rackDirty = false;
    VSTGUI::CViewContainer *fxRow = nullptr, *fxCtl = nullptr, *fxBody = nullptr;
    std::vector<int> tabSlots;             // the used slots, in the row's order
    VSTGUI::CView* fxDropMark = nullptr;   // where a dragged tab lands
    para::FilterView* fxFilterView = nullptr;
    multidyn::DynDisplay* fxDynDisplay = nullptr;
    smacheratr::ShaperView* fxShaperView = nullptr; // the selected slot's
    smacheratr::ColorView* fxColorView = nullptr;
    // Clarity's band selector on the Smacheratr page: the band shown
    int clarityBand = 0;
    std::vector<VSTGUI::CView*> rackBandViews[2], rackBandButtons;
    void showClarityBand (int band);
    widr::GonioView* fxGonio = nullptr;
    // the Wubr page: its band display, both bands' shapes (stacked, always shown), both bands'
    // controls (the selected band's shown), each band's rate controls (band 1's while Link Rates is
    // on, else the selected band's; Sync or Hz by its rate mode), and the controls only Envelope mode uses
    wubr::BandView* wubrBands = nullptr;
    wubr::ShapeView* wubrShapes[2] {};
    int wubrBand = 0;
    std::vector<VSTGUI::CView*> wubrBandViews[2], wubrBandButtons;
    VSTGUI::CView *wubrRateMode[2] {}, *wubrSync[2] {}, *wubrHz[2] {};
    std::vector<ParamView*> wubrEnvViews;
    ParamView* wubrSensView = nullptr;
    void showWubrBand (int band);
    void updateWubrLooks (); // the rate controls shown (Link, rate mode); the Envelope controls' look
    MsView* msView = nullptr;
    pk::ParamHost* mdLayoutHost = nullptr;
    pk::ScopeView* scope = nullptr;
    Label* mdNames[4] {};
    VSTGUI::CView *mdOn[4] {}, *mdSolo[4] {}, *mdIn[4] {}, *mdOut[4] {}, *mdBoxes[4][6] {};
};

} // namespace smempler
