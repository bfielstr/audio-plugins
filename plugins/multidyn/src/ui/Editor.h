#pragma once

#include "smacheratr/src/ui/TailPanel.h"

#include <memory>

#include "pluginkit/vst/EditorBase.h"

#include <vector>

namespace multidyn {

class Controller;
class DynDisplay;

// Laid out like Live's Multiband Dynamics: a Split column (band names, On, Solo, the crossover
// fields between the lanes), the Input knobs, the lanes with their Below / Above / Att/Rel value
// fields, the Output knobs, and the global Output / Time / Amount / Soften (and Soften's Color) on the
// right. The Sub band, when on, is a lane at the bottom (its Above and Att/Rel fields, its Output as a
// field); its On and Frequency sit in the bottom row with the crossovers' Slope. The Style (OTT /
// Character) sits in the top bar after the band count; in OTT style the controls only Character uses
// (Soft Knee, Peak/RMS, the RMS Window, Soften) look disabled.
class Editor : public pk::EditorBase
{
public:
    static constexpr double kWidth = 920.0;
    // the end saturator's section (smacheratr::TailPanel, both parts open) at the bottom, from y 424, 8 px clear under it
    static constexpr double kHeight = 424.0 + smacheratr::TailPanel::kOpenHeight + 8.0;
    // layout (also used by the host test)
    static constexpr double kDisplayLeft = 166.0, kDisplayTop = 40.0, kDisplayRight = 760.0, kDisplayBottom = 336.0;
    static constexpr double kBandColLeft = 8.0, kInputColLeft = 104.0, kOutputColLeft = 766.0, kGlobalColLeft = 842.0;
    // the second row under the display: Slope, the Sub band's On and Frequency; Soften's Color under Soften
    static constexpr double kRow2Top = 382.0;
    static constexpr double kSlopeLeft = 50.0, kSubOnLeft = 150.0, kSubFreqLeft = 204.0;
    static constexpr double kColorTop = 330.0;
    // the top bar's Style selector (OTT | Character); the side-chain status line under the second row
    static constexpr double kStyleLeft = 378.0, kStyleRight = 498.0, kStyleTop = 7.0;
    static constexpr double kScStatusTop = 404.0;

    explicit Editor (Controller* c);
    void buildUI (VSTGUI::CFrame* f) override;
    // the panels for Menu > Layout (pluginkit/Layout.h): the Wide template's rows by purpose
    pk::layout::Spec layoutSpec (bool arranged) const override;
    void idle () override;
    void paramChanged (uint32_t id) override;
    // every edit from the editor: the thresholds of a band cannot cross (Thresholds.h)
    void setNorm (uint32_t id, double v) override;

private:
    std::unique_ptr<smacheratr::TailPanel> tail;
    void onClose () override;
    void updateLayout ();
    void showMenu (VSTGUI::CPoint where);
    void updateLooks (); // the Character-only controls in OTT style; the RMS Window with the Peak detector

    Controller* ctl;
    DynDisplay* display = nullptr;
    pk::Label* nameLabels[4] {};
    VSTGUI::CView* onToggles[4] {}, * soloToggles[4] {}, * inputKnobs[4] {}, * outputKnobs[4] {};
    VSTGUI::CView* valueBoxes[4][6] {}; // below thr / ratio, above thr / ratio, attack, release
    VSTGUI::CView* xoverBoxes[3] {};
    pk::Label* subName = nullptr;
    VSTGUI::CView* subBoxes[4] {}; // threshold, ratio, attack, release
    VSTGUI::CView* subInBox = nullptr;
    VSTGUI::CView* subOutBox = nullptr;
    pk::Label* scStatus = nullptr;
    pk::NumberBox* rmsWindowBox = nullptr; // dimmed with the Peak detector (and in OTT style)
    pk::ParamView *softKneeToggle = nullptr, *detectorSeg = nullptr, *softenKnob = nullptr; // Character only
};

} // namespace multidyn
