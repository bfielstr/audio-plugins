#include "ModView.h"

#include "Editor.h"
#include "UiKit.h"

#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cgraphicspath.h"
#include "vstgui/lib/events.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace smemplr {

using namespace VSTGUI;

CColor modColor (int lfo)
{
    static const CColor colors[kModLfos] = {CColor (80, 200, 255), CColor (255, 105, 200), CColor (130, 220, 110), CColor (185, 145, 255)};
    return colors[(size_t)std::clamp (lfo, 0, kModLfos - 1)];
}

namespace {
CColor withAlpha (CColor c, uint8_t a)
{
    c.alpha = a;
    return c;
}

void roundRect (CDrawContext* ctx, const CRect& r, double radius, const CColor* fill, const CColor* frame, double width = 1.0)
{
    if (auto path = owned (ctx->createGraphicsPath ()))
    {
        path->addRoundRect (r, radius);
        if (fill)
        {
            ctx->setFillColor (*fill);
            ctx->drawGraphicsPath (path, CDrawContext::kPathFilled);
        }
        if (frame)
        {
            ctx->setFrameColor (*frame);
            ctx->setLineWidth (width);
            ctx->drawGraphicsPath (path, CDrawContext::kPathStroked);
        }
        return;
    }
    if (fill)
    {
        ctx->setFillColor (*fill);
        ctx->drawRect (r, kDrawFilled);
    }
    if (frame)
    {
        ctx->setFrameColor (*frame);
        ctx->setLineWidth (width);
        ctx->drawRect (r, kDrawStroked);
    }
}

// a knob's angle (degrees, as pk::Knob: 135 .. 405) for a normalized value
double knobAngle (double v) { return 135.0 + 270.0 * std::clamp (v, 0.0, 1.0); }
} // namespace

// --- the handle -------------------------------------------------------------------------------------
CPoint LfoHandle::toFrame (CPoint p) const
{
    localToFrame (p);
    return p;
}

void LfoHandle::draw (CDrawContext* ctx)
{
    const CRect r = getViewSize ();
    const CColor c = modColor (lfo);
    const CColor fill = pressed ? withAlpha (c, 70) : theme::kControlBg;
    roundRect (ctx, r, 4.0, &fill, &c, 1.5);
    char buf[16];
    std::snprintf (buf, sizeof (buf), "LFO %d", lfo + 1);
    ctx->setFont (theme::font (11.0, true));
    ctx->setFontColor (theme::kTextBright);
    ctx->drawString (buf, CRect (r.left, r.top + 3, r.right, r.top + 18), kCenterText, true);
    // the grip (drag me), and the LFO's value as a bar from the middle
    ctx->setFillColor (theme::kTextDim);
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 2; ++j)
            ctx->drawEllipse (CRect (r.getCenter ().x - 6 + i * 4, r.top + 20 + j * 4, r.getCenter ().x - 4 + i * 4, r.top + 22 + j * 4),
                              kDrawFilled);
    const double v = std::clamp ((double)ed->lfoValueNow (lfo), -1.0, 1.0);
    const double mid = r.getCenter ().x, half = r.getWidth () / 2 - 6;
    ctx->setFillColor (c);
    ctx->drawRect (CRect (std::min (mid, mid + v * half), r.bottom - 7, std::max (mid, mid + v * half) + 1, r.bottom - 4), kDrawFilled);
}

void LfoHandle::onMouseDownEvent (MouseDownEvent& e)
{
    if (!e.buttonState.isLeft ())
        return;
    pressed = true;
    dragging = false;
    down = e.mousePosition;
    invalid ();
    e.consumed = true;
}

void LfoHandle::onMouseMoveEvent (MouseMoveEvent& e)
{
    if (!pressed)
        return;
    e.consumed = true;
    if (!dragging && std::hypot (e.mousePosition.x - down.x, e.mousePosition.y - down.y) < 4.0)
        return;
    dragging = true;
    ed->lfoDragged (lfo, toFrame (e.mousePosition), false);
}

void LfoHandle::onMouseUpEvent (MouseUpEvent& e)
{
    if (!pressed)
        return;
    e.consumed = true;
    pressed = false;
    if (dragging)
        ed->lfoDragged (lfo, toFrame (e.mousePosition), true);
    dragging = false;
    invalid ();
}

void LfoHandle::onMouseCancelEvent (MouseCancelEvent& e)
{
    if (dragging)
        ed->lfoDragCancelled ();
    pressed = dragging = false;
    invalid ();
    e.consumed = true;
}

// --- the scope --------------------------------------------------------------------------------------
void LfoScope::draw (CDrawContext* ctx)
{
    const CRect r = getViewSize ();
    ctx->setFillColor (theme::kWaveBg);
    ctx->drawRect (r, kDrawFilled);
    ctx->setFrameColor (theme::kGrid);
    ctx->setLineWidth (1.0);
    ctx->drawLine (CPoint (r.left, r.getCenter ().y), CPoint (r.right, r.getCenter ().y));
    const int shape = ed->lfoShapeNow (lfo);
    const double phaseOffset = ed->lfoPhaseOffset (lfo);
    // the random shapes drawn with made-up values (they are different every cycle)
    const float rnd[5] = {0.2f, -0.7f, 0.8f, -0.1f, 0.5f};
    auto at = [&] (double x) {
        if (shape >= kModRandom)
        {
            const int k = std::min (3, (int)(x * 4.0));
            return modShape (shape, x * 4.0 - k, rnd[k], rnd[k + 1]);
        }
        return modShape (shape, x + phaseOffset, 0.0f, 0.0f);
    };
    const double h = r.getHeight () / 2 - 4;
    const CColor c = modColor (lfo);
    ctx->setFrameColor (withAlpha (c, 200));
    ctx->setLineWidth (1.5);
    CPoint last;
    const int n = (int)r.getWidth ();
    for (int i = 0; i <= n; ++i)
    {
        const double x = (double)i / n;
        const CPoint pt (r.left + i, r.getCenter ().y - at (std::min (x, 0.99999)) * h);
        if (i > 0 && std::fabs (pt.y - last.y) < h * 1.5)
            ctx->drawLine (last, pt);
        else if (i > 0)
            ctx->drawLine (CPoint (pt.x, last.y), pt);
        last = pt;
    }
    // where it is now (the random shapes: their value, at the right)
    const double ph = shape >= kModRandom ? 1.0 : ed->lfoPhaseNow (lfo) - phaseOffset;
    const double x = r.left + (ph - std::floor (ph) + (shape >= kModRandom ? 0.999 : 0.0)) * r.getWidth ();
    const double y = r.getCenter ().y - std::clamp ((double)ed->lfoValueNow (lfo), -1.0, 1.0) * h;
    ctx->setFillColor (theme::kTextBright);
    ctx->drawEllipse (CRect (x - 3, y - 3, x + 3, y + 3), kDrawFilled);
}

// --- the overlay ------------------------------------------------------------------------------------
ModOverlay::ModOverlay (const CRect& r, Editor* e) : CView (r), ed (e)
{
    // the controls under it keep their hover help (CFrame finds the view under the mouse by its area)
    setMouseableArea (CRect ());
}

void ModOverlay::knobDial (const CRect& r, CPoint& centre, double& radius)
{
    const double size = std::min (r.getWidth () - 14.0, r.getHeight () - 30.0);
    centre = CPoint (r.getCenter ().x, r.top + 15 + size / 2);
    radius = size / 2;
}

CRect ModOverlay::area (const ModRing& g)
{
    CRect a = g.r;
    a.extend (6 + 3 * (double)g.maps.size (), 6 + 3 * (double)g.maps.size ());
    return a;
}

void ModOverlay::setRings (std::vector<ModRing> next)
{
    for (const auto& g : rings)
        invalidRect (area (g));
    rings = std::move (next);
    for (const auto& g : rings)
        invalidRect (area (g));
}

void ModOverlay::setDragTarget (const CRect& r, int lfo)
{
    if (r == dragRect && lfo == dragLfo)
        return;
    CRect old = dragRect;
    old.extend (3, 3);
    invalidRect (old);
    dragRect = r;
    dragLfo = lfo;
    CRect now = dragRect;
    now.extend (3, 3);
    invalidRect (now);
}

void ModOverlay::draw (CDrawContext* ctx)
{
    const ModMap map = ed->modMap ();
    ctx->setLineStyle (CLineStyle (CLineStyle::kLineCapRound));
    for (const auto& g : rings)
    {
        const double base = ed->norm (g.target);
        double sum = 0.0;
        bool any = false;
        for (size_t i : g.maps)
        {
            float off;
            if (ed->modOffsetNow (i, off))
            {
                sum += off;
                any = true;
            }
        }
        const double now = std::clamp (base + sum, 0.0, 1.0);
        if (g.knob)
        {
            CPoint c;
            double dial;
            knobDial (g.r, c, dial);
            for (size_t k = 0; k < g.maps.size (); ++k)
            {
                if (g.maps[k] >= map.list.size ())
                    continue;
                const ModMapping& m = map.list[g.maps[k]];
                float off;
                const bool working = ed->modOffsetNow (g.maps[k], off);
                const double rad = ringRadius (dial, k);
                const CRect rr (c.x - rad, c.y - rad, c.x + rad, c.y + rad);
                ctx->setLineWidth (2.0);
                ctx->setFrameColor (withAlpha (theme::kKnobTrack, 160));
                ctx->drawArc (rr, 135.0f, 405.0f, kDrawStroked);
                const double a0 = knobAngle (base), a1 = knobAngle (base + m.depth);
                ctx->setFrameColor (withAlpha (modColor (m.lfo), working ? 255 : 90));
                if (std::fabs (a1 - a0) > 0.5)
                    ctx->drawArc (rr, (float)std::min (a0, a1), (float)std::max (a0, a1), kDrawStroked);
            }
            if (any)
            {
                // where the parameter is now, on the inner ring
                const double ang = knobAngle (now) * M_PI / 180.0, rad = ringRadius (dial, 0);
                const CPoint p (c.x + std::cos (ang) * rad, c.y + std::sin (ang) * rad);
                ctx->setFillColor (theme::kTextBright);
                ctx->drawEllipse (CRect (p.x - 2.5, p.y - 2.5, p.x + 2.5, p.y + 2.5), kDrawFilled);
            }
        }
        else
        {
            // a line under the control: the depth from its value, and where it is now
            const double y = g.r.bottom + 1.5, w = g.r.getWidth ();
            for (size_t k = 0; k < g.maps.size (); ++k)
            {
                if (g.maps[k] >= map.list.size ())
                    continue;
                const ModMapping& m = map.list[g.maps[k]];
                float off;
                const bool working = ed->modOffsetNow (g.maps[k], off);
                const double x0 = g.r.left + base * w, x1 = g.r.left + std::clamp (base + m.depth, 0.0, 1.0) * w;
                ctx->setFillColor (withAlpha (modColor (m.lfo), working ? 255 : 90));
                ctx->drawRect (CRect (std::min (x0, x1), y + 2.0 * k, std::max (x0, x1) + 1, y + 2.0 * k + 2), kDrawFilled);
            }
            if (any)
            {
                const double x = g.r.left + now * w;
                ctx->setFillColor (theme::kTextBright);
                ctx->drawRect (CRect (x - 1, y - 2, x + 1, y + 2.0 * g.maps.size () + 1), kDrawFilled);
            }
        }
    }
    ctx->setLineStyle (kLineSolid);
    if (!dragRect.isEmpty ())
    {
        CRect r = dragRect;
        r.extend (2, 2);
        const CColor c = modColor (dragLfo);
        const CColor fill = withAlpha (c, 40);
        roundRect (ctx, r, 4.0, &fill, &c, 2.0);
    }
}

bool ModOverlay::ringAt (const CPoint& p, size_t& mapping) const
{
    for (const auto& g : rings)
    {
        if (!g.knob)
            continue;
        CPoint c;
        double dial;
        knobDial (g.r, c, dial);
        const double dx = p.x - c.x, dy = p.y - c.y, d = std::hypot (dx, dy);
        const double ang = std::atan2 (dy, dx) * 180.0 / M_PI;
        if (ang > 50.0 && ang < 130.0)
            continue; // the gap at the bottom (the knob's value is shown there)
        for (size_t k = 0; k < g.maps.size (); ++k)
            if (std::fabs (d - ringRadius (dial, k)) <= 1.6)
            {
                mapping = g.maps[k];
                return true;
            }
    }
    return false;
}

bool ModOverlay::hitTest (const CPoint& where, const Event&)
{
    size_t m;
    return editing || ringAt (where, m);
}

void ModOverlay::onMouseDownEvent (MouseDownEvent& e)
{
    size_t m;
    if (!ringAt (e.mousePosition, m))
        return;
    e.consumed = true;
    if (e.buttonState.isRight ())
    {
        ed->removeMod (m);
        e.ignoreFollowUpMoveAndUpEvents (true);
        return;
    }
    if (!e.buttonState.isLeft ())
        return;
    editing = true;
    editMap = m;
    lastY = e.mousePosition.y;
}

void ModOverlay::onMouseMoveEvent (MouseMoveEvent& e)
{
    if (!editing)
        return;
    e.consumed = true;
    const double range = e.modifiers.has (ModifierKey::Shift) ? 1200.0 : 200.0;
    const ModMap map = ed->modMap ();
    if (editMap < map.list.size ())
        ed->setModDepth (editMap, map.list[editMap].depth + (lastY - e.mousePosition.y) / range);
    lastY = e.mousePosition.y;
}

void ModOverlay::onMouseUpEvent (MouseUpEvent& e)
{
    if (!editing)
        return;
    editing = false;
    e.consumed = true;
}

void ModOverlay::onMouseCancelEvent (MouseCancelEvent& e)
{
    editing = false;
    e.consumed = true;
}

// --- the list ---------------------------------------------------------------------------------------
namespace {
// a row's parts: the depth and the remove cross (the rest is the LFO and the parameter)
CRect depthPart (const CRect& row) { return CRect (row.right - 62, row.top, row.right - 16, row.bottom); }
CRect crossPart (const CRect& row) { return CRect (row.right - 14, row.top, row.right, row.bottom); }
} // namespace

void ModList::draw (CDrawContext* ctx)
{
    const CRect r = getViewSize ();
    ctx->setFillColor (theme::kWaveBg);
    ctx->drawRect (r, kDrawFilled);
    const ModMap map = ed->modMap ();
    if (map.list.empty ())
    {
        ctx->setFont (theme::font (10.0));
        ctx->setFontColor (theme::kTextDim);
        ctx->drawString ("Drag an LFO onto a control", CRect (r.left, r.top + 4, r.right, r.top + 20), kCenterText, true);
        ctx->drawString ("to modulate it.", CRect (r.left, r.top + 20, r.right, r.top + 36), kCenterText, true);
        return;
    }
    for (size_t i = 0; i < map.list.size (); ++i)
    {
        const ModMapping& m = map.list[i];
        const CRect row (r.left, r.top + i * kRow, r.right, r.top + (i + 1) * kRow);
        if (row.bottom > r.bottom)
            break;
        if (i % 2 == 1)
        {
            ctx->setFillColor (CColor (30, 30, 32));
            ctx->drawRect (row, kDrawFilled);
        }
        float off;
        const bool working = ed->modOffsetNow (i, off);
        const CColor c = modColor (m.lfo);
        ctx->setFillColor (c);
        ctx->drawRect (CRect (row.left + 3, row.top + 4, row.left + 13, row.bottom - 4), kDrawFilled);
        char buf[48];
        std::snprintf (buf, sizeof (buf), "%d", m.lfo + 1);
        ctx->setFont (theme::font (9.0, true));
        ctx->setFontColor (CColor (20, 20, 20));
        ctx->drawString (buf, CRect (row.left + 3, row.top + 2, row.left + 13, row.bottom - 2), kCenterText, true);
        ctx->setFont (theme::font (10.0));
        ctx->setFontColor (working ? theme::kText : theme::kTextDim);
        ctx->drawString (ed->modTargetName (m).c_str (), CRect (row.left + 17, row.top, depthPart (row).left - 2, row.bottom), kLeftText, true);
        std::snprintf (buf, sizeof (buf), "%+.0f %%", m.depth * 100.0);
        ctx->setFontColor (editing && editRow == i ? theme::kAccent : (working ? theme::kTextBright : theme::kTextDim));
        ctx->drawString (buf, depthPart (row), kRightText, true);
        ctx->setFontColor (theme::kTextDim);
        ctx->drawString ("x", crossPart (row), kCenterText, true);
    }
}

void ModList::onMouseDownEvent (MouseDownEvent& e)
{
    const CRect r = getViewSize ();
    const ModMap map = ed->modMap ();
    const double y = e.mousePosition.y - r.top;
    if (y < 0)
        return;
    const size_t i = (size_t)(y / kRow);
    if (i >= map.list.size ())
        return;
    e.consumed = true;
    const CRect row (r.left, r.top + i * kRow, r.right, r.top + (i + 1) * kRow);
    if (e.buttonState.isRight () || (e.buttonState.isLeft () && crossPart (row).pointInside (e.mousePosition)))
    {
        ed->removeMod (i);
        e.ignoreFollowUpMoveAndUpEvents (true);
        invalid ();
        return;
    }
    if (!e.buttonState.isLeft () || !depthPart (row).pointInside (e.mousePosition))
        return;
    if (e.clickCount == 2)
    {
        ed->setModDepth (i, -map.list[i].depth);
        e.ignoreFollowUpMoveAndUpEvents (true);
        return;
    }
    editing = true;
    editRow = i;
    lastY = e.mousePosition.y;
    invalid ();
}

void ModList::onMouseMoveEvent (MouseMoveEvent& e)
{
    if (!editing)
        return;
    e.consumed = true;
    const double range = e.modifiers.has (ModifierKey::Shift) ? 1200.0 : 200.0;
    const ModMap map = ed->modMap ();
    if (editRow < map.list.size ())
        ed->setModDepth (editRow, map.list[editRow].depth + (lastY - e.mousePosition.y) / range);
    lastY = e.mousePosition.y;
}

void ModList::onMouseUpEvent (MouseUpEvent& e)
{
    if (!editing)
        return;
    editing = false;
    e.consumed = true;
    invalid ();
}

void ModList::onMouseCancelEvent (MouseCancelEvent& e)
{
    editing = false;
    e.consumed = true;
    invalid ();
}

} // namespace smemplr
