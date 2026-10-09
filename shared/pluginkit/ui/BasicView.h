// The Basic page (issue: "simplify the UI"): a plug-in's editor opens on a page of its main controls, few
// and large; everything else is in the Advanced view (the editor as it builds itself, in its layouts:
// pluginkit/Layout.h), one click away (the Advanced switch in the header of both). Which one an instance
// shows is editor state saved in the project (ControllerBase::uiAdvanced); a new instance opens on the Basic
// page unless Menu > Defaults > Advanced View by Default is on.
//
// An editor declares its Basic page as a Spec (EditorBase::basicSpec): its main controls row by row, its
// output controls, and optionally its most useful display, a few views of its own in the header (smemplr's
// sample loader), a level for the mini meter, the end saturator's On switch and what its extras show.
// EditorBase builds the page from it, the same way in every plug-in (place ()):
//
//   +-------------------------------------------------------------------------------------+
//   | title  [the plug-in's header views]  [preset name] < >        [Advanced] [?] [Menu] |  header
//   +-------------------------------------------------------------------------------------+
//   | the capture band (optional): the output's scope, Freeze, drag it out as audio or as  |
//   | a wavetable (pluginkit/ui/CaptureView.h)                                             |
//   +-------------------------------------------------------------------+-----------------+
//   | the display (optional)                                            | OUTPUT          |
//   |                                                                   |  the output     |
//   | the main controls, a row at a time (at most kPerRow a row),       |  controls       |
//   | spread evenly across the row                                      |                 |
//   +-------------------------------------------------------------------+-----------------+
//   | the extras, when the strip is expanded (the end saturator's section, the meters...)  |
//   +-------------------------------------------------------------------------------------+
//   | [Extras] [Tail]  a line of what is there                          | mini meter      |  extras strip
//   +-------------------------------------------------------------------------------------+
//   (the info box under it, as in every editor)
//
// An editor with no Basic page (an empty Spec, the default) has only its Advanced view, and no switch.
#pragma once

#include "vstgui/lib/crect.h"
#include "pluginkit/Capture.h"
#include "vstgui/lib/cview.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace pk::basic {

// ---- layout constants (the Basic page of every plug-in)
constexpr double kWidth = 880;      // the page's width (a Spec may ask for more: Spec::width)
constexpr double kHeader = 40;      // the header band
constexpr double kMargin = 12;      // at the window's sides
constexpr double kGap = 16;         // between sections, and at least between controls
constexpr double kKnobW = 96;       // a large knob (its label and value in kLabelSize, kValueSize)
constexpr double kKnobH = 116;
constexpr double kLabelSize = 13.0, kValueSize = 12.0;
constexpr double kCaption = 16;     // the caption above a switch or a menu
constexpr double kSwitchH = 30;     // a switch, a segmented switch or a menu
constexpr double kSideW = 148;      // the output column at the right
constexpr double kStripH = 36;      // the extras strip
constexpr int kPerRow = 4;          // controls in a row, at most
constexpr double kTitleSize = 16.0;
constexpr double kCaptureH = 104;   // the capture band (its row of controls, the scope under it)

// One of the page's controls, bound to a parameter. Its label is the parameter's short name unless given
// (keep it the label the Advanced view shows the control by, docs name controls by their labels).
struct Control
{
    enum class Kind
    {
        Knob,      // a large knob
        Toggle,    // an on / off switch
        Choice,    // a drop-down menu
        Segmented, // a segmented switch (segments: its labels)
    };
    uint32_t id = 0;
    Kind kind = Kind::Knob;
    std::string label;                 // ("": the parameter's short name; a Segmented's caption)
    bool bipolar = false;              // a knob's arc from its centre
    std::vector<std::string> segments; // a Segmented's labels
    double width = 0;                  // a switch's or a menu's width (0: as wide as a knob, a Segmented 84 a segment)
};
inline Control knob (uint32_t id, std::string label = {}, bool bipolar = false) { return {id, Control::Kind::Knob, std::move (label), bipolar, {}, 0}; }
inline Control toggle (uint32_t id, std::string label) { return {id, Control::Kind::Toggle, std::move (label), false, {}, 0}; }
inline Control choice (uint32_t id, std::string label = {}, double width = 0) { return {id, Control::Kind::Choice, std::move (label), false, {}, width}; }
inline Control segmented (uint32_t id, std::string caption, std::vector<std::string> segments, double width = 0)
{
    return {id, Control::Kind::Segmented, std::move (caption), false, std::move (segments), width};
}
// a control's size on the page
double controlWidth (const Control& c);
double controlHeight (const Control& c);

// Makes a view of the editor's own in `r` (the page's coordinates; the view is added where `r` is).
using Factory = std::function<VSTGUI::CView* (const VSTGUI::CRect& r)>;

struct Spec
{
    std::string title;                      // the plug-in's name, lowercase ("smemplr")
    std::vector<std::vector<Control>> rows; // the main controls, row by row (3 to 6 in all is the idea)
    std::vector<Control> output;            // the output column's controls, top to bottom (none: no column)
    Factory display;                        // the most useful display, above the controls (optional; several
                                            // views: a pk::Group of them, which the page unpacks)
    double displayHeight = 0;               // (0: no display)
    Factory header;                         // the plug-in's own views in the header, after the title (optional)
    double headerWidth = 0;                 // (as wide as this)
    std::function<float ()> level;          // the mini meter's level now (linear peak; none: the capture
                                            // buffer's, else no meter)
    // The capture band across the top (the plug-in's output, held and dragged out as audio or a
    // wavetable): the plug-in's capture buffer, which its processor pushes its output into (none yet: an
    // empty scope). One line in a basicSpec: s.capture = [this] { return &<the buffer>; };
    std::function<const CaptureBuffer* ()> capture;
    int64_t tailOn = -1;                    // the end saturator's On parameter: the strip's Tail switch (-1: none)
    Factory extras;                         // what the expanded strip shows (the end saturator's section, the meters)
    double extrasHeight = 0;                // (0: nothing to expand)
    std::string extrasTitle;                // its panel's title
    std::function<std::string ()> summary;  // the strip's line of text (what the extras hold now)
    const char* summaryHelp = nullptr;      // (its hover help)
    std::function<void (VSTGUI::CPoint where)> menu; // the header's Menu button: the editor's menu at `where`
    const char* (*help) (uint32_t id) = nullptr; // the controls' hover help
    double width = kWidth;                  // the page's width
    VSTGUI::CRect advancedSwitch;           // where the Advanced view's header shows the Advanced switch (the
                                            // Advanced build's coordinates, in its header band)
    bool empty () const { return rows.empty () && output.empty (); }
    // every parameter with a control on the page
    std::vector<uint32_t> params () const;
};

// ---- where everything goes (the page's coordinates)
struct Geometry
{
    double width = 0, height = 0;                    // the content (the info strip goes under it)
    VSTGUI::CRect title, headerViews, presets, presetPrev, presetNext;
    VSTGUI::CRect advanced, help, menu;              // the header's right end
    VSTGUI::CRect capture;                           // the capture band (empty: none)
    VSTGUI::CRect display;                           // (empty: none)
    VSTGUI::CRect main;                              // the main controls' panel
    std::vector<std::vector<VSTGUI::CRect>> rows;    // each control's rectangle, inside `main` (the panel's coordinates)
    VSTGUI::CRect side;                              // the output column's panel
    std::vector<VSTGUI::CRect> output;               // inside `side`
    VSTGUI::CRect extras;                            // (empty: closed)
    VSTGUI::CRect strip;                             // the extras strip's panel
    VSTGUI::CRect expand, tail, summary, meter;      // inside `strip` (tail empty: none; meter empty: none)
    std::vector<std::string> problems;               // what does not fit (rows too wide, the header too full)
};
Geometry place (const Spec& spec, bool extrasOpen);
// The header's right end on a page `width` wide: the Advanced switch, ? and Menu (host tests click them).
struct HeaderRight
{
    VSTGUI::CRect advanced, help, menu;
};
// (inline: the host tests, which load a plug-in as a bundle, find the switch with it)
inline HeaderRight headerRight (double width)
{
    // the same places in every plug-in: Menu at the right edge, ? before it, Advanced before that
    HeaderRight h;
    const double top = 7, bottom = kHeader - 9;
    h.menu = VSTGUI::CRect (width - kMargin - 56, top, width - kMargin, bottom);
    h.help = VSTGUI::CRect (h.menu.left - 8 - 24, top, h.menu.left - 8, bottom);
    h.advanced = VSTGUI::CRect (h.help.left - 8 - 92, top, h.help.left - 8, bottom);
    return h;
}

// The Advanced switch's help (the info box), and the strip's.
extern const char* const kAdvancedHelp;
extern const char* const kExtrasHelp;
extern const char* const kTailHelp;
extern const char* const kMeterHelp;
extern const char* const kPresetStepHelp;

// The strip's mini meter: the level (Spec::level) as a bar on a dB scale (-60 .. +6 dB, 0 dB marked), lit in
// cinnabar, its peak in peak colour above 0 dB; it falls back at about 24 dB a second. idle () reads the
// level and repaints only when the bar moves.
class MiniMeter : public VSTGUI::CView
{
public:
    MiniMeter (const VSTGUI::CRect& r, std::function<float ()> level);
    void draw (VSTGUI::CDrawContext* ctx) override;
    void idle ();
    static double position (double db); // 0 .. 1 along the bar

private:
    std::function<float ()> level;
    double shownDb = -120.0;
    int shownPx = -1;
};

} // namespace pk::basic
