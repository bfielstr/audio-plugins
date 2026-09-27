#include "pluginkit/ui/Widgets.h"
#include "pluginkit/ui/Theme.h"

#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cframe.h"
#include "vstgui/lib/cgraphicspath.h"
#include "vstgui/lib/controls/coptionmenu.h"
#include "vstgui/lib/events.h"

#include <algorithm>
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

void roundRect (CDrawContext* ctx, const CRect& r, double radius, const CColor& fill, const CColor* stroke = nullptr)
{
    auto path = owned (ctx->createGraphicsPath ());
    if (!path)
    {
        ctx->setFillColor (fill);
        ctx->drawRect (r, kDrawFilled);
        return;
    }
    path->addRoundRect (r, radius);
    ctx->setFillColor (fill);
    ctx->drawGraphicsPath (path, CDrawContext::kPathFilled);
    if (stroke)
    {
        ctx->setFrameColor (*stroke);
        ctx->setLineWidth (1.0);
        ctx->drawGraphicsPath (path, CDrawContext::kPathStroked);
    }
}

bool isFine (const Modifiers& m) { return m.has (ModifierKey::Shift) || m.has (ModifierKey::Control); }

} // namespace

//==============================================================================
Knob::Knob (const CRect& r, ParamHost* h, uint32_t id, const char* l, bool bi)
: ParamView (r, h, id), label (l ? l : host->table ().info (id).shortName), bipolar (bi)
{
}

void Knob::draw (CDrawContext* ctx)
{
    const CRect r = getViewSize ();
    const double v = host->norm (param);
    const CColor dimText = enabledLook ? theme::kText : theme::kTextDim;
    text (ctx, label, CRect (r.left, r.top, r.right, r.top + 13), dimText, 10.5);

    const double size = std::min (r.getWidth () - 14.0, r.getHeight () - 30.0);
    const CPoint c (r.getCenter ().x, r.top + 15 + size / 2);
    const CRect kr (c.x - size / 2, c.y - size / 2, c.x + size / 2, c.y + size / 2);

    const float start = 135.0f, sweep = 270.0f;
    ctx->setLineWidth (3.0);
    ctx->setLineStyle (CLineStyle (CLineStyle::kLineCapRound));
    ctx->setFrameColor (theme::kKnobTrack);
    ctx->drawArc (kr, start, start + sweep, kDrawStroked);
    const float a0 = bipolar ? start + sweep * 0.5f : start;
    const float a1 = start + sweep * (float)v;
    ctx->setFrameColor (enabledLook ? theme::kAccent : theme::kAccentDim);
    if (std::fabs (a1 - a0) > 0.5f)
        ctx->drawArc (kr, std::min (a0, a1), std::max (a0, a1), kDrawStroked);
    // pointer
    const double ang = (start + sweep * v) * M_PI / 180.0;
    const CPoint tip (c.x + std::cos (ang) * size * 0.42, c.y + std::sin (ang) * size * 0.42);
    const CPoint in (c.x + std::cos (ang) * size * 0.12, c.y + std::sin (ang) * size * 0.12);
    ctx->setLineWidth (2.0);
    ctx->setFrameColor (enabledLook ? theme::kTextBright : theme::kTextDim);
    ctx->drawLine (in, tip);
    ctx->setLineStyle (kLineSolid);

    text (ctx, host->valueText (param), CRect (r.left - 4, r.bottom - 13, r.right + 4, r.bottom),
          dragging ? theme::kAccent : dimText, 10.0);
}

void Knob::onMouseDownEvent (MouseDownEvent& e)
{
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
    CRect bar (r.left + 46, r.top + 2, r.right, r.bottom - 2);
    roundRect (ctx, bar, 2.0, theme::kControlBg);
    CRect fill = bar;
    fill.right = bar.left + bar.getWidth () * v;
    if (fill.getWidth () > 0.5)
        roundRect (ctx, fill, 2.0, enabledLook ? theme::kAccentDim : theme::kKnobTrack);
    text (ctx, label, CRect (r.left, r.top, r.left + 44, r.bottom), enabledLook ? theme::kText : theme::kTextDim, 10.5,
          false, kLeftText);
    text (ctx, host->valueText (param), bar, theme::kTextBright, 10.0);
}

void HSlider::onMouseDownEvent (MouseDownEvent& e)
{
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
Toggle::Toggle (const CRect& r, ParamHost* h, uint32_t id, const char* l) : ParamView (r, h, id), label (l) {}

void Toggle::draw (CDrawContext* ctx)
{
    const CRect r = getViewSize ();
    const bool onState = host->norm (param) >= 0.5;
    roundRect (ctx, r, 3.0, onState ? theme::kControlOn : theme::kControlBg);
    text (ctx, label, r, onState ? CColor (20, 20, 20) : (enabledLook ? theme::kText : theme::kTextDim), 10.5, onState);
}

void Toggle::onMouseDownEvent (MouseDownEvent& e)
{
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
    roundRect (ctx, r, 3.0, theme::kControlBg);
    const double w = r.getWidth () / n;
    for (int i = 0; i < n; ++i)
    {
        CRect s (r.left + i * w, r.top, r.left + (i + 1) * w, r.bottom);
        if (i == sel)
            roundRect (ctx, s, 3.0, enabledLook ? theme::kControlOn : theme::kKnobTrack);
        else if (i > 0 && i != sel + 1)
        {
            ctx->setFrameColor (theme::kPanelEdge);
            ctx->setLineWidth (1.0);
            ctx->drawLine (CPoint (s.left, s.top + 4), CPoint (s.left, s.bottom - 4));
        }
        text (ctx, labels[(size_t)i], s, i == sel ? CColor (20, 20, 20) : theme::kText, 10.5, i == sel);
    }
}

void Segmented::onMouseDownEvent (MouseDownEvent& e)
{
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
        text (ctx, label, CRect (r.left, r.top, r.right, r.top + 13), enabledLook ? theme::kText : theme::kTextDim,
              10.5);
        r.top += 15;
    }
    roundRect (ctx, r, 3.0, theme::kControlBg);
    CRect t = r;
    t.inset (6, 0);
    t.right -= 10;
    text (ctx, host->valueText (param), t, enabledLook ? theme::kTextBright : theme::kTextDim, 10.5, false, kLeftText);
    // caret
    const double cx = r.right - 10, cy = r.getCenter ().y;
    ctx->setFillColor (theme::kTextDim);
    ctx->drawPolygon ({CPoint (cx - 3.5, cy - 2), CPoint (cx + 3.5, cy - 2), CPoint (cx, cy + 2.5)}, kDrawFilled);
}

void Choice::onMouseDownEvent (MouseDownEvent& e)
{
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
    roundRect (ctx, r, 3.0, pressed ? theme::kKnobTrack : (lit ? theme::kControlOn : theme::kControlBg));
    ::pk::text (ctx, text, r, lit ? CColor (20, 20, 20) : theme::kText, 10.5, lit);
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
    roundRect (ctx, r, 5.0, theme::kPanel, &theme::kPanelEdge);
    if (!title.empty ())
        text (ctx, title, CRect (8, 3, r.right - 8, 18), theme::kTextDim, 10.0, true, kLeftText);
}

Group::Group (const CRect& r) : CViewContainer (r)
{
    setBackgroundColor (kTransparentCColor);
}

} // namespace pk
