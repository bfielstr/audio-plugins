#pragma once

#include "smacheratr/src/ui/TailPanel.h"

#include <memory>

#include "pluginkit/vst/EditorBase.h"

namespace locus {

class Controller;
class SpectrumView;

class Editor : public pk::EditorBase
{
public:
    static constexpr double kWidth = 760.0;
    // the end saturator's section (smacheratr::TailPanel, both parts open) at the bottom, from y 440, 8 px clear under it
    static constexpr double kHeight = 440.0 + smacheratr::TailPanel::kOpenHeight + 8.0;

    explicit Editor (Controller* c);
    void buildUI (VSTGUI::CFrame* f) override;
    void idle () override;
    void paramChanged (uint32_t id) override;

private:
    std::unique_ptr<smacheratr::TailPanel> tail;
    void onClose () override;
    void showMenu (VSTGUI::CPoint where);

    Controller* ctl;
    SpectrumView* spectrum = nullptr;
    pk::Label* latencyLabel = nullptr;
};

} // namespace locus
