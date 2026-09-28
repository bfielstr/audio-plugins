#pragma once

#include "pluginkit/vst/EditorBase.h"

namespace perrera {

class Controller;
class FilterView;

// The display on top, one row of controls below it as in Live's devices.
class Editor : public pk::EditorBase
{
public:
    static constexpr double kWidth = 760.0;
    static constexpr double kHeight = 440.0;
    // layout (also used by the host test)
    static constexpr double kViewLeft = 8.0, kViewTop = 40.0, kViewRight = 752.0, kViewBottom = 300.0;

    explicit Editor (Controller* c);
    void buildUI (VSTGUI::CFrame* f) override;
    void idle () override;
    void paramChanged (uint32_t id) override;

private:
    void onClose () override;
    void showMenu (VSTGUI::CPoint where);

    Controller* ctl;
    FilterView* view = nullptr;
};

} // namespace perrera
