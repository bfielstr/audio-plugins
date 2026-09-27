#pragma once

#include "pluginkit/vst/EditorBase.h"

namespace lowfocus {

class Controller;
class SpectrumView;

class Editor : public pk::EditorBase
{
public:
    static constexpr double kWidth = 760.0;
    static constexpr double kHeight = 440.0;

    explicit Editor (Controller* c);
    void buildUI (VSTGUI::CFrame* f) override;
    void idle () override;
    void paramChanged (uint32_t id) override;

private:
    void onClose () override;
    void showMenu (VSTGUI::CPoint where);

    Controller* ctl;
    SpectrumView* spectrum = nullptr;
    pk::Label* latencyLabel = nullptr;
};

} // namespace lowfocus
