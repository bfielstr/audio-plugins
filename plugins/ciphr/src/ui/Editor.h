#pragma once

#include "smacheratr/src/ui/TailPanel.h"

#include <memory>

#include "pluginkit/vst/EditorBase.h"

namespace ciphr {

class Controller;
class CipherView;

class Editor : public pk::EditorBase
{
public:
    // Classic: the display across the top, two rows of panels (GENERATOR, INPUT, FILTER over AMP ENVELOPE,
    // FILTER ENVELOPE, PROCESSOR), OUTPUT beside both, the end saturator's section at the bottom
    static constexpr double kWidth = 1070.0;
    static constexpr double kRow1 = 228.0, kRow2 = 352.0, kRowH = 116.0;
    static constexpr double kTailTop = kRow2 + kRowH + 8.0;
    static constexpr double kHeight = kTailTop + smacheratr::TailPanel::kOpenHeight + 8.0;
    // the panels' places (left, right) in the Classic layout, for the host test
    static constexpr double kGenLeft = 8.0, kGenRight = 406.0;
    static constexpr double kInputLeft = 414.0, kInputRight = 620.0;
    static constexpr double kFilterLeft = 628.0, kFilterRight = 962.0;
    static constexpr double kAmpLeft = 8.0, kAmpRight = 342.0;
    static constexpr double kFenvLeft = 350.0, kFenvRight = 620.0;
    static constexpr double kProcLeft = 628.0, kProcRight = 962.0;
    static constexpr double kOutLeft = 970.0, kOutRight = 1062.0;
    // the Input Path switch in the INPUT panel (its coordinates in the panel)
    static constexpr double kPathLeft = 14.0, kPathTop = 30.0, kPathW = 110.0, kPathH = 20.0;
    // the knobs: the first at x 14 in its panel, one every 64 px, 56 x 64 from y 28
    static constexpr double kKnobLeft = 14.0, kKnobStep = 64.0, kKnobTop = 28.0, kKnobW = 56.0, kKnobH = 64.0;
    // the display (Classic; an arranged layout builds it narrower: kArrangedDisplayW)
    static constexpr double kDisplayTop = 40.0, kDisplayBottom = 220.0, kArrangedDisplayW = 440.0;

    explicit Editor (Controller* c);
    void buildUI (VSTGUI::CFrame* f) override;
    // the panels for Menu > Layout (pluginkit/Layout.h): the Wide template's rows by purpose
    pk::layout::Spec layoutSpec (bool arranged) const override;
    void idle () override;
    void paramChanged (uint32_t id) override;

private:
    std::unique_ptr<smacheratr::TailPanel> tail;
    void onClose () override;
    void showMenu (VSTGUI::CPoint where);
    VSTGUI::CRect displayRect (bool arranged) const;

    Controller* ctl;
    CipherView* display = nullptr;
    pk::Label* latencyLabel = nullptr;
};

} // namespace ciphr
