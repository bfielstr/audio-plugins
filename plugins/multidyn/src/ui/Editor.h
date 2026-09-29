#pragma once

#include "pluginkit/vst/EditorBase.h"

#include <vector>

namespace multidyn {

class Controller;
class DynDisplay;

// Laid out like Live's Multiband Dynamics: a Split column (band names, On, Solo, the crossover
// fields between the lanes), the Input knobs, the lanes with their Below / Above / Att/Rel value
// fields, the Output knobs, and the global Output / Time / Amount on the right.
class Editor : public pk::EditorBase
{
public:
    static constexpr double kWidth = 920.0;
    static constexpr double kHeight = 508.0;
    // layout (also used by the host test)
    static constexpr double kDisplayLeft = 166.0, kDisplayTop = 40.0, kDisplayRight = 760.0, kDisplayBottom = 336.0;
    static constexpr double kBandColLeft = 8.0, kInputColLeft = 104.0, kOutputColLeft = 766.0, kGlobalColLeft = 842.0;

    explicit Editor (Controller* c);
    void buildUI (VSTGUI::CFrame* f) override;
    void idle () override;
    void paramChanged (uint32_t id) override;
    // every edit from the editor: the thresholds of a band cannot cross (Thresholds.h)
    void setNorm (uint32_t id, double v) override;

private:
    void onClose () override;
    void updateLayout ();
    void showMenu (VSTGUI::CPoint where);

    Controller* ctl;
    DynDisplay* display = nullptr;
    pk::Label* nameLabels[4] {};
    VSTGUI::CView* onToggles[4] {}, * soloToggles[4] {}, * inputKnobs[4] {}, * outputKnobs[4] {};
    VSTGUI::CView* valueBoxes[4][6] {}; // below thr / ratio, above thr / ratio, attack, release
    VSTGUI::CView* xoverBoxes[3] {};
    pk::Label* scStatus = nullptr;
    pk::NumberBox* rmsWindowBox = nullptr; // dimmed with the Peak detector
};

} // namespace multidyn
