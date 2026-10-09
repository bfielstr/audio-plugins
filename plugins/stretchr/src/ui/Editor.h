#pragma once

#include "smacheratr/src/ui/TailPanel.h"

#include <memory>

#include "pluginkit/vst/EditorBase.h"

#include <string>

namespace stretchr {

class Controller;
class ClipView;

class Editor : public pk::EditorBase
{
public:
    static constexpr double kWidth = 980.0;
    // the end saturator's section (smacheratr::TailPanel, both parts open) at the bottom, from y 600, 8 px clear under it
    static constexpr double kHeight = 600.0 + smacheratr::TailPanel::kOpenHeight + 8.0;

    explicit Editor (Controller* c);
    void buildUI (VSTGUI::CFrame* f) override;
    // the panels for Menu > Layout (pluginkit/Layout.h): the Wide template's rows by purpose
    pk::layout::Spec layoutSpec (bool arranged) const override;
    // The Basic page (pluginkit/ui/BasicView.h): the clip, Capture, Load and Drag Out in the header, Algorithm, Pitch, Speed and Follow Tempo; Gain; the end saturator in the extras
    pk::basic::Spec basicSpec () override;
    void idle () override;
    void paramChanged (uint32_t id) override;

    // Writes the current render to Music/Stretchr Renders (reusing the file while the render is
    // unchanged) and returns its path, or "" on failure.
    std::string renderToFile (std::string& error);
    void loadFile (const std::string& path);

private:
    std::unique_ptr<smacheratr::TailPanel> tail;
    std::unique_ptr<smacheratr::TailPanel> makeTail ();
    static smacheratr::TailBases tailBases ();
    void onClose () override;
    void showMenu (VSTGUI::CPoint where);
    void showClipMenu (VSTGUI::CPoint where);
    void browseForFile ();
    void exportWav ();
    void updateAlgorithmControls ();
    void setEditMode (int mode);

    Controller* ctl;
    ClipView* clipView = nullptr;
    pk::Label* status = nullptr;
    pk::Label* summary[2] = {nullptr, nullptr};
    pk::ActionButton* modeButtons[2] = {nullptr, nullptr};
    pk::ActionButton* captureBtn = nullptr;
    VSTGUI::CView* alienGrain = nullptr; // Alien's grain size (the Window parameter)
    VSTGUI::CViewContainer* windowGroup = nullptr;
    VSTGUI::CViewContainer* transientsGroup = nullptr;
    VSTGUI::CViewContainer* smearGroup = nullptr;
    VSTGUI::CViewContainer* noneGroup = nullptr;
    uint64_t lastChanges = 0;
    uint64_t exportedKey = 0;
    std::string exportedPath;
    std::string lastError;
    int errorTicks = 0;
};

} // namespace stretchr
