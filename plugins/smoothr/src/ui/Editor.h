#pragma once

#include "smacheratr/src/ui/TailDisplays.h"

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
    static constexpr double kHeight = 660.0;
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
    std::unique_ptr<smacheratr::TailDisplays> tailDisplays;
    HistoryView* history = nullptr;
};

} // namespace smoothr
