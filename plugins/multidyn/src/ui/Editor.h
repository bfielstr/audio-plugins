#pragma once

#include "pluginkit/vst/EditorBase.h"

namespace multidyn {

class Controller;
class DynDisplay;

class Editor : public pk::EditorBase
{
public:
    static constexpr double kWidth = 900.0;
    static constexpr double kHeight = 484.0;

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
    VSTGUI::CViewContainer* columns[3] {};
    std::vector<VSTGUI::CView*> tabButtons;
    pk::Label* scStatus = nullptr;
    int column = 1;
};

} // namespace multidyn
