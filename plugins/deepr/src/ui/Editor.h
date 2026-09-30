#pragma once

#include "smacheratr/src/ui/TailDisplays.h"

#include <memory>

#include "pluginkit/vst/EditorBase.h"

namespace deepr {

class Controller;
class DeeprView;

class Editor : public pk::EditorBase
{
public:
    static constexpr double kWidth = 760.0;
    static constexpr double kHeight = 612.0 + 170.0; // the tail panel's displays (smacheratr::TailDisplays::kHeight)

    explicit Editor (Controller* c);
    void buildUI (VSTGUI::CFrame* f) override;
    void idle () override;
    void paramChanged (uint32_t id) override;

private:
    std::unique_ptr<smacheratr::TailDisplays> tailDisplays;
    void onClose () override;
    void showMenu (VSTGUI::CPoint where);

    Controller* ctl;
    DeeprView* display = nullptr;
    pk::Label* latencyLabel = nullptr;
};

} // namespace deepr
