#pragma once

#include "pluginkit/vst/EditorBase.h"

namespace multidyn {

class Controller;
class DynDisplay;

class Editor : public pk::EditorBase
{
public:
    static constexpr double kWidth = 920.0;
    static constexpr double kHeight = 580.0;
    // layout (also used by the host test)
    static constexpr double kDisplayTop = 40.0, kDisplayBottom = 260.0;
    static constexpr double kColumnX = 10.0, kColumnW = 180.0, kColumnStep = 240.0, kColumnTop = 268.0;

    explicit Editor (Controller* c);
    void buildUI (VSTGUI::CFrame* f) override;
    void idle () override;
    void paramChanged (uint32_t id) override;
    void setColumn (int mode); // 0 time, 1 below, 2 above

private:
    void onClose () override;
    void updateVisibility ();
    void showMenu (VSTGUI::CPoint where);

    Controller* ctl;
    DynDisplay* display = nullptr;
    VSTGUI::CViewContainer* bandColumns[4] {};
    VSTGUI::CViewContainer* modeGroups[4][3] {};
    VSTGUI::CView* xoverKnobs[3] {};
    pk::Label* rangeLabels[4] {};
    std::vector<VSTGUI::CView*> tabButtons;
    pk::Label* scStatus = nullptr;
    int column = 1;
};

} // namespace multidyn
