#include "ShapeView.h"

#include "pluginkit/ui/Theme.h"

#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cframe.h"
#include "vstgui/lib/cgraphicspath.h"
#include "vstgui/lib/events.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace dropr {

using namespace VSTGUI;
namespace theme = pk::theme;

namespace {
constexpr double kPointRadius = 5.0;
const CColor kShape (90, 150, 255); // the drop: blue, a cut, as everywhere in the suite
const CColor kHit (255, 230, 120);  // the hit flash

CColor withAlpha (CColor c, uint8_t a)
{
    c.alpha = a;
    return c;
}

void text (CDrawContext* ctx, const std::string& s, const CRect& r, const CColor& c, double size,
           CHoriTxtAlign a = kCenterText, bool bold = false)
{
    ctx->setFont (theme::font (size, bold));
    ctx->setFontColor (c);
    ctx->drawString (s.c_str (), r, a, true);
}

// a level 0 .. 1 in dB, -Depth .. 0
double dbOf (double y, double depth) { return (y - 1.0) * depth; }
bool whole (double v) { return std::fabs (v - std::round (v)) < 0.05; }
} // namespace

ShapeView::ShapeView (const CRect& r, pk::ParamHost* h, MeterSource m) : CView (r), host (h), meters (std::move (m)) {}

CRect ShapeView::plot () const
{
    const CRect r = getViewSize ();
    return CRect (r.left + kAxisLeft, r.top + kTop, r.right - kPad, r.bottom - kAxisBottom - 6.0);
}

double ShapeView::xOf (double x) const
{
    const CRect p = plot ();
    return p.left + x * p.getWidth ();
}

double ShapeView::yOf (double y) const
{
    const CRect p = plot ();
    return p.bottom - y * p.getHeight ();
}

double ShapeView::timeAt (const CPoint& pt) const
{
    const CRect p = plot ();
    return std::clamp ((pt.x - p.left) / p.getWidth (), 0.0, 1.0);
}

double ShapeView::levelAt (const CPoint& pt) const
{
    const CRect p = plot ();
    return std::clamp ((p.bottom - pt.y) / p.getHeight (), 0.0, 1.0);
}

Shape ShapeView::shape () const
{
    return shapeOf ([this] (uint32_t id) { return host->plainValue (id); });
}

void ShapeView::idle ()
{
    const Meters* m = meters ? meters () : nullptr;
    if (!m)
        return;
    const float p = m->position.load (std::memory_order_relaxed), g = m->gainDb.load (std::memory_order_relaxed);
    const int h = m->hits.load (std::memory_order_relaxed);
    bool dirty = false;
    if (lastHits >= 0 && h != lastHits)
    {
        flash = 1.0f;
        dirty = true;
    }
    lastHits = h;
    if (flash > 0.0f)
    {
        flash = flash < 0.05f ? 0.0f : flash * 0.75f;
        dirty = true;
    }
    if (std::fabs (p - shownPos) > 0.002f || std::fabs (g - shownGain) > 0.05f)
    {
        shownPos = p;
        shownGain = g;
        dirty = true;
    }
    if (dirty)
        invalid ();
}

void ShapeView::draw (CDrawContext* ctx)
{
    const CRect all = getViewSize ();
    const CRect pr = plot ();
    ctx->setFillColor (theme::kWaveBg);
    ctx->drawRect (all, kDrawFilled);
    ctx->setClipRect (all);
    ctx->setLineWidth (1.0);
    const double depth = host->plainValue (kDepth), length = host->plainValue (kLength);

    // the hit flash: the left edge (where every hit starts the shape) lights up
    if (flash > 0.0f)
    {
        ctx->setFillColor (withAlpha (kHit, (uint8_t)(70 * flash)));
        ctx->drawRect (CRect (pr.left, pr.top, pr.left + 18, pr.bottom), kDrawFilled);
        ctx->setFrameColor (withAlpha (kHit, (uint8_t)(255 * flash)));
        ctx->setLineWidth (2.0);
        ctx->drawLine (CPoint (pr.left, pr.top), CPoint (pr.left, pr.bottom));
        ctx->setLineWidth (1.0);
    }

    // grid: time in ms across (from Length), level in dB up (from Depth)
    char buf[96];
    for (int i = 0; i <= 8; ++i)
    {
        const double x = xOf (i / 8.0);
        ctx->setFrameColor (i % 4 == 0 ? CColor (58, 58, 64) : theme::kGrid);
        ctx->drawLine (CPoint (x, pr.top), CPoint (x, pr.bottom));
        if (i % 2 == 0)
        {
            const double ms = length * i / 8.0;
            std::snprintf (buf, sizeof (buf), whole (ms) || ms >= 1000.0 ? "%.0f ms" : "%.1f ms", ms);
            const CHoriTxtAlign a = i == 0 ? kLeftText : i == 8 ? kRightText : kCenterText;
            const CRect tr = i == 0 ? CRect (x, pr.bottom + 3, x + 60, pr.bottom + 3 + kAxisBottom)
                             : i == 8 ? CRect (x - 60, pr.bottom + 3, x, pr.bottom + 3 + kAxisBottom)
                                      : CRect (x - 30, pr.bottom + 3, x + 30, pr.bottom + 3 + kAxisBottom);
            text (ctx, buf, tr, theme::kTextDim, 9.5, a);
        }
    }
    for (int i = 0; i <= 4; ++i)
    {
        const double y = i / 4.0;
        ctx->setFrameColor (i == 4 || i == 0 ? CColor (58, 58, 64) : theme::kGrid);
        ctx->drawLine (CPoint (pr.left, yOf (y)), CPoint (pr.right, yOf (y)));
        const double db = dbOf (y, depth);
        std::snprintf (buf, sizeof (buf), std::fabs (db) < 0.05 ? "0 dB" : whole (db) ? "%.0f" : "%.1f", db);
        text (ctx, buf, CRect (all.left + 2, yOf (y) - 7, pr.left - 5, yOf (y) + 7), theme::kTextDim, 9.5, kRightText);
    }

    // the shape, filled down from the top (what it takes away)
    const Shape s = shape ();
    const int steps = std::max (2, (int)pr.getWidth () / 2);
    if (auto path = owned (ctx->createGraphicsPath ()))
    {
        path->beginSubpath (CPoint (xOf (0.0), yOf (1.0)));
        for (int i = 0; i <= steps; ++i)
        {
            const double x = (double)i / steps;
            path->addLine (CPoint (xOf (x), yOf (s.valueAt (x))));
        }
        path->addLine (CPoint (xOf (1.0), yOf (1.0)));
        path->closeSubpath ();
        ctx->setFillColor (withAlpha (kShape, 40));
        ctx->drawGraphicsPath (path, CDrawContext::kPathFilled);
    }
    if (auto line = owned (ctx->createGraphicsPath ()))
    {
        for (int i = 0; i <= steps; ++i)
        {
            const double x = (double)i / steps;
            const CPoint pt (xOf (x), yOf (s.valueAt (x)));
            if (i == 0)
                line->beginSubpath (pt);
            else
                line->addLine (pt);
        }
        ctx->setLineWidth (2.0);
        ctx->setFrameColor (kShape);
        ctx->drawGraphicsPath (line, CDrawContext::kPathStroked);
        ctx->setLineWidth (1.0);
    }

    // where the shape is now (only while it runs)
    const bool running = shownPos < 0.999f;
    if (running)
    {
        const double px = xOf (shownPos);
        ctx->setFrameColor (CColor (255, 255, 255, 80));
        ctx->drawLine (CPoint (px, pr.top), CPoint (px, pr.bottom));
        const CPoint now (px, yOf (s.valueAt (shownPos)));
        ctx->setFillColor (theme::kPlayhead);
        ctx->drawEllipse (CRect (now.x - 3.5, now.y - 3.5, now.x + 3.5, now.y + 3.5), kDrawFilled);
    }

    // the points
    for (int i = 0; i < s.n; ++i)
    {
        const CPoint pt (xOf (s.p[(size_t)i].x), yOf (s.p[(size_t)i].y));
        const CRect r (pt.x - kPointRadius, pt.y - kPointRadius, pt.x + kPointRadius, pt.y + kPointRadius);
        ctx->setFillColor (drag == Drag::Point && dragIndex == i ? kShape : theme::kTextBright);
        ctx->drawEllipse (r, kDrawFilled);
        ctx->setLineWidth (1.5);
        ctx->setFrameColor (kShape);
        ctx->drawEllipse (r, kDrawStroked);
    }
    ctx->setLineWidth (1.0);

    // title and readouts
    text (ctx, "SHAPE", CRect (all.left + 6, all.top + 4, all.left + 80, all.top + 18), kShape, 10.5, kLeftText, true);
    std::snprintf (buf, sizeof (buf), "Depth %.1f dB   Length %.0f ms   Pre %.1f ms", depth, length, host->plainValue (kPre));
    text (ctx, buf, CRect (all.left + 70, all.top + 5, all.left + 420, all.top + 18), theme::kTextDim, 9.5, kLeftText);
    std::snprintf (buf, sizeof (buf), "Gain %.1f dB", (double)shownGain);
    text (ctx, buf, CRect (all.right - 200, all.top + 4, all.right - 60, all.top + 18),
          running || shownGain < -0.05f ? theme::kTextBright : theme::kText, 11.0, kRightText, true);
    text (ctx, "HIT", CRect (all.right - 52, all.top + 4, all.right - kPad, all.top + 18),
          flash > 0.0f ? withAlpha (kHit, (uint8_t)(90 + 165 * flash)) : withAlpha (theme::kTextDim, 120), 10.5, kRightText, true);
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
    while (at < s.n - 1 && s.p[(size_t)at].x < x)
        ++at;
    auto once = [this] (uint32_t id, double v) { host->setOnce (id, host->table ().toNormalized (id, v)); };
    for (int j = s.n - 1; j >= at; --j) // make room
        for (uint32_t f : {kPtX, kPtY, kPtCurve})
            once (pointParam (j + 1, f), host->plainValue (pointParam (j, f)));
    once (pointParam (at, kPtX), x);
    once (pointParam (at, kPtY), y);
    once (pointParam (at, kPtCurve), 0.0);
    once (kPointCount, s.n + 1);
}

void ShapeView::removePoint (int i)
{
    Shape s = shape ();
    if (i <= 0 || i >= s.n - 1 || s.n <= 2)
        return;
    auto once = [this] (uint32_t id, double v) { host->setOnce (id, host->table ().toNormalized (id, v)); };
    for (int j = i; j + 1 < s.n; ++j)
        for (uint32_t f : {kPtX, kPtY, kPtCurve})
            once (pointParam (j, f), host->plainValue (pointParam (j + 1, f)));
    once (kPointCount, s.n - 1);
}

void ShapeView::onMouseDownEvent (MouseDownEvent& e)
{
    const bool right = e.buttonState.isRight ();
    if (!e.buttonState.isLeft () && !right)
        return;
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
        const uint32_t id = pointParam (segmentAt (e.mousePosition), kPtCurve);
        host->setOnce (id, host->table ().toNormalized (id, 0.0));
        return done ();
    }
    if (e.clickCount == 2)
    {
        if (pt >= 0)
            removePoint (pt);
        else
            insertPoint (timeAt (e.mousePosition), levelAt (e.mousePosition));
        return done ();
    }
    down = e.mousePosition;
    if (pt >= 0)
    {
        drag = Drag::Point;
        dragIndex = pt;
        host->beginEdit (pointParam (pt, kPtX));
        host->beginEdit (pointParam (pt, kPtY));
    }
    else
    {
        drag = Drag::Bend;
        dragIndex = segmentAt (e.mousePosition);
        startCurve = host->plainValue (pointParam (dragIndex, kPtCurve));
        host->beginEdit (pointParam (dragIndex, kPtCurve));
    }
    invalid ();
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
    const Shape s = shape ();
    if (drag == Drag::Point)
    {
        // the ends stay at the ends; the others between their neighbours
        if (dragIndex > 0 && dragIndex < s.n - 1)
            setPlain (pointParam (dragIndex, kPtX),
                      std::clamp (timeAt (e.mousePosition), s.p[(size_t)dragIndex - 1].x, s.p[(size_t)dragIndex + 1].x));
        setPlain (pointParam (dragIndex, kPtY), levelAt (e.mousePosition));
    }
    else
    {
        // up bends the line up, down bends it down
        const double dy = (down.y - e.mousePosition.y) / plot ().getHeight ();
        const bool rising = s.p[(size_t)dragIndex + 1].y >= s.p[(size_t)dragIndex].y;
        setPlain (pointParam (dragIndex, kPtCurve), std::clamp (startCurve + (rising ? dy : -dy), -1.0, 1.0));
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
        host->endEdit (pointParam (dragIndex, kPtX));
        host->endEdit (pointParam (dragIndex, kPtY));
    }
    else if (drag == Drag::Bend)
        host->endEdit (pointParam (dragIndex, kPtCurve));
    drag = Drag::None;
    invalid ();
    e.consumed = true;
}

void ShapeView::onMouseExitEvent (MouseExitEvent& e)
{
    if (auto* f = getFrame ())
        f->setCursor (kCursorDefault);
    e.consumed = true;
}

} // namespace dropr
