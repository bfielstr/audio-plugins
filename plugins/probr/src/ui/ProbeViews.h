// Probr's own views: the Record button (red while a take is written), the level meter and the Label
// field.
#pragma once

#include "pluginkit/ui/Widgets.h"

#include "vstgui/lib/controls/ctextedit.h"
#include "vstgui/lib/controls/icontrollistener.h"

#include <functional>
#include <string>

namespace probr {

// Record: a click arms the probe or switches it off (the parameter); it shows the probe's state (Status's
// State): Off, Armed (waiting for the song to play), Recording (filled red) or Stopped (a problem).
class RecordButton : public pk::ParamView
{
public:
    RecordButton (const VSTGUI::CRect& r, pk::ParamHost* h, uint32_t id, std::function<int ()> state);
    void draw (VSTGUI::CDrawContext* ctx) override;
    void onMouseDownEvent (VSTGUI::MouseDownEvent& e) override;
    // repaints when the state changed (the editor's idle)
    void idle ();

private:
    std::function<int ()> state;
    int shown = -1;
};

// Two horizontal peak bars, left over right, -60 .. 0 dBFS, falling back slowly.
class LevelMeter : public VSTGUI::CView
{
public:
    // peaks: the largest sample of each channel since the last call (and resets them)
    LevelMeter (const VSTGUI::CRect& r, std::function<void (float&, float&)> peaks);
    void draw (VSTGUI::CDrawContext* ctx) override;
    void idle ();

private:
    std::function<void (float&, float&)> peaks;
    double db[2] = {-100.0, -100.0};
    double drawn[2] = {-100.0, -100.0}; // (the levels last asked to be drawn)
};

// A line of status text: plain, or a warning (in the suite's peak colour).
class MessageText : public VSTGUI::CView
{
public:
    explicit MessageText (const VSTGUI::CRect& r) : CView (r) {}
    void set (const std::string& t, bool warning);
    const std::string& text () const { return msg; }
    bool warning () const { return warn; }
    void draw (VSTGUI::CDrawContext* ctx) override;

private:
    std::string msg;
    bool warn = false;
};

// A one-line text field in the suite's look; `commit` gets the text when typing ends (Return, or the
// field losing the focus).
class TextField : public VSTGUI::CTextEdit
{
public:
    TextField (const VSTGUI::CRect& r, std::function<void (const std::string&)> commit);

private:
    struct Listener : VSTGUI::IControlListener
    {
        std::function<void (const std::string&)> commit;
        void valueChanged (VSTGUI::CControl* c) override;
    };
    Listener listener;
};

// `text` cut in the middle with "..." to about `maxChars` characters (whole UTF-8 characters).
std::string fitText (const std::string& text, size_t maxChars);

} // namespace probr
