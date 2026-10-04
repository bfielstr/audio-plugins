#pragma once

#include "smacheratr/src/ui/TailPanel.h"

#include "../core/Params.h"

#include "pluginkit/vst/EditorBase.h"

#include <memory>

namespace smoothr {

class Controller;
class HistoryView;

// The history and meters on top, the limiter's controls under them, then the Smacheratr that feeds it.
class Editor : public pk::EditorBase
{
public:
    static constexpr double kWidth = 900.0;
    // the end saturator's section (smacheratr::TailPanel, both parts open) at the bottom, from y 408, 8 px clear under it
    static constexpr double kHeight = 408.0 + smacheratr::TailPanel::kOpenHeight + 8.0;
    // layout (also used by the host test)
    static constexpr double kViewLeft = 8.0, kViewTop = 40.0, kViewRight = 892.0, kViewBottom = 300.0;
    static constexpr double kRowTop = 308.0, kTailTop = 408.0;

    explicit Editor (Controller* c);
    void buildUI (VSTGUI::CFrame* f) override;
    void idle () override;
    void paramChanged (uint32_t id) override;

private:
    void onClose () override;
    void showMenu (VSTGUI::CPoint where);

    Controller* ctl;
    std::unique_ptr<smacheratr::TailPanel> tail;
    HistoryView* history = nullptr;
};

} // namespace smoothr
