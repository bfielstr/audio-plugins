#pragma once

#include "smacheratr/src/ui/TailPanel.h"

#include <memory>

#include "pluginkit/vst/EditorBase.h"

namespace deepr {

class Controller;
class DeeprView;

class Editor : public pk::EditorBase
{
public:
    static constexpr double kWidth = 760.0;
    // the end saturator's section (smacheratr::TailPanel, both parts open) at the bottom, from y 530, 8 px clear under it
    static constexpr double kHeight = 530.0 + smacheratr::TailPanel::kOpenHeight + 8.0;

    explicit Editor (Controller* c);
    void buildUI (VSTGUI::CFrame* f) override;
    // the panels for Menu > Layout (pluginkit/Layout.h): the Wide template's rows by purpose
    pk::layout::Spec layoutSpec (bool arranged) const override;
    // The Basic page (pluginkit/ui/BasicView.h): the display, Depth, Dip Freq, Threshold and Sub Gain; Mix and Output; the end saturator in the extras
    pk::basic::Spec basicSpec () override;
    void idle () override;
    void paramChanged (uint32_t id) override;

private:
    std::unique_ptr<smacheratr::TailPanel> tail;
    std::unique_ptr<smacheratr::TailPanel> makeTail ();
    static smacheratr::TailBases tailBases ();
    void onClose () override;
    void showMenu (VSTGUI::CPoint where);

    Controller* ctl;
    DeeprView* display = nullptr;
    pk::Label* latencyLabel = nullptr;
};

} // namespace deepr
