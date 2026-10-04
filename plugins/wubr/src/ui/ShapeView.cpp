#include "ShapeView.h"

#include "pluginkit/ui/Theme.h"

#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cframe.h"
#include "vstgui/lib/cgraphicspath.h"
#include "vstgui/lib/events.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace wubr {

using namespace VSTGUI;
namespace theme = pk::theme;

namespace {
constexpr double kPointRadius = 5.0, kPad = 10.0;
// one colour for both bands (docs/THEME.md): pale copper; the title names the band
CColor bandColor (int band, uint8_t alpha = 255)
{
    (void)band;
    return theme::withAlpha (theme::kCopperPale, alpha);
}
} // namespace

ShapeView::ShapeView (const CRect& r, pk::ParamHost* h, int b, MeterSource m) : CView (r), host (h), band (b), meters (std::move (m)) {}

double ShapeView::xOf (double x) const
{
    const CRect r = getViewSize ();
    return r.left + kPad + x * (r.getWidth () - 2 * kPad);
}

double ShapeView::yOf (double y) const
{
    const CRect r = getViewSize ();
    return r.getCenter ().y - y * (r.getHeight () * 0.5 - kPad - 8.0);
}

Shape ShapeView::shape () const
{
    return shapeOf (band, [this] (uint32_t id) { return host->plainValue (id); });
}

void ShapeView::idle ()
{
    const Meters* m = meters ? meters () : nullptr;
    if (!m)
        return;
    const float p = m->pos[band].load (std::memory_order_relaxed), v = m->value[band].load (std::memory_order_relaxed);
    if (std::fabs (p - shownPos) > 0.002f || std::fabs (v - shownValue) > 0.005f)
    {
        shownPos = p;
        shownValue = v;
        invalid ();
    }
}

void ShapeView::draw (CDrawContext* ctx)
{
    const CRect all = getViewSize ();
    ctx->setFillColor (theme::kWell);
    ctx->drawRect (all, kDrawFilled);
    ctx->setClipRect (all);
    ctx->setLineWidth (1.0);
    for (int i = 0; i <= 8; ++i)
    {
        ctx->setFrameColor (i % 4 == 0 ? theme::kGridMajor : theme::kGridMinor);
        ctx->drawLine (CPoint (xOf (i / 8.0), all.top), CPoint (xOf (i / 8.0), all.bottom));
    }
    for (double y : {-1.0, -0.5, 0.0, 0.5, 1.0})
    {
        ctx->setFrameColor (y == 0.0 ? theme::kGridZero : theme::kGridMinor);
        ctx->drawLine (CPoint (all.left, yOf (y)), CPoint (all.right, yOf (y)));
    }
    const Shape s = shape ();
    const bool envelope = std::lround (host->plainValue (kMode)) == kEnvelope;
    // the hold point (Envelope)
    if (envelope)
    {
        const double hx = xOf (s.holdX ());
        ctx->setLineStyle (theme::dashed ());
        ctx->setFrameColor (theme::kCopperPale);
        ctx->drawLine (CPoint (hx, all.top), CPoint (hx, all.bottom));
        ctx->setLineStyle (kLineSolid);
        ctx->setFont (theme::font (9.5, true));
        ctx->setFontColor (theme::kCopperPale);
        ctx->drawString ("hold", CRect (hx + 3, all.top + 2, hx + 60, all.top + 14), kLeftText, true);
    }
    // the shape, filled from the middle
    const CColor c = bandColor (band);
    if (auto path = owned (ctx->createGraphicsPath ()))
    {
        const int steps = 240;
        path->beginSubpath (CPoint (xOf (0.0), yOf (0.0)));
        for (int i = 0; i <= steps; ++i)
        {
            const double x = (double)i / steps;
            path->addLine (CPoint (xOf (x), yOf (s.valueAt (x))));
        }
        path->addLine (CPoint (xOf (1.0), yOf (0.0)));
        path->closeSubpath ();
        ctx->setFillColor (theme::withAlpha (theme::kCopper, 40));
        ctx->drawGraphicsPath (path, CDrawContext::kPathFilled);
    }
    if (auto line = owned (ctx->createGraphicsPath ()))
    {
        const int steps = 240;
        for (int i = 0; i <= steps; ++i)
        {
            const double x = (double)i / steps;
            const CPoint pt (xOf (x), yOf (s.valueAt (x)));
            if (i == 0)
                line->beginSubpath (pt);
            else
                line->addLine (pt);
        }
        ctx->setLineWidth (1.5);
        ctx->setFrameColor (theme::kText); // the shape: a text-coloured trace over a faint copper body
        ctx->drawGraphicsPath (line, CDrawContext::kPathStroked);
    }
    // where the band is now: a cinnabar playhead and dot
    const double px = xOf (shownPos);
    ctx->setLineWidth (1.0);
    ctx->setFrameColor (theme::withAlpha (theme::kEnergyLive, 150));
    ctx->drawLine (CPoint (px, all.top), CPoint (px, all.bottom));
    const CPoint now (px, yOf (shownValue));
    ctx->setFillColor (theme::kEnergyLive);
    ctx->drawEllipse (CRect (now.x - 3.5, now.y - 3.5, now.x + 3.5, now.y + 3.5), kDrawFilled);
    // the points (the hold point with a pale copper centre)
    for (int i = 0; i < s.n; ++i)
    {
        const CPoint pt (xOf (s.p[(size_t)i].x), yOf (s.p[(size_t)i].y));
        pk::draw::handle (ctx, pt, kPointRadius, false);
        if (envelope && i == s.hold)
        {
            ctx->setFillColor (c);
            ctx->drawEllipse (CRect (pt.x - 2.0, pt.y - 2.0, pt.x + 2.0, pt.y + 2.0), kDrawFilled);
        }
    }
    ctx->setFont (theme::font (10.5, true));
    ctx->setFontColor (c);
    char buf[32];
    std::snprintf (buf, sizeof (buf), "SHAPE  band %d", band + 1);
    ctx->drawString (buf, CRect (all.left + 6, all.top + 3, all.right - 6, all.top + 17), kLeftText, true);
    ctx->resetClipRect ();
}

int ShapeView::pointAt (const CPoint& p) const
{
    const Shape s = shape ();
    for (int i = 0; i < s.n; ++i)
        if (std::hypot (p.x - xOf (s.p[(size_t)i].x), p.y - yOf (s.p[(size_t)i].y)) <= kPointRadius + 4.0)
            return i;
    return -1;
}

int ShapeView::segmentAt (const CPoint& p) const
{
    const Shape s = shape ();
    for (int i = 0; i + 1 < s.n; ++i)
        if (p.x <= xOf (s.p[(size_t)i + 1].x))
            return i;
    return s.n - 2;
}

void ShapeView::setPlain (uint32_t id, double v) { host->setNorm (id, host->table ().toNormalized (id, v)); }

void ShapeView::insertPoint (double x, double y)
{
    Shape s = shape ();
    if (s.n >= kMaxPoints)
        return;
    int at = 1;
    while (at < s.n && s.p[(size_t)at].x < x)
        ++at;
    auto once = [this] (uint32_t id, double v) { host->setOnce (id, host->table ().toNormalized (id, v)); };
    for (int j = s.n - 1; j >= at; --j) // make room
        for (uint32_t f : {kPtX, kPtY, kPtCurve})
            once (pointParam (band, j + 1, f), host->plainValue (pointParam (band, j, f)));
    once (pointParam (band, at, kPtX), x);
    once (pointParam (band, at, kPtY), y);
    once (pointParam (band, at, kPtCurve), 0.0);
    once (bandParam (band, kPointCount), s.n + 1);
    if (s.hold >= at) // the hold point keeps its point
        once (bandParam (band, kHold), s.hold + 2);
}

void ShapeView::removePoint (int i)
{
    Shape s = shape ();
    if (i <= 0 || i >= s.n - 1 || s.n <= 2)
        return;
    auto once = [this] (uint32_t id, double v) { host->setOnce (id, host->table ().toNormalized (id, v)); };
    if (s.hold >= i) // first, so the envelope never sees it on the wrong point
        once (bandParam (band, kHold), std::max (1, s.hold)); // the hold moves with its point, or to the one before
    for (int j = i; j + 1 < s.n; ++j)
        for (uint32_t f : {kPtX, kPtY, kPtCurve})
            once (pointParam (band, j, f), host->plainValue (pointParam (band, j + 1, f)));
    once (bandParam (band, kPointCount), s.n - 1);
}

void ShapeView::onMouseDownEvent (MouseDownEvent& e)
{
    const bool right = e.buttonState.isRight ();
    if (!e.buttonState.isLeft () && !right)
        return;
    const CRect r = getViewSize ();
    const double x = std::clamp ((e.mousePosition.x - r.left - kPad) / (r.getWidth () - 2 * kPad), 0.0, 1.0);
    const double y = std::clamp ((r.getCenter ().y - e.mousePosition.y) / (r.getHeight () * 0.5 - kPad - 8.0), -1.0, 1.0);
    if (drag != Drag::None) // a drag the host never ended
    {
        MouseUpEvent up;
        onMouseUpEvent (up);
    }
    const int pt = pointAt (e.mousePosition);
    auto done = [&] {
        invalid ();
        e.consumed = true;
        e.ignoreFollowUpMoveAndUpEvents (true);
    };
    if (right)
    {
        // straighten the line under the mouse
        const uint32_t id = pointParam (band, segmentAt (e.mousePosition), kPtCurve);
        host->setOnce (id, host->table ().toNormalized (id, 0.0));
        return done ();
    }
    if (e.clickCount == 2)
    {
        if (pt >= 0)
            removePoint (pt);
        else
            insertPoint (x, y);
        return done ();
    }
    if (pt >= 0 && e.modifiers.has (ModifierKey::Alt))
    {
        const uint32_t id = bandParam (band, kHold);
        host->setOnce (id, host->table ().toNormalized (id, pt + 1));
        return done ();
    }
    down = e.mousePosition;
    if (pt >= 0)
    {
        drag = Drag::Point;
        dragIndex = pt;
        host->beginEdit (pointParam (band, pt, kPtX));
        host->beginEdit (pointParam (band, pt, kPtY));
    }
    else
    {
        drag = Drag::Bend;
        dragIndex = segmentAt (e.mousePosition);
        startCurve = host->plainValue (pointParam (band, dragIndex, kPtCurve));
        host->beginEdit (pointParam (band, dragIndex, kPtCurve));
    }
    e.consumed = true;
}

void ShapeView::onMouseMoveEvent (MouseMoveEvent& e)
{
    if (drag == Drag::None)
    {
        if (auto* f = getFrame ())
            f->setCursor (pointAt (e.mousePosition) >= 0 ? kCursorSizeAll : kCursorVSize);
        return;
    }
    const CRect r = getViewSize ();
    if (drag == Drag::Point)
    {
        const Shape s = shape ();
        const double x = std::clamp ((e.mousePosition.x - r.left - kPad) / (r.getWidth () - 2 * kPad), 0.0, 1.0);
        const double y = std::clamp ((r.getCenter ().y - e.mousePosition.y) / (r.getHeight () * 0.5 - kPad - 8.0), -1.0, 1.0);
        // the ends stay at the ends; the others between their neighbours
        if (dragIndex > 0 && dragIndex < s.n - 1)
            setPlain (pointParam (band, dragIndex, kPtX), std::clamp (x, s.p[(size_t)dragIndex - 1].x, s.p[(size_t)dragIndex + 1].x));
        setPlain (pointParam (band, dragIndex, kPtY), y);
    }
    else
    {
        // up bends the line up (early), down late
        const double dy = (down.y - e.mousePosition.y) / (r.getHeight () * 0.5);
        const Shape s = shape ();
        const bool rising = s.p[(size_t)dragIndex + 1].y >= s.p[(size_t)dragIndex].y;
        setPlain (pointParam (band, dragIndex, kPtCurve), std::clamp (startCurve + (rising ? dy : -dy), -1.0, 1.0));
    }
    invalid ();
    e.consumed = true;
}

void ShapeView::onMouseCancelEvent (MouseCancelEvent& e)
{
    MouseUpEvent up;
    onMouseUpEvent (up); // closes the edits of the drag
    e.consumed = true;
}

void ShapeView::onMouseUpEvent (MouseUpEvent& e)
{
    if (drag == Drag::Point)
    {
        host->endEdit (pointParam (band, dragIndex, kPtX));
        host->endEdit (pointParam (band, dragIndex, kPtY));
    }
    else if (drag == Drag::Bend)
        host->endEdit (pointParam (band, dragIndex, kPtCurve));
    drag = Drag::None;
    e.consumed = true;
}

} // namespace wubr
