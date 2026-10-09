#pragma once

#include "pluginkit/vst/EditorBase.h"

#include <string>

namespace probr {

class Controller;
class RecordButton;
class LevelMeter;
class MessageText;
class TextField;

class Editor : public pk::EditorBase
{
public:
    // Classic: PROBE (the Label and the Folder) and RECORD (the button, Mode, the take) side by side, MONITOR
    // (the level, what the probe is doing, the free space, the session) under them.
    static constexpr double kWidth = 640.0;
    static constexpr double kTop = 40.0, kRowBottom = 180.0;
    static constexpr double kMonTop = 188.0, kMonBottom = 268.0;
    static constexpr double kHeight = kMonBottom + 8.0;
    static constexpr double kProbeLeft = 8.0, kProbeRight = 400.0;
    static constexpr double kRecLeft = 408.0, kRecRight = 632.0;
    // inside PROBE (panel coordinates)
    static constexpr double kFieldLeft = 12.0, kFieldTop = 38.0, kFieldRight = 380.0, kFieldBottom = 62.0;
    static constexpr double kChooseLeft = 12.0, kChooseTop = 110.0, kChooseW = 84.0, kButtonH = 22.0;
    static constexpr double kDefaultLeft = 104.0;
    // inside RECORD
    static constexpr double kRecordLeft = 12.0, kRecordTop = 22.0, kRecordW = 200.0, kRecordH = 44.0;
    static constexpr double kModeLeft = 12.0, kModeTop = 90.0, kModeW = 200.0, kModeH = 22.0;

    explicit Editor (Controller* c);
    void buildUI (VSTGUI::CFrame* f) override;
    pk::layout::Spec layoutSpec (bool arranged) const override;
    // The Basic page (pluginkit/ui/BasicView.h): the record button, the level and the probe's status, the take, and Mode (no capture band: probr records the track itself)
    pk::basic::Spec basicSpec () override;
    void idle () override;
    void paramChanged (uint32_t id) override;

private:
    void onClose () override;
    void showMenu (VSTGUI::CPoint where);
    void chooseFolder ();
    void updateTexts ();

    Controller* ctl;
    RecordButton* record = nullptr;
    LevelMeter* meter = nullptr;
    MessageText* message = nullptr;
    TextField* labelField = nullptr;
    pk::Label* folderLabel = nullptr;
    pk::Label* takeLabel = nullptr;
    pk::Label* freeLabel = nullptr;
    pk::Label* sessionLabel = nullptr;
    std::string labelShown;
};

} // namespace probr
