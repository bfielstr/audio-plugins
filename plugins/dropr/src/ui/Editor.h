#pragma once

#include "smacheratr/src/ui/TailDisplays.h"

#include <memory>

#include "pluginkit/vst/EditorBase.h"

namespace dropr {

class Controller;
class ShapeView;

class Editor : public pk::EditorBase
{
public:
    static constexpr double kWidth = 760.0;
    static constexpr double kHeight = 514.0 + 170.0; // the tail panel's displays (smacheratr::TailDisplays::kHeight)

    explicit Editor (Controller* c);
    void buildUI (VSTGUI::CFrame* f) override;
    void idle () override;
    void paramChanged (uint32_t id) override;

private:
    std::unique_ptr<smacheratr::TailDisplays> tailDisplays;
    void onClose () override;
    void showMenu (VSTGUI::CPoint where);

    Controller* ctl;
    ShapeView* display = nullptr;
    pk::Label* latencyLabel = nullptr;
};

} // namespace dropr
