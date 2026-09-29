#pragma once

#include "smacheratr/src/ui/TailDisplays.h"

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
    static constexpr double kHeight = 686.0 + 170.0; // the tail panel's displays (smacheratr::TailDisplays::kHeight)

    explicit Editor (Controller* c);
    void buildUI (VSTGUI::CFrame* f) override;
    void idle () override;
    void paramChanged (uint32_t id) override;

    // Writes the current render to Music/Stretchr Renders (reusing the file while the render is
    // unchanged) and returns its path, or "" on failure.
    std::string renderToFile (std::string& error);
    void loadFile (const std::string& path);

private:
    std::unique_ptr<smacheratr::TailDisplays> tailDisplays;
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
