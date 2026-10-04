// Custom-drawn controls bound directly to parameter IDs through a ParamHost.
#pragma once

#include "pluginkit/ParamTable.h"
#include "pluginkit/ui/Theme.h"

#include "vstgui/lib/ccolor.h"
#include "vstgui/lib/cview.h"
#include "vstgui/lib/cviewcontainer.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace VSTGUI {
class CDrawContext;
}

namespace pk {

// The theme's shared drawing (docs/THEME.md): thin 1 px copper linework, outlines instead of fills, one
// energy colour for what is lit. Plug-in views use these so their own buttons and frames match the kit's.
namespace draw {
// A 1 px outline of `r` on the pixel grid, with a small corner radius (0: square).
void outline (VSTGUI::CDrawContext* ctx, const VSTGUI::CRect& r, const VSTGUI::CColor& c, double radius = 2.0);
// Corner brackets: an L of `len` px at each corner of `r`.
void brackets (VSTGUI::CDrawContext* ctx, const VSTGUI::CRect& r, double len, const VSTGUI::CColor& c);
// A plain horizontal hairline from x0 to x1 on the pixel row `y` (a section divider, a header's edge).
void rule (VSTGUI::CDrawContext* ctx, double x0, double x1, double y, const VSTGUI::CColor& c);
// The old "ticked" rule. The ticks were decoration and are gone (they made the panels look busy): it
// draws the plain rule now and only stays so callers not yet moved over keep building. Use rule().
inline void tickRule (VSTGUI::CDrawContext* ctx, double x0, double x1, double y, const VSTGUI::CColor& c, double /*step*/ = 8.0)
{
    rule (ctx, x0, x1, y, c);
}
// An arc of the ellipse in `r` from startDeg to endDeg (degrees, clockwise from 3 o'clock, as
// CDrawContext::drawArc takes them), stroked in the context's frame colour, line width and style. Drawn
// as a graphics path: VSTGUI's cairo drawArc passes the degrees on as radians, so on Linux it drew
// nearly whole rings (every knob's value arc), and slowly; the path takes degrees on every platform.
void arc (VSTGUI::CDrawContext* ctx, const VSTGUI::CRect& r, double startDeg, double endDeg);
// A button's lamp: a short 2 px bar above the bottom edge of `r`.
void marker (VSTGUI::CDrawContext* ctx, const VSTGUI::CRect& r, const VSTGUI::CColor& c);
struct ButtonState
{
    bool lit = false;     // on / selected: the lamp lights, the label turns text colour and bold
    bool lamp = true;     // draw the lamp (off: a plain push button)
    bool pressed = false; // held or dragged: a cinnabar outline
    bool enabled = true;
};
// An outlined button with a centred label (Toggle, ActionButton and the plug-ins' own tabs).
void button (VSTGUI::CDrawContext* ctx, const VSTGUI::CRect& r, const std::string& label, ButtonState s);
// A drag handle in a display: a well-filled ring of radius `r`, pale copper; held or selected
// (`active`) it lights cinnabar with a centre dot. Its element off (`enabled` false): plain copper.
void handle (VSTGUI::CDrawContext* ctx, const VSTGUI::CPoint& c, double r, bool active, bool enabled = true);
// The editor window's background: ground, a well header band of `headerHeight` over a plain dim
// hairline, and a thin copper frame with corner brackets.
void window (VSTGUI::CDrawContext* ctx, const VSTGUI::CRect& r, double headerHeight = 34.0);
} // namespace draw

struct ParamHost
{
    virtual ~ParamHost () = default;
    virtual const ParamTable& table () = 0;
    virtual double norm (uint32_t id) = 0;
    virtual double plainValue (uint32_t id) = 0;
    virtual void beginEdit (uint32_t id) = 0;
    virtual void setNorm (uint32_t id, double v) = 0; // inside a gesture
    virtual void endEdit (uint32_t id) = 0;
    virtual std::string valueText (uint32_t id) = 0;
    // The plug-in's own parameter behind `id` (a host in front of another one maps it; -1: none). Smemplr's
    // editor finds what a control is bound to this way (its modulation's drag and drop).
    virtual int64_t sourceParam (uint32_t id) { return id; }
    void setOnce (uint32_t id, double v)
    {
        beginEdit (id);
        setNorm (id, v);
        endEdit (id);
    }
};

// A ParamHost that shows another plug-in's parameter table on this plug-in's parameters (the
// built-in effects in Smemplr): ids go through `map`; ids it does not map (< 0) read as their
// defaults and ignore edits. Normalized values pass through unchanged, so both tables must give
// the parameters the same ranges.
class MappedParamHost : public ParamHost
{
public:
    using Map = std::function<int64_t (uint32_t)>;
    MappedParamHost (ParamHost* host, const ParamTable& table, Map map) : in (host), tbl (table), idOf (std::move (map)) {}
    const ParamTable& table () override { return tbl; }
    double norm (uint32_t id) override
    {
        const int64_t m = idOf (id);
        return m < 0 ? tbl.defaultNormalized (id) : in->norm ((uint32_t)m);
    }
    double plainValue (uint32_t id) override { return tbl.toPlain (id, norm (id)); }
    void beginEdit (uint32_t id) override
    {
        if (const int64_t m = idOf (id); m >= 0)
            in->beginEdit ((uint32_t)m);
    }
    void setNorm (uint32_t id, double v) override
    {
        if (const int64_t m = idOf (id); m >= 0)
            in->setNorm ((uint32_t)m, v);
    }
    void endEdit (uint32_t id) override
    {
        if (const int64_t m = idOf (id); m >= 0)
            in->endEdit ((uint32_t)m);
    }
    std::string valueText (uint32_t id) override { return tbl.toText (id, plainValue (id)); }
    int64_t sourceParam (uint32_t id) override
    {
        const int64_t m = idOf (id);
        return m < 0 ? -1 : in->sourceParam ((uint32_t)m);
    }

private:
    ParamHost* in;
    const ParamTable& tbl;
    Map idOf;
};

// The mouse wheel on a filter handle changes the filter's intensity (resonance, Q or slope) in every
// display of the suite: while the handle is held, or with Shift over it. The change of the
// parameter's normalized value for one wheel event: one step for a stepped parameter, 2 % per
// notch otherwise; 0 when the event has no vertical movement.
double wheelStep (const VSTGUI::MouseWheelEvent& e, const ParamTable& table, uint32_t id);

// A piece of text a widget draws, for the layout check (pk::layoutReport): the area it is drawn in,
// the text, its font, how it is aligned (0 left, 1 centre, 2 right) and the room the widget keeps
// clear inside the area (its outline and padding, both sides together). A widget whose text changes
// gives one spot per sample text (a value at the ends of its range, its default, now).
struct TextSpot
{
    VSTGUI::CRect area;
    std::string text;
    double size = 10.0;
    bool bold = false;
    int align = 1;
    double pad = 0.0;
};

// A view bound to one parameter.
class ParamView : public VSTGUI::CView
{
public:
    ParamView (const VSTGUI::CRect& r, ParamHost* h, uint32_t id) : CView (r), host (h), param (id) {}
    uint32_t paramId () const { return param; }
    // The parameter's full name (the info box's title for this control).
    std::string paramName () const { return host->table ().info (param).name; }
    int64_t sourceParamId () const { return host->sourceParam (param); } // (ParamHost::sourceParam)
    void setEnabledLook (bool e)
    {
        if (e != enabledLook)
        {
            enabledLook = e;
            invalid ();
        }
    }

protected:
    // A right click resets the parameter to its default (its neutral value) in every control of the
    // suite. Call first in onMouseDownEvent; true when it handled the event.
    bool resetOnRightClick (VSTGUI::MouseDownEvent& e);
    // The value's text at the ends of the range, at the default and now (the layout check's samples).
    std::vector<std::string> sampleTexts () const;

    ParamHost* host;
    uint32_t param;
    bool enabledLook = true;
};

class Knob : public ParamView
{
public:
    Knob (const VSTGUI::CRect& r, ParamHost* h, uint32_t id, const char* label = nullptr, bool bipolar = false);
    void draw (VSTGUI::CDrawContext* ctx) override;
    std::vector<TextSpot> textSpots () const; // (the layout check)
    // The square the dial (track, arc and body) is drawn in: under the label, above the value.
    VSTGUI::CRect dialRect () const;
    void onMouseDownEvent (VSTGUI::MouseDownEvent& e) override;
    void onMouseMoveEvent (VSTGUI::MouseMoveEvent& e) override;
    void onMouseUpEvent (VSTGUI::MouseUpEvent& e) override;
    void onMouseCancelEvent (VSTGUI::MouseCancelEvent& e) override;
    void onMouseWheelEvent (VSTGUI::MouseWheelEvent& e) override;

private:
    std::string label;
    bool bipolar;
    bool dragging = false;
    double startY = 0, startValue = 0, dragValue = 0;
};

// Horizontal bar slider (LFO amounts), label on the left, value on the right.
class HSlider : public ParamView
{
public:
    HSlider (const VSTGUI::CRect& r, ParamHost* h, uint32_t id, const char* label);
    void draw (VSTGUI::CDrawContext* ctx) override;
    std::vector<TextSpot> textSpots () const; // (the layout check)
    void onMouseDownEvent (VSTGUI::MouseDownEvent& e) override;
    void onMouseMoveEvent (VSTGUI::MouseMoveEvent& e) override;
    void onMouseUpEvent (VSTGUI::MouseUpEvent& e) override;

private:
    std::string label;
    bool dragging = false;
    double startX = 0, startValue = 0;
};

// A value field in the style of Live's number boxes: the value text in a box, drag up/down to
// change it (Shift: fine), double-click to reset, mouse wheel to step.
class NumberBox : public ParamView
{
public:
    NumberBox (const VSTGUI::CRect& r, ParamHost* h, uint32_t id, VSTGUI::CColor color = theme::kText);
    void draw (VSTGUI::CDrawContext* ctx) override;
    std::vector<TextSpot> textSpots () const; // (the layout check)
    void onMouseDownEvent (VSTGUI::MouseDownEvent& e) override;
    void onMouseMoveEvent (VSTGUI::MouseMoveEvent& e) override;
    void onMouseUpEvent (VSTGUI::MouseUpEvent& e) override;
    void onMouseCancelEvent (VSTGUI::MouseCancelEvent& e) override;
    void onMouseWheelEvent (VSTGUI::MouseWheelEvent& e) override;

private:
    VSTGUI::CColor color;
    bool dragging = false;
    double startY = 0, dragValue = 0;
};

// On/off button for a bool parameter.
class Toggle : public ParamView
{
public:
    Toggle (const VSTGUI::CRect& r, ParamHost* h, uint32_t id, const char* label);
    void draw (VSTGUI::CDrawContext* ctx) override;
    std::vector<TextSpot> textSpots () const; // (the layout check)
    void onMouseDownEvent (VSTGUI::MouseDownEvent& e) override;

private:
    std::string label;
};

// Segmented selector for a choice parameter; `labels` may shorten the choice names.
class Segmented : public ParamView
{
public:
    Segmented (const VSTGUI::CRect& r, ParamHost* h, uint32_t id, std::vector<std::string> labels);
    void draw (VSTGUI::CDrawContext* ctx) override;
    std::vector<TextSpot> textSpots () const; // (the layout check)
    void onMouseDownEvent (VSTGUI::MouseDownEvent& e) override;

private:
    std::vector<std::string> labels;
};

// Drop-down for a choice parameter.
class Choice : public ParamView
{
public:
    Choice (const VSTGUI::CRect& r, ParamHost* h, uint32_t id, const char* label = nullptr);
    void draw (VSTGUI::CDrawContext* ctx) override;
    std::vector<TextSpot> textSpots () const; // (the layout check)
    void onMouseDownEvent (VSTGUI::MouseDownEvent& e) override;

private:
    std::string label;
};

// A plain push button that runs a callback. `active` returns the lit state (optional).
class ActionButton : public VSTGUI::CView
{
public:
    ActionButton (const VSTGUI::CRect& r, std::string text, std::function<void ()> onClick,
                  std::function<bool ()> active = {});
    void draw (VSTGUI::CDrawContext* ctx) override;
    std::vector<TextSpot> textSpots () const; // (the layout check)
    void onMouseDownEvent (VSTGUI::MouseDownEvent& e) override;
    void onMouseUpEvent (VSTGUI::MouseUpEvent& e) override;
    void setText (const std::string& t)
    {
        text = t;
        invalid ();
    }
    const std::string& label () const { return text; }

private:
    std::string text;
    std::function<void ()> onClick;
    std::function<bool ()> active;
    bool pressed = false;
};

// Static or dynamic text.
class Label : public VSTGUI::CView
{
public:
    Label (const VSTGUI::CRect& r, std::string text, double size = 11.0, bool bold = false, int align = 0);
    void setText (const std::string& t)
    {
        if (t != text)
        {
            text = t;
            invalid ();
        }
    }
    const std::string& getText () const { return text; }
    void setDim (bool d)
    {
        dim = d;
        invalid ();
    }
    void draw (VSTGUI::CDrawContext* ctx) override;
    std::vector<TextSpot> textSpots () const; // (the layout check)

private:
    std::string text;
    double size;
    bool bold, dim = false;
    int align; // 0 left, 1 centre, 2 right
};

// Container with a titled, rounded panel background.
class Panel : public VSTGUI::CViewContainer
{
public:
    Panel (const VSTGUI::CRect& r, std::string title = {});
    void drawBackgroundRect (VSTGUI::CDrawContext* ctx, const VSTGUI::CRect& update) override;
    const std::string& titleText () const { return title; }

private:
    std::string title;
};

// Invisible grouping container (used to switch sets of controls).
class Group : public VSTGUI::CViewContainer
{
public:
    explicit Group (const VSTGUI::CRect& r);
};

} // namespace pk
