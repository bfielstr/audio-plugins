#pragma once

#include "smacheratr/src/ui/TailDisplays.h"

#include <memory>

#include "pluginkit/vst/EditorBase.h"

namespace locus {

class Controller;
class SpectrumView;

class Editor : public pk::EditorBase
{
public:
    static constexpr double kWidth = 760.0;
    static constexpr double kHeight = 526.0 + 170.0; // the tail panel's displays (smacheratr::TailDisplays::kHeight)

    explicit Editor (Controller* c);
    void buildUI (VSTGUI::CFrame* f) override;
    void idle () override;
    void paramChanged (uint32_t id) override;

private:
    std::unique_ptr<smacheratr::TailDisplays> tailDisplays;
    void onClose () override;
    void showMenu (VSTGUI::CPoint where);

    Controller* ctl;
    SpectrumView* spectrum = nullptr;
    pk::Label* latencyLabel = nullptr;
};

} // namespace locus
