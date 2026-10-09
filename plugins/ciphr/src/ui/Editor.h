#pragma once

#include "smacheratr/src/ui/TailPanel.h"

#include <memory>

#include "pluginkit/vst/EditorBase.h"

namespace ciphr {

class Controller;
class CipherView;
class DisperseView;

class Editor : public pk::EditorBase
{
public:
    // Classic: the display across the top, three rows of panels (GENERATOR, INPUT, FILTER over AMP ENVELOPE,
    // FILTER ENVELOPE, PROCESSOR over TEXTURE, DISPERSE), OUTPUT beside the first two, the end saturator's
    // section at the bottom
    static constexpr double kWidth = 1070.0;
    static constexpr double kRow1 = 228.0, kRow2 = 352.0, kRowH = 116.0;
    static constexpr double kRow3 = kRow2 + kRowH + 8.0;
    static constexpr double kTailTop = kRow3 + kRowH + 8.0;
    static constexpr double kHeight = kTailTop + smacheratr::TailPanel::kOpenHeight + 8.0;
    // the panels' places (left, right) in the Classic layout, for the host test
    static constexpr double kGenLeft = 8.0, kGenRight = 406.0;
    static constexpr double kInputLeft = 414.0, kInputRight = 620.0;
    static constexpr double kFilterLeft = 628.0, kFilterRight = 962.0;
    static constexpr double kAmpLeft = 8.0, kAmpRight = 342.0;
    static constexpr double kFenvLeft = 350.0, kFenvRight = 620.0;
    static constexpr double kProcLeft = 628.0, kProcRight = 962.0;
    static constexpr double kOutLeft = 970.0, kOutRight = 1062.0;
    static constexpr double kTexLeft = 8.0, kTexRight = 192.0;
    static constexpr double kDispLeft = 200.0, kDispRight = 1062.0;
    // in the TEXTURE panel: the Wave Set menu (its label over its field), then Stretch's knob
    static constexpr double kWavesLeft = 14.0, kWavesTop = 28.0, kWavesW = 90.0, kWavesH = 37.0, kStretchLeft = 114.0;
    // in the DISPERSE panel: the On switch, the knobs from kDispKnobLeft, the bars from kBarsLeft
    static constexpr double kDispOnLeft = 14.0, kDispOnTop = 30.0, kDispOnW = 52.0, kDispOnH = 20.0;
    static constexpr double kDispKnobLeft = 76.0, kBarsLeft = 400.0, kBarsTop = 26.0, kBarsBottom = 106.0;
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
    // The Basic page (pluginkit/ui/BasicView.h): the cluster display, Timbre, Character, Cutoff and Space;
    // Blend and Output; the end saturator in the extras
    pk::basic::Spec basicSpec () override;
    void idle () override;
    void paramChanged (uint32_t id) override;

private:
    std::unique_ptr<smacheratr::TailPanel> tail;
    std::unique_ptr<smacheratr::TailPanel> makeTail ();
    static smacheratr::TailBases tailBases ();
    void onClose () override;
    void showMenu (VSTGUI::CPoint where);
    VSTGUI::CRect displayRect (bool arranged) const;

    Controller* ctl;
    CipherView* display = nullptr;
    DisperseView* bars = nullptr;
    pk::Label* latencyLabel = nullptr;
};

} // namespace ciphr
