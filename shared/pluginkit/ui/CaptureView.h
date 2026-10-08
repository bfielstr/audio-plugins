// The capture band of a Basic page (pluginkit/ui/BasicView.h, Spec::capture): a scope as wide as the page
// showing the plug-in's output over the capture's length (1, 2 or 4 bars synced to the host's tempo while
// it plays, else 1, 2 or 4 seconds: pluginkit/Capture.h), Freeze to hold what it shows, and the audio
// handed out:
//   - drag the scope (or Drag WAV): a stereo 32-bit float WAV of it at the host's rate, dropped on a DAW
//     track (written to the temporary folder first: <temp>/bfielstr captures);
//   - Drag Wavetable: the same as a wavetable (pluginkit/Wavetable.h: single cycles of 2048, up to 256, in
//     a mono WAV with Serum's `clm ` chunk, which Serum, Vital and Ableton's Wavetable read);
//   - right click: Save Audio..., Save Wavetable... (to a file of your choice), Freeze.
// What is held survives the page being built again (the extras opened, the view switched): the editor
// keeps it (CaptureHold).
#pragma once

#include "pluginkit/Capture.h"

#include "vstgui/lib/cviewcontainer.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace pk {

class Label;

// What Freeze holds (the editor keeps it across builds).
struct CaptureHold
{
    bool frozen = false;
    std::vector<float> l, r;
    double sampleRate = 48000;
    bool bars = false;
    double length = 1;
};

class CaptureBand : public VSTGUI::CViewContainer
{
public:
    using Source = std::function<const CaptureBuffer* ()>;
    // source: the plug-in's capture buffer (none: nothing to show yet); name: the plug-in's (file names);
    // choice: the length (0, 1, 2), kept by the editor
    CaptureBand (const VSTGUI::CRect& r, Source source, std::shared_ptr<CaptureHold> hold, std::string name, std::function<int ()> getChoice,
                 std::function<void (int)> setChoice);
    void idle ();
    void setFrozen (bool on);
    bool frozen () const { return hold->frozen; }

    // The audio shown now (held, or the buffer's window): stereo, and its rate. False when there is none.
    bool audioNow (std::vector<float>& l, std::vector<float>& r, double& sampleRate) const;
    // A WAV of it, or a wavetable of it, in the temporary folder: its path ("" and the reason when none).
    std::string writeAudio (std::string& error) const;
    std::string writeWavetable (std::string& error) const;
    // The same to a path of your choice.
    bool saveAudio (const std::string& path, std::string& error) const;
    bool saveWavetable (const std::string& path, std::string& error) const;
    void showMenu (VSTGUI::CPoint where);
    void say (const std::string& text); // the status line

    static constexpr double kControlsW = 436; // the controls at the right of the band's top row
    static constexpr double kRowH = 22;       // that row (the scope under it, the band's whole width)

private:
    friend class CaptureScope;
    Source source;
    std::shared_ptr<CaptureHold> hold;
    std::string name;
    std::function<int ()> getChoice;
    std::function<void (int)> setChoice;
    VSTGUI::CView* scope = nullptr;
    Label* unit = nullptr;
    Label* status = nullptr;
    void chooseFile (bool wavetable);
};

} // namespace pk
