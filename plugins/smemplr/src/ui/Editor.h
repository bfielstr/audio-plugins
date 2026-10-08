#pragma once

#include "Modulation.h"
#include "Params.h"
#include "UiKit.h"

#include "para/src/ui/GainLock.h"
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
class ThresholdSlider;
}
namespace widr {
class GonioView;
}
namespace wubr {
class BandView;
class ShapeView;
}
namespace levlr {
class LevelView;
}
namespace smoothr {
class HistoryView;
}
namespace gentlr {
class GentlrView;
}
namespace pk {
class ScopeView;
}

namespace smemplr {

class Controller;
class WaveformView;
class FilterDisplay;
class EnvelopeDisplay;
class MsView;
class ModOverlay;
class ModList;

class Editor : public pk::EditorBase
{
public:
    // every edit from the editor: Multidyn's thresholds of a band cannot cross
    void setNorm (uint32_t id, double v) override;
    // the modulation section is the column at the right (kModLeft ..)
    static constexpr double kWidth = 1328.0, kModLeft = 1108.0;
    // the modulation column split in two in an arranged layout (Menu > Layout): the LFOs two by two, and
    // the mappings' list under them (where they are built: right of the Classic window)
    static constexpr double kModSplitW = 424.0, kModSplitH = 256.0, kMapTop = 300.0, kMapH = 24.0 + 24 * 17.0 + 8.0;
    static constexpr double kHeight = 1012.0;
    // the effects rack at the bottom (also used by the host test): the slots' tabs in chain order (drag
    // one sideways to move the effect) and "+"; the selected slot's controls; its panel
    static constexpr double kFxTabTop = 722.0, kFxTabWidth = 96.0, kFxCtlTop = 746.0, kFxPanelTop = 770.0;
    static constexpr int kFxNone = kRackSlots; // setFxTab: no slot (the rack is empty)

    explicit Editor (Controller* c);

    void buildUI (VSTGUI::CFrame* f) override;
    // the panels for Menu > Layout (pluginkit/Layout.h): the Wide template's two rows, instrument and effects
    pk::layout::Spec layoutSpec (bool arranged) const override;
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
    // A new slot of an effect (addFx), after its factory defaults: the effect's own plug-in's saved default
    // (Save as Default) and its Menu > Defaults switches, as a new instance of the plug-in gets them
    // (plugin/RackPresetIO.h). Only for a slot the user adds: a loaded project keeps its values.
    void applyNewSlotDefaults (int slot, int type);
    void removeFx (int slot);
    void moveFx (int from, int to);
    void duplicateFx (int from, int at); // a copy of slot `from` at slot `at` (the ones from there move up one)
    // Copy / Paste on a rack page: the slot's effect settings as text on the clipboard, the same text
    // as the effect's own plug-in copies (its menu's Copy / Paste Settings), so settings go both ways
    // between a plug-in and a slot. The parameters the rack does not use (rackHiddenParams) are left out.
    std::string slotSettingsText (int slot);
    bool applySlotSettingsText (int slot, const std::string& text); // false: not this slot's effect
    void showMenu (VSTGUI::CPoint where);

    // --- the modulation (ModView.h; the mappings live in the Bridge, which the state saves) ---
    ModMap modMap ();                               // the mappings now (none without a processor)
    bool addMod (int lfo, uint32_t target);         // a new mapping, at kModDropDepth (false: cannot)
    void removeMod (size_t index);
    void setModDepth (size_t index, double depth);  // (held to -1 .. 1)
    // An LFO's handle dragged to `where` (frame coordinates): the control under it is framed; let go
    // there (drop), the LFO modulates it.
    void lfoDragged (int lfo, VSTGUI::CPoint where, bool drop);
    void lfoDragCancelled ();
    // the parameter a control at `where` is bound to, if an LFO may modulate it (-1: none); its rect
    int64_t modTargetAt (VSTGUI::CPoint where, VSTGUI::CRect* rect = nullptr);
    std::string modTargetName (const ModMapping& m);
    bool modOffsetNow (size_t index, float& offset); // false: not working (or not playing yet)
    float lfoValueNow (int lfo);
    float lfoPhaseNow (int lfo);
    int lfoShapeNow (int lfo);
    double lfoPhaseOffset (int lfo); // Phase, 0 .. 1
    static constexpr double kModDropDepth = 0.25;

private:
    void setMods (ModMap m);
    void remapMods (const std::array<int, kRackSlots>& newSlot); // the rack's effects moved (Modulation.h)
    void updateModRings ();
    ModOverlay* modOverlay = nullptr;
    ModList* modList = nullptr;
    std::vector<VSTGUI::CView*> modLive;            // the LFOs' handles and scopes (repainted in idle)
    std::array<VSTGUI::CView*, kModLfos> modRateKnobs {};
    uint64_t modListShown = ~0ull; // what the list shows (the mappings' count of changes, which work)

    std::set<uint32_t> rackPageParams; // the effect parameters with a control on the current rack page
    void updateVisibility ();
    void updateMdLayout ();
    void rebuildRack ();   // the tabs and the selected slot's controls and panel
    void buildBody ();     // the selected slot's panel
    void clearBody ();     // takes the panel's views away (before the rack's slots change under them)
    // dragging a slot's tab: `pos` is its place in the row, `target` where it would land (the marker
    // shows it); dropped there, the effect moves
    void dragTab (int pos, int target, bool copy);
    void dropTab (int pos, int target, bool copy);
    void moveOldEndIntoRack (); // an old project's saturator after a full rack, into the rack
    void showAddMenu (VSTGUI::CPoint where);
    void copySlot (int from, int to);
    void browseForSample ();
    void stepSample (int dir);
    void loadFile (const std::string& path);
    void syncLoopLength ();      // (the loop's Sync: Length shows the loop at the root note)
    std::vector<double> syncKey; // what Length was last set from

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
    // the playheads' Spread and each one's Channel (and number): dimmed beyond Playheads
    ParamView* headSpreadKnob = nullptr;
    std::array<ParamView*, kMaxPlayheads> headChannels {};
    std::array<Label*, kMaxPlayheads> headLabels {};
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
    // the Para page's Gain Locks: in front of its slot's host for the gains (made with the page, dropped
    // with it)
    std::unique_ptr<para::GainLockHost> paraLock;
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
    // Gentlr's (Clarity's) band selector on the Smacheratr page: the band shown (its knobs with Gentlr's
    // layer in front of the colour display, its handle and Threshold slider lit)
    int clarityBand = 0;
    std::vector<VSTGUI::CView*> rackBandViews[4], rackBandButtons;
    std::vector<VSTGUI::CView*> rackColorKnobs; // the colour amounts (with Color in front)
    VSTGUI::CView* rackLayerSwitch = nullptr;   // Color | Gentlr, above the colour display
    void showClarityBand (int band);
    void setSatLayer (int layer); // 0 Color, 1 Gentlr (kept per instance: the controller's uiColorLayer)
    // the Gentlr page: a band is selected by grabbing any of its controls (through gentlrWatch, in front of
    // the page's host); its handle is lit and so is its Threshold
    std::unique_ptr<pk::WatchedParamHost> gentlrWatch;
    pk::NumberBox* gentlrThresh[4] = {nullptr, nullptr, nullptr, nullptr};
    int gentlrBand = 0;
    void selectGentlrBand (int band);
    // Gentlr's Advanced mode on the Smacheratr page: the Threshold sliders at the right of the colour
    // display and the region Drive's controls, shown while Advanced is on (satHost: the page's host)
    smacheratr::ThresholdSlider* fxThresholds[4] = {nullptr, nullptr, nullptr, nullptr};
    std::vector<pk::ParamView*> fxSatAdvanced;
    pk::MappedParamHost* satHost = nullptr;
    void updateSatAdvanced ();
    widr::GonioView* fxGonio = nullptr;
    // the Wubr page: its band display, both bands' shapes (stacked, always shown), both bands'
    // controls (the selected band's shown), each band's rate controls (band 1's while Link Rates is
    // on, else the selected band's; Sync or Hz by its rate mode), and the controls only Envelope mode uses
    wubr::BandView* wubrBands = nullptr;
    levlr::LevelView* levlrView = nullptr; // the Levlr page's band display
    smoothr::HistoryView* smoothrView = nullptr; // the Smoothr page's history
    gentlr::GentlrView* gentlrView = nullptr;    // the Gentlr page's band display
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
    // the Sub band's lane (shown while it is on): its name, Output, and threshold, ratio, attack, release
    VSTGUI::CView *mdSubName = nullptr, *mdSubIn = nullptr, *mdSubOut = nullptr, *mdSubBoxes[4] {};
    // the controls only Multidyn's Character style uses (Soft Knee, Peak/RMS, Soften) and the RMS Window:
    // a disabled look in OTT style (the window also with the Peak detector)
    std::vector<ParamView*> mdCharViews;
    ParamView* mdRmsWindow = nullptr;
    void updateMdLooks ();
};

} // namespace smemplr
