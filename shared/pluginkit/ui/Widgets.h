// Custom-drawn controls bound directly to parameter IDs through a ParamHost.
#pragma once

#include "pluginkit/ParamTable.h"

#include "vstgui/lib/ccolor.h"
#include "vstgui/lib/cview.h"
#include "vstgui/lib/cviewcontainer.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace pk {

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

// A view bound to one parameter.
class ParamView : public VSTGUI::CView
{
public:
    ParamView (const VSTGUI::CRect& r, ParamHost* h, uint32_t id) : CView (r), host (h), param (id) {}
    uint32_t paramId () const { return param; }
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

    ParamHost* host;
    uint32_t param;
    bool enabledLook = true;
};

class Knob : public ParamView
{
public:
    Knob (const VSTGUI::CRect& r, ParamHost* h, uint32_t id, const char* label = nullptr, bool bipolar = false);
    void draw (VSTGUI::CDrawContext* ctx) override;
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
    NumberBox (const VSTGUI::CRect& r, ParamHost* h, uint32_t id, VSTGUI::CColor color = VSTGUI::CColor (240, 240, 240));
    void draw (VSTGUI::CDrawContext* ctx) override;
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
    void onMouseDownEvent (VSTGUI::MouseDownEvent& e) override;
    void onMouseUpEvent (VSTGUI::MouseUpEvent& e) override;
    void setText (const std::string& t)
    {
        text = t;
        invalid ();
    }

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
    void setDim (bool d)
    {
        dim = d;
        invalid ();
    }
    void draw (VSTGUI::CDrawContext* ctx) override;

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
