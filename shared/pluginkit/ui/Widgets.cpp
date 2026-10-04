#include "pluginkit/ui/Widgets.h"
#include "pluginkit/ui/Theme.h"

#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cframe.h"
#include "vstgui/lib/cgraphicspath.h"
#include "vstgui/lib/controls/coptionmenu.h"
#include "vstgui/lib/events.h"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace pk {

using namespace VSTGUI;

namespace {

void text (CDrawContext* ctx, const std::string& s, const CRect& r, const CColor& c, double size, bool bold = false,
           CHoriTxtAlign align = kCenterText)
{
    ctx->setFont (theme::font (size, bold));
    ctx->setFontColor (c);
    ctx->drawString (s.c_str (), r, align, true);
}

void fill (CDrawContext* ctx, const CRect& r, const CColor& c)
{
    ctx->setFillColor (c);
    ctx->drawRect (r, kDrawFilled);
}

// The value text of a readout (docs/THEME.md: the value in the text colour, its unit in text dim): the
// number in `num` and a trailing unit after a space ("-12.0 dB", "250 ms") in `unit`, centred in `r`.
// Only split when the text starts like a number and the part before the space ends in a digit, so
// choice names ("Soft Clip") stay in one colour.
void valueText (CDrawContext* ctx, const std::string& s, const CRect& r, const CColor& num, const CColor& unit, double size)
{
    const size_t sp = s.rfind (' ');
    const bool split = sp != std::string::npos && sp > 0 && sp + 1 < s.size () &&
                       (std::isdigit ((unsigned char)s[0]) || s[0] == '-' || s[0] == '+' || s[0] == '.') &&
                       std::isdigit ((unsigned char)s[sp - 1]);
    if (!split || num == unit)
    {
        text (ctx, s, r, num, size);
        return;
    }
    ctx->setFont (theme::font (size));
    const std::string a = s.substr (0, sp), b = s.substr (sp);
    const double wa = ctx->getStringWidth (a.c_str ()), wb = ctx->getStringWidth (b.c_str ());
    if (wa + wb > r.getWidth ())
    {
        text (ctx, s, r, num, size); // too tight to split cleanly: one colour, clipped as before
        return;
    }
    const double x = std::round (r.getCenter ().x - (wa + wb) / 2);
    text (ctx, a, CRect (x, r.top, x + wa + 1, r.bottom), num, size, false, kLeftText);
    text (ctx, b, CRect (x + wa, r.top, x + wa + wb + 1, r.bottom), unit, size, false, kLeftText);
}

bool isFine (const Modifiers& m) { return m.has (ModifierKey::Shift) || m.has (ModifierKey::Control); }

} // namespace

//==============================================================================
// Shared drawing of the theme (docs/THEME.md, "Line and form" and "Translating to plugin UIs").
namespace draw {

// Hairlines are stroked as graphics paths at half-pixel coordinates: VSTGUI leaves path coordinates
// alone in both its integral and non-integral draw modes (drawLine and drawRect snap and offset them
// in integral mode, which would move a half-pixel line off by one, or out of the view at its right and
// bottom edges), so a 1 px line at x + 0.5 lights exactly pixel column x either way.
static void hairlines (CDrawContext* ctx, const std::vector<std::pair<CPoint, CPoint>>& segs, const CColor& c)
{
    ctx->setLineWidth (1.0);
    ctx->setLineStyle (kLineSolid);
    ctx->setFrameColor (c);
    auto path = owned (ctx->createGraphicsPath ());
    if (!path)
    {
        for (const auto& s : segs)
            ctx->drawLine (s.first, s.second);
        return;
    }
    for (const auto& s : segs)
    {
        path->beginSubpath (s.first);
        path->addLine (s.second);
    }
    ctx->drawGraphicsPath (path, CDrawContext::kPathStroked);
}

void outline (CDrawContext* ctx, const CRect& r, const CColor& c, double radius)
{
    CRect o = r;
    o.inset (0.5, 0.5); // on the pixel grid: a crisp 1 px line just inside `r`
    ctx->setLineWidth (1.0);
    ctx->setLineStyle (kLineSolid);
    ctx->setFrameColor (c);
    auto path = owned (ctx->createGraphicsPath ());
    if (!path)
    {
        ctx->drawRect (r, kDrawStroked);
        return;
    }
    if (radius > 0.0)
        path->addRoundRect (o, radius);
    else
        path->addRect (o);
    ctx->drawGraphicsPath (path, CDrawContext::kPathStroked);
}

void brackets (CDrawContext* ctx, const CRect& r, double len, const CColor& c)
{
    // four L-shaped corner marks of `len` px along each edge, 1 px, just inside `r`
    const double l = r.left + 0.5, t = r.top + 0.5, rt = r.right - 0.5, b = r.bottom - 0.5;
    len = std::min (len, std::min (r.getWidth (), r.getHeight ()) / 2);
    hairlines (ctx,
               {{CPoint (l, t + len), CPoint (l, t)}, {CPoint (l, t), CPoint (l + len, t)},
                {CPoint (rt - len, t), CPoint (rt, t)}, {CPoint (rt, t), CPoint (rt, t + len)},
                {CPoint (rt, b - len), CPoint (rt, b)}, {CPoint (rt, b), CPoint (rt - len, b)},
                {CPoint (l + len, b), CPoint (l, b)}, {CPoint (l, b), CPoint (l, b - len)}},
               c);
}

void rule (CDrawContext* ctx, double x0, double x1, double y, const CColor& c)
{
    // one hairline, nothing hanging from it: the suite's divider is as quiet as a line can be
    y = std::floor (y) + 0.5;
    hairlines (ctx, {{CPoint (x0, y), CPoint (x1, y)}}, c);
}

void marker (CDrawContext* ctx, const CRect& r, const CColor& c)
{
    // the lamp of a button: a 2 px bar, 40 % of the width, just above the bottom edge
    const double w = std::max (4.0, std::round (r.getWidth () * 0.4));
    const double x = std::round (r.getCenter ().x - w / 2);
    fill (ctx, CRect (x, r.bottom - 4, x + w, r.bottom - 2), c);
}

void button (CDrawContext* ctx, const CRect& r, const std::string& label, ButtonState s)
{
    // An outlined button (no fill): copper outline, its lamp idle when off and live when on. Pressed
    // (held, or being dragged) the outline turns cinnabar: the interaction is lit, not the plate.
    // Disabled, everything drops to dim line, energy idle and text dim.
    const CColor edge = !s.enabled ? theme::kLineDim : (s.pressed ? theme::kEnergyLive : theme::kCopper);
    outline (ctx, r, edge);
    if (s.lamp)
        marker (ctx, r, s.lit && s.enabled ? theme::kEnergyLive : theme::kEnergyIdle);
    const CColor tc = !s.enabled ? theme::kTextDim : (s.lit ? theme::kText : theme::kCopperPale);
    CRect t = r;
    if (s.lamp && r.getHeight () >= 16)
        t.bottom -= 2; // lift the label off the lamp
    text (ctx, label, t, tc, 10.5, s.lit && s.enabled);
}

void handle (CDrawContext* ctx, const CPoint& c, double r, bool active, bool enabled)
{
    // a ring, not a filled dot: the well inside keeps the curve under it readable. A handle whose
    // element does not work drops to plain copper rather than dim line, so it can still be found.
    const CRect hr (c.x - r, c.y - r, c.x + r, c.y + r);
    const CColor col = active ? theme::kEnergyLive : (enabled ? theme::kCopperPale : theme::kCopper);
    ctx->setFillColor (theme::kWell);
    ctx->drawEllipse (hr, kDrawFilled);
    ctx->setLineWidth (1.5);
    ctx->setLineStyle (kLineSolid);
    ctx->setFrameColor (col);
    ctx->drawEllipse (hr, kDrawStroked);
    ctx->setLineWidth (1.0);
    if (active)
    {
        const double d = std::max (1.5, r * 0.3);
        ctx->setFillColor (col);
        ctx->drawEllipse (CRect (c.x - d, c.y - d, c.x + d, c.y + d), kDrawFilled);
    }
}

void window (CDrawContext* ctx, const CRect& r, double headerHeight)
{
    // The window: the ground, the header band as a well over a plain dim hairline, and a thin copper
    // frame with nested corner brackets (the outer at the edge, a shorter one inset by 3 px).
    fill (ctx, r, theme::kGround);
    if (headerHeight > 0)
    {
        fill (ctx, CRect (r.left, r.top, r.right, r.top + headerHeight), theme::kWell);
        rule (ctx, r.left + 6, r.right - 6, r.top + headerHeight - 1, theme::kLineDim);
    }
    outline (ctx, r, theme::withAlpha (theme::kCopper, 110), 0);
    brackets (ctx, r, 14, theme::kCopper);
    CRect in = r;
    in.inset (3, 3);
    brackets (ctx, in, 6, theme::kCopper);
}

} // namespace draw

//==============================================================================
Knob::Knob (const CRect& r, ParamHost* h, uint32_t id, const char* l, bool bi)
: ParamView (r, h, id), label (l ? l : host->table ().info (id).shortName), bipolar (bi)
{
}

CRect Knob::dialRect () const
{
    // as large as fits between the label (13 px, and 2 px clear) and the value strip, and 7 px in
    // from each side
    const CRect r = getViewSize ();
    const double size = std::min (r.getWidth () - 14.0, r.getHeight () - 30.0);
    const double cx = r.getCenter ().x, cy = r.top + 15 + size / 2;
    return CRect (cx - size / 2, cy - size / 2, cx + size / 2, cy + size / 2);
}

void Knob::draw (CDrawContext* ctx)
{
    const CRect r = getViewSize ();
    const double v = host->norm (param);
    // the label in pale copper (small text: plain copper is too dark for it), text dim when disabled
    text (ctx, label, CRect (r.left, r.top, r.right, r.top + 13), enabledLook ? theme::kCopperPale : theme::kTextDim, 10.5);

    const CRect kr = dialRect ();
    const CPoint c = kr.getCenter ();
    const double rad = kr.getWidth () / 2;
    auto at = [&] (double deg, double radius) {
        const double a = deg * M_PI / 180.0;
        return CPoint (c.x + std::cos (a) * radius, c.y + std::sin (a) * radius);
    };

    // Drawn as linework (docs/THEME.md, "Knobs and faders"): a dim-line track on the outer radius with
    // the value arc lit in cinnabar on it, a thin copper body circle inside it and a text-coloured
    // pointer up to the body. No fills, and no tick scale (it was decoration: the arc already shows
    // where the value sits, and the ticks made rows of knobs look busy). Disabled, the copper drops to
    // dim line and the arc to energy idle.
    const float start = 135.0f, sweep = 270.0f;
    const CColor copper = enabledLook ? theme::kCopper : theme::kLineDim;
    ctx->setLineStyle (CLineStyle (CLineStyle::kLineCapButt));
    ctx->setLineWidth (1.0);
    ctx->setFrameColor (theme::kLineDim);
    ctx->drawArc (kr, start, start + sweep, kDrawStroked);
    const float a0 = bipolar ? start + sweep * 0.5f : start;
    const float a1 = start + sweep * (float)v;
    ctx->setLineWidth (2.0);
    ctx->setFrameColor (enabledLook ? theme::kEnergyLive : theme::kEnergyIdle);
    if (std::fabs (a1 - a0) > 0.5f)
        ctx->drawArc (kr, std::min (a0, a1), std::max (a0, a1), kDrawStroked);
    ctx->setLineWidth (1.0);
    ctx->setFrameColor (copper);
    // the body: a clear gap inside the arc (about a fifth of the radius, at least 4 px) so the two
    // circles read as track and knob, not as a double line
    const double body = std::max (3.0, rad - std::max (4.0, rad * 0.22));
    ctx->drawEllipse (CRect (c.x - body, c.y - body, c.x + body, c.y + body), kDrawStroked);
    // pointer: from near the centre to the body circle, 1.5 px
    const double ang = start + sweep * v;
    ctx->setLineWidth (1.5);
    ctx->setFrameColor (enabledLook ? theme::kText : theme::kTextDim);
    ctx->drawLine (at (ang, body * 0.25), at (ang, body));
    ctx->setLineStyle (kLineSolid);
    ctx->setLineWidth (1.0);

    // the value: text colour, its unit dim; cinnabar while it is being dragged (the lit interaction)
    const CRect vr (r.left - 4, r.bottom - 13, r.right + 4, r.bottom);
    const std::string vt = host->valueText (param);
    if (dragging)
        text (ctx, vt, vr, theme::kEnergyLive, 10.0);
    else if (enabledLook)
        valueText (ctx, vt, vr, theme::kText, theme::kTextDim, 10.0);
    else
        text (ctx, vt, vr, theme::kTextDim, 10.0);
}

void Knob::onMouseDownEvent (MouseDownEvent& e)
{
    if (resetOnRightClick (e))
        return;
    if (!e.buttonState.isLeft ())
        return;
    if (e.clickCount == 2)
    {
        host->setOnce (param, host->table ().defaultNormalized (param));
        invalid ();
        e.consumed = true;
        return;
    }
    dragging = true;
    startY = e.mousePosition.y;
    startValue = dragValue = host->norm (param);
    host->beginEdit (param);
    e.consumed = true;
}

void Knob::onMouseMoveEvent (MouseMoveEvent& e)
{
    if (!dragging)
        return;
    const double range = isFine (e.modifiers) ? 1200.0 : 200.0;
    const double delta = (startY - e.mousePosition.y) / range;
    startY = e.mousePosition.y;
    dragValue = std::clamp (dragValue + delta, 0.0, 1.0);
    const int steps = host->table ().info (param).stepCount ();
    double v = dragValue;
    if (steps > 0)
        v = std::round (v * steps) / steps;
    host->setNorm (param, v);
    invalid ();
    e.consumed = true;
}

void Knob::onMouseUpEvent (MouseUpEvent& e)
{
    if (!dragging)
        return;
    dragging = false;
    host->endEdit (param);
    invalid ();
    e.consumed = true;
}

void Knob::onMouseCancelEvent (MouseCancelEvent& e)
{
    if (dragging)
    {
        dragging = false;
        host->endEdit (param);
    }
    e.consumed = true;
}

bool ParamView::resetOnRightClick (MouseDownEvent& e)
{
    if (!e.buttonState.isRight ())
        return false;
    host->setOnce (param, host->table ().defaultNormalized (param));
    invalid ();
    e.consumed = true;
    e.ignoreFollowUpMoveAndUpEvents (true);
    return true;
}

double wheelStep (const MouseWheelEvent& e, const ParamTable& table, uint32_t id)
{
    const double d = e.deltaY != 0.0 ? e.deltaY : e.deltaX;
    if (d == 0.0)
        return 0.0;
    const int steps = table.info (id).stepCount ();
    if (steps > 0)
        return (d > 0 ? 1.0 : -1.0) / steps;
    return 0.02 * std::clamp (d, -3.0, 3.0);
}

void Knob::onMouseWheelEvent (MouseWheelEvent& e)
{
    const int steps = host->table ().info (param).stepCount ();
    double step = steps > 0 ? 1.0 / steps : (isFine (e.modifiers) ? 0.002 : 0.01);
    const double d = e.deltaY != 0.0 ? e.deltaY : e.deltaX;
    if (d == 0.0)
        return;
    const double v = std::clamp (host->norm (param) + (d > 0 ? step : -step), 0.0, 1.0);
    host->setOnce (param, v);
    invalid ();
    e.consumed = true;
}

//==============================================================================
HSlider::HSlider (const CRect& r, ParamHost* h, uint32_t id, const char* l) : ParamView (r, h, id), label (l) {}

void HSlider::draw (CDrawContext* ctx)
{
    const CRect r = getViewSize ();
    const double v = host->norm (param);
    // a fader as linework: the bar a well with a thin copper outline; the amount a 2 px cinnabar line
    // along its bottom edge up to a 1 px position marker (no filled bar under the value text)
    CRect bar (r.left + 46, r.top + 2, r.right, r.bottom - 2);
    fill (ctx, bar, theme::kWell);
    draw::outline (ctx, bar, enabledLook ? theme::kCopper : theme::kLineDim);
    const CColor lit = enabledLook ? theme::kEnergyLive : theme::kEnergyIdle;
    const double x = std::round (bar.left + 1 + (bar.getWidth () - 2) * v);
    if (x > bar.left + 1.5)
        fill (ctx, CRect (bar.left + 1, bar.bottom - 3, x, bar.bottom - 1), lit);
    fill (ctx, CRect (std::min (x, bar.right - 2), bar.top + 1, std::min (x, bar.right - 2) + 1, bar.bottom - 1), lit);
    text (ctx, label, CRect (r.left, r.top, r.left + 44, r.bottom), enabledLook ? theme::kCopperPale : theme::kTextDim, 10.5,
          false, kLeftText);
    if (enabledLook)
        valueText (ctx, host->valueText (param), bar, theme::kText, theme::kTextDim, 10.0);
    else
        text (ctx, host->valueText (param), bar, theme::kTextDim, 10.0);
}

void HSlider::onMouseDownEvent (MouseDownEvent& e)
{
    if (resetOnRightClick (e))
        return;
    if (!e.buttonState.isLeft ())
        return;
    if (e.clickCount == 2)
    {
        host->setOnce (param, host->table ().defaultNormalized (param));
        invalid ();
        e.consumed = true;
        return;
    }
    dragging = true;
    startX = e.mousePosition.x;
    startValue = host->norm (param);
    host->beginEdit (param);
    e.consumed = true;
}

void HSlider::onMouseMoveEvent (MouseMoveEvent& e)
{
    if (!dragging)
        return;
    const double w = std::max (20.0, getViewSize ().getWidth () - 46.0) * (isFine (e.modifiers) ? 6.0 : 1.0);
    host->setNorm (param, std::clamp (startValue + (e.mousePosition.x - startX) / w, 0.0, 1.0));
    invalid ();
    e.consumed = true;
}

void HSlider::onMouseUpEvent (MouseUpEvent& e)
{
    if (!dragging)
        return;
    dragging = false;
    host->endEdit (param);
    e.consumed = true;
}

//==============================================================================
NumberBox::NumberBox (const CRect& r, ParamHost* h, uint32_t id, CColor c) : ParamView (r, h, id), color (c) {}

void NumberBox::draw (CDrawContext* ctx)
{
    const CRect r = getViewSize ();
    // a readout: the value in a well inside a thin copper bracket (corner marks, no box), the unit dim;
    // the bracket lights cinnabar while the value is dragged
    fill (ctx, r, theme::kWell);
    draw::brackets (ctx, r, 4, dragging ? theme::kEnergyLive : (enabledLook ? theme::kCopper : theme::kLineDim));
    if (dragging)
        text (ctx, host->valueText (param), r, theme::kEnergyLive, 10.0);
    else if (enabledLook)
        valueText (ctx, host->valueText (param), r, color, theme::kTextDim, 10.0);
    else
        text (ctx, host->valueText (param), r, theme::kTextDim, 10.0);
}

void NumberBox::onMouseDownEvent (MouseDownEvent& e)
{
    if (resetOnRightClick (e))
        return;
    if (!e.buttonState.isLeft ())
        return;
    if (e.clickCount == 2)
    {
        host->setOnce (param, host->table ().defaultNormalized (param));
        invalid ();
        e.consumed = true;
        return;
    }
    dragging = true;
    startY = e.mousePosition.y;
    dragValue = host->norm (param);
    host->beginEdit (param);
    invalid ();
    e.consumed = true;
}

void NumberBox::onMouseMoveEvent (MouseMoveEvent& e)
{
    if (!dragging)
        return;
    const double range = isFine (e.modifiers) ? 1200.0 : 200.0;
    dragValue = std::clamp (dragValue + (startY - e.mousePosition.y) / range, 0.0, 1.0);
    startY = e.mousePosition.y;
    const int steps = host->table ().info (param).stepCount ();
    host->setNorm (param, steps > 0 ? std::round (dragValue * steps) / steps : dragValue);
    invalid ();
    e.consumed = true;
}

void NumberBox::onMouseUpEvent (MouseUpEvent& e)
{
    if (!dragging)
        return;
    dragging = false;
    host->endEdit (param);
    invalid ();
    e.consumed = true;
}

void NumberBox::onMouseCancelEvent (MouseCancelEvent& e)
{
    if (dragging)
    {
        dragging = false;
        host->endEdit (param);
    }
    e.consumed = true;
}

void NumberBox::onMouseWheelEvent (MouseWheelEvent& e)
{
    const int steps = host->table ().info (param).stepCount ();
    const double step = steps > 0 ? 1.0 / steps : (isFine (e.modifiers) ? 0.002 : 0.01);
    const double d = e.deltaY != 0.0 ? e.deltaY : e.deltaX;
    if (d == 0.0)
        return;
    host->setOnce (param, std::clamp (host->norm (param) + (d > 0 ? step : -step), 0.0, 1.0));
    invalid ();
    e.consumed = true;
}

//==============================================================================
Toggle::Toggle (const CRect& r, ParamHost* h, uint32_t id, const char* l) : ParamView (r, h, id), label (l) {}

void Toggle::draw (CDrawContext* ctx)
{
    const CRect r = getViewSize ();
    const bool onState = host->norm (param) >= 0.5;
    draw::ButtonState s;
    s.lit = onState;
    s.enabled = enabledLook;
    draw::button (ctx, r, label, s);
}

void Toggle::onMouseDownEvent (MouseDownEvent& e)
{
    if (resetOnRightClick (e))
        return;
    if (!e.buttonState.isLeft ())
        return;
    host->setOnce (param, host->norm (param) >= 0.5 ? 0.0 : 1.0);
    invalid ();
    e.consumed = true;
    e.ignoreFollowUpMoveAndUpEvents (true);
}

//==============================================================================
Segmented::Segmented (const CRect& r, ParamHost* h, uint32_t id, std::vector<std::string> l)
: ParamView (r, h, id), labels (std::move (l))
{
    if (labels.empty ())
        for (auto* c : host->table ().info (id).choices)
            labels.push_back (c);
}

void Segmented::draw (CDrawContext* ctx)
{
    const CRect r = getViewSize ();
    const int n = (int)labels.size ();
    const int sel = (int)std::lround (host->plainValue (param) - host->table ().info (param).min);
    // one copper outline around the row, dim hairline dividers between the segments; every segment has a
    // lamp, idle, and the selected one lights it and its label (no filled segment)
    draw::outline (ctx, r, enabledLook ? theme::kCopper : theme::kLineDim);
    const double w = r.getWidth () / n;
    for (int i = 0; i < n; ++i)
    {
        CRect s (r.left + i * w, r.top, r.left + (i + 1) * w, r.bottom);
        if (i > 0)
        {
            const double x = std::floor (s.left) + 0.5;
            ctx->setFrameColor (theme::kLineDim);
            ctx->setLineWidth (1.0);
            ctx->drawLine (CPoint (x, s.top + 4), CPoint (x, s.bottom - 4));
        }
        const bool on = i == sel;
        draw::marker (ctx, s, on && enabledLook ? theme::kEnergyLive : theme::kEnergyIdle);
        CRect t = s;
        if (s.getHeight () >= 16)
            t.bottom -= 2;
        text (ctx, labels[(size_t)i], t, !enabledLook ? theme::kTextDim : (on ? theme::kText : theme::kCopperPale), 10.5,
              on && enabledLook);
    }
}

void Segmented::onMouseDownEvent (MouseDownEvent& e)
{
    if (resetOnRightClick (e))
        return;
    if (!e.buttonState.isLeft ())
        return;
    const CRect r = getViewSize ();
    const int n = (int)labels.size ();
    const int i = std::clamp ((int)((e.mousePosition.x - r.left) / (r.getWidth () / n)), 0, n - 1);
    host->setOnce (param, host->table ().toNormalized (param, host->table ().info (param).min + i));
    invalid ();
    e.consumed = true;
    e.ignoreFollowUpMoveAndUpEvents (true);
}

//==============================================================================
Choice::Choice (const CRect& r, ParamHost* h, uint32_t id, const char* l) : ParamView (r, h, id), label (l ? l : "") {}

void Choice::draw (CDrawContext* ctx)
{
    CRect r = getViewSize ();
    if (!label.empty ())
    {
        text (ctx, label, CRect (r.left, r.top, r.right, r.top + 13), enabledLook ? theme::kCopperPale : theme::kTextDim,
              10.5);
        r.top += 15;
    }
    // a field: a well in a thin copper outline, the value in text colour, a stroked chevron for the caret
    fill (ctx, r, theme::kWell);
    draw::outline (ctx, r, enabledLook ? theme::kCopper : theme::kLineDim);
    CRect t = r;
    t.inset (6, 0);
    t.right -= 10;
    text (ctx, host->valueText (param), t, enabledLook ? theme::kText : theme::kTextDim, 10.5, false, kLeftText);
    const double cx = std::floor (r.right - 10) + 0.5, cy = std::floor (r.getCenter ().y) + 0.5;
    ctx->setLineWidth (1.0);
    ctx->setFrameColor (enabledLook ? theme::kCopperPale : theme::kTextDim);
    ctx->drawLine (CPoint (cx - 3, cy - 1.5), CPoint (cx, cy + 1.5));
    ctx->drawLine (CPoint (cx, cy + 1.5), CPoint (cx + 3, cy - 1.5));
}

void Choice::onMouseDownEvent (MouseDownEvent& e)
{
    if (resetOnRightClick (e))
        return;
    if (!e.buttonState.isLeft ())
        return;
    auto* frame = getFrame ();
    if (!frame)
        return;
    auto menu = makeOwned<COptionMenu> ();
    const auto& info = host->table ().info (param);
    const int sel = (int)std::lround (host->plainValue (param));
    for (size_t i = 0; i < info.choices.size (); ++i)
        menu->addEntry (info.choices[i], -1, (int)i == sel ? CMenuItem::kChecked : CMenuItem::kNoFlags);
    CPoint where (getViewSize ().left, getViewSize ().bottom);
    localToFrame (where);
    ParamHost* h = host;
    const uint32_t id = param;
    SharedPointer<CView> self (this);
    menu->popup (frame, where, [h, id, self, menu] (COptionMenu* m) {
        const int32_t idx = m->getLastResult ();
        if (idx >= 0)
        {
            h->setOnce (id, h->table ().toNormalized (id, (double)idx));
            self->invalid ();
        }
    });
    e.consumed = true;
    e.ignoreFollowUpMoveAndUpEvents (true);
}

//==============================================================================
ActionButton::ActionButton (const CRect& r, std::string t, std::function<void ()> f, std::function<bool ()> a)
: CView (r), text (std::move (t)), onClick (std::move (f)), active (std::move (a))
{
}

void ActionButton::draw (CDrawContext* ctx)
{
    const CRect r = getViewSize ();
    const bool lit = active && active ();
    // a lamp only on buttons with a lit state (`active`): a plain push button is just its outline
    draw::ButtonState s;
    s.lit = lit;
    s.lamp = (bool)active;
    s.pressed = pressed;
    draw::button (ctx, r, text, s);
}

void ActionButton::onMouseDownEvent (MouseDownEvent& e)
{
    if (!e.buttonState.isLeft ())
        return;
    pressed = true;
    invalid ();
    e.consumed = true;
}

void ActionButton::onMouseUpEvent (MouseUpEvent& e)
{
    if (!pressed)
        return;
    pressed = false;
    invalid ();
    e.consumed = true;
    if (getViewSize ().pointInside (e.mousePosition) && onClick)
        onClick ();
}

//==============================================================================
Label::Label (const CRect& r, std::string t, double s, bool b, int a)
: CView (r), text (std::move (t)), size (s), bold (b), align (a)
{
    setMouseEnabled (false);
}

void Label::draw (CDrawContext* ctx)
{
    const CHoriTxtAlign al = align == 0 ? kLeftText : (align == 1 ? kCenterText : kRightText);
    ::pk::text (ctx, text, getViewSize (), dim ? theme::kTextDim : theme::kText, size, bold, al);
}

//==============================================================================
Panel::Panel (const CRect& r, std::string t) : CViewContainer (r), title (std::move (t))
{
    setBackgroundColor (kTransparentCColor);
}

void Panel::drawBackgroundRect (CDrawContext* ctx, const CRect&)
{
    CRect r (0, 0, getViewSize ().getWidth (), getViewSize ().getHeight ());
    // A section as a drafting frame, not a card: the panel surface (barely lifted from the ground) inside
    // a square dim hairline, copper corner brackets over its corners, and the title in small uppercase
    // pale copper on plain space (no rule after it: the ticked rule it had was decoration, and with
    // controls in the title strip it only added clutter).
    fill (ctx, r, theme::kPanel);
    draw::outline (ctx, r, theme::kLineDim, 0);
    draw::brackets (ctx, r, 6, theme::kCopper);
    if (!title.empty ())
    {
        std::string t = title;
        for (auto& ch : t)
            ch = (char)std::toupper ((unsigned char)ch);
        text (ctx, t, CRect (8, 3, r.right - 8, 18), theme::kCopperPale, 9.5, true, kLeftText);
    }
}

Group::Group (const CRect& r) : CViewContainer (r)
{
    setBackgroundColor (kTransparentCColor);
}

} // namespace pk

//==============================================================================
// The texts each widget draws, for the layout check (pk::layoutReport): the same areas, fonts and
// alignments as their draw() above.
namespace pk {

std::vector<std::string> ParamView::sampleTexts () const
{
    const ParamTable& t = host->table ();
    std::vector<std::string> out;
    for (double n : {0.0, 1.0, t.defaultNormalized (param), host->norm (param)})
        out.push_back (t.toText (param, t.toPlain (param, n)));
    return out;
}

std::vector<TextSpot> Knob::textSpots () const
{
    const CRect r = getViewSize ();
    std::vector<TextSpot> out {{CRect (r.left, r.top, r.right, r.top + 13), label, 10.5, false, 1, 0}};
    for (const auto& s : sampleTexts ())
        out.push_back ({CRect (r.left - 4, r.bottom - 13, r.right + 4, r.bottom), s, 10.0, false, 1, 0});
    return out;
}

std::vector<TextSpot> HSlider::textSpots () const
{
    const CRect r = getViewSize ();
    std::vector<TextSpot> out {{CRect (r.left, r.top, r.left + 44, r.bottom), label, 10.5, false, 0, 0}};
    for (const auto& s : sampleTexts ())
        out.push_back ({CRect (r.left + 46, r.top + 2, r.right, r.bottom - 2), s, 10.0, false, 1, 4});
    return out;
}

std::vector<TextSpot> NumberBox::textSpots () const
{
    std::vector<TextSpot> out;
    for (const auto& s : sampleTexts ())
        out.push_back ({getViewSize (), s, 10.0, false, 1, 4});
    return out;
}

std::vector<TextSpot> Toggle::textSpots () const
{
    // (lit, the label is bold: the wider of the two)
    return {{getViewSize (), label, 10.5, true, 1, 6}};
}

std::vector<TextSpot> ActionButton::textSpots () const { return {{getViewSize (), text, 10.5, (bool)active, 1, 6}}; }

std::vector<TextSpot> Segmented::textSpots () const
{
    const CRect r = getViewSize ();
    const double w = r.getWidth () / std::max<size_t> (1, labels.size ());
    std::vector<TextSpot> out;
    for (size_t i = 0; i < labels.size (); ++i)
        out.push_back ({CRect (r.left + (double)i * w, r.top, r.left + (double)(i + 1) * w, r.bottom), labels[i], 10.5, true, 1, 4});
    return out;
}

std::vector<TextSpot> Choice::textSpots () const
{
    CRect r = getViewSize ();
    std::vector<TextSpot> out;
    if (!label.empty ())
    {
        out.push_back ({CRect (r.left, r.top, r.right, r.top + 13), label, 10.5, false, 1, 0});
        r.top += 15;
    }
    CRect t = r;
    t.inset (6, 0);
    t.right -= 10;
    for (const char* c : host->table ().info (param).choices)
        out.push_back ({t, c, 10.5, false, 0, 0});
    if (host->table ().info (param).choices.empty ())
        for (const auto& s : sampleTexts ())
            out.push_back ({t, s, 10.5, false, 0, 0});
    return out;
}

std::vector<TextSpot> Label::textSpots () const { return {{getViewSize (), text, size, bold, align, 0}}; }

} // namespace pk
