#include "Views.h"

#include "pluginkit/ui/DropFiles.h"
#include "pluginkit/ui/Theme.h"

#include "smemplr/src/core/SampleData.h" // isSupportedAudioFile

#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cgraphicspath.h"
#include "vstgui/lib/dragging.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace detonatr {

using namespace VSTGUI;
namespace theme = pk::theme;

namespace {
void text (CDrawContext* ctx, const std::string& s, const CRect& r, const CColor& c, double size, CHoriTxtAlign a = kLeftText,
           bool bold = false)
{
    ctx->setFont (theme::font (size, bold));
    ctx->setFontColor (c);
    ctx->drawString (s.c_str (), r, a, true);
}

// each stage's colour (its light and the edge of its box)
CColor stageColor (int stage, uint8_t alpha = 255)
{
    static const CColor c[kNumStages] = {CColor (120, 200, 190), CColor (255, 164, 40), CColor (110, 165, 255),
                                         CColor (240, 110, 110), CColor (255, 205, 90)};
    CColor k = c[std::clamp (stage, 0, kNumStages - 1)];
    k.alpha = alpha;
    return k;
}
} // namespace

uint32_t stageOnParam (int stage)
{
    switch (stage)
    {
        case kStageClean: return kCleanOn;
        case kStageTone: return kToneOn;
        case kStageMultiband: return kMultibandOn;
        case kStageTransient: return kTransientOn;
        default: return kTailBase + pk::kTailOn;
    }
}

// --- StageStrip ------------------------------------------------------------------------

StageStrip::StageStrip (const CRect& r, pk::ParamHost* h) : CView (r), host (h) {}

Order StageStrip::order () const
{
    int chosen[kNumStages];
    for (int i = 0; i < kNumStages; ++i)
        chosen[i] = (int)std::lround (host->plainValue (kOrderBase + (uint32_t)i));
    return resolveOrder (chosen);
}

CRect StageStrip::boxRect (int pos) const
{
    const CRect all = getViewSize ();
    const double gap = 18.0, w = (all.getWidth () - gap * (kNumStages - 1)) / (double)kNumStages;
    const double x = all.left + pos * (w + gap);
    return CRect (x, all.top, x + w, all.bottom);
}

CRect StageStrip::lightRect (int pos) const
{
    const CRect b = boxRect (pos);
    return CRect (b.left + 8, b.top + 8, b.left + 22, b.top + 22);
}

int StageStrip::positionAt (double x) const
{
    for (int i = 0; i < kNumStages; ++i)
    {
        const CRect b = boxRect (i);
        if (x < b.right + 9.0)
            return i;
    }
    return kNumStages - 1;
}

void StageStrip::move (int from, int to)
{
    if (from < 0 || to < 0 || from >= kNumStages || to >= kNumStages || from == to)
        return;
    const Order o = order ();
    std::vector<int> v (o.stage, o.stage + kNumStages);
    const int s = v[(size_t)from];
    v.erase (v.begin () + from);
    v.insert (v.begin () + to, s);
    for (int i = 0; i < kNumStages; ++i)
    {
        const uint32_t id = kOrderBase + (uint32_t)i;
        host->setOnce (id, host->table ().toNormalized (id, v[(size_t)i]));
    }
}

void StageStrip::draw (CDrawContext* ctx)
{
    const Order o = order ();
    auto drawBox = [&] (int pos, const CRect& b, bool lifted) {
        const int s = o.stage[pos];
        const bool on = host->plainValue (stageOnParam (s)) >= 0.5;
        const bool sel = s == selected;
        ctx->setFillColor (lifted ? CColor (64, 64, 68) : (sel ? CColor (54, 54, 58) : theme::kPanel));
        ctx->setFrameColor (sel ? stageColor (s) : theme::kPanelEdge);
        ctx->setLineWidth (sel ? 2.0 : 1.0);
        auto path = owned (ctx->createRoundRectGraphicsPath (b, 5.0));
        if (path)
        {
            ctx->drawGraphicsPath (path, CDrawContext::kPathFilled);
            ctx->drawGraphicsPath (path, CDrawContext::kPathStroked);
        }
        // the light
        const CRect l (b.left + 8, b.top + 8, b.left + 22, b.top + 22);
        ctx->setFillColor (on ? stageColor (s) : CColor (70, 70, 70));
        ctx->drawEllipse (l, kDrawFilled);
        ctx->setFrameColor (CColor (0, 0, 0, 150));
        ctx->setLineWidth (1.0);
        ctx->drawEllipse (l, kDrawStroked);
        char num[8];
        std::snprintf (num, sizeof (num), "%d", pos + 1);
        text (ctx, num, CRect (b.right - 24, b.top + 6, b.right - 8, b.top + 22), theme::kTextDim, 10.0, kRightText);
        text (ctx, stageName (s), CRect (b.left + 8, b.top + 26, b.right - 8, b.bottom - 6), on ? theme::kTextBright : theme::kTextDim,
              13.0, kLeftText, true);
        text (ctx, on ? "" : "off", CRect (b.left + 28, b.top + 7, b.right - 28, b.top + 23), theme::kTextDim, 9.5);
    };
    // the arrows between the stages
    ctx->setFrameColor (theme::kTextDim);
    ctx->setLineWidth (1.5);
    for (int i = 0; i + 1 < kNumStages; ++i)
    {
        const CRect a = boxRect (i), b = boxRect (i + 1);
        const double y = (a.top + a.bottom) / 2, x0 = a.right + 4, x1 = b.left - 4;
        ctx->drawLine (CPoint (x0, y), CPoint (x1, y));
        ctx->drawLine (CPoint (x1 - 4, y - 4), CPoint (x1, y));
        ctx->drawLine (CPoint (x1 - 4, y + 4), CPoint (x1, y));
    }
    for (int i = 0; i < kNumStages; ++i)
        if (!(dragging && i == dragFrom))
            drawBox (i, boxRect (i), false);
    if (dragging && dragFrom >= 0)
    {
        // where it would land, and the stage under the mouse
        const CRect t = boxRect (dragTo);
        ctx->setFrameColor (stageColor (o.stage[dragFrom], 160));
        ctx->setLineWidth (2.0);
        ctx->drawLine (CPoint (dragTo > dragFrom ? t.right + 9 : t.left - 9, t.top), CPoint (dragTo > dragFrom ? t.right + 9 : t.left - 9, t.bottom));
        CRect b = boxRect (dragFrom);
        const double w = b.getWidth ();
        b.left = std::clamp (dragX - grabDx, getViewSize ().left, getViewSize ().right - w);
        b.right = b.left + w;
        drawBox (dragFrom, b, true);
    }
    setDirty (false);
}

void StageStrip::onMouseDownEvent (MouseDownEvent& e)
{
    if (!e.buttonState.isLeft ())
        return;
    const Order o = order ();
    for (int i = 0; i < kNumStages; ++i)
    {
        const CRect b = boxRect (i);
        if (!b.pointInside (e.mousePosition))
            continue;
        const int s = o.stage[i];
        CRect l = lightRect (i);
        l.extend (6, 6);
        if (l.pointInside (e.mousePosition))
        {
            const uint32_t id = stageOnParam (s);
            host->setOnce (id, host->plainValue (id) >= 0.5 ? 0.0 : 1.0);
            invalid ();
            e.consumed = true;
            e.ignoreFollowUpMoveAndUpEvents (true);
            return;
        }
        selected = s;
        if (onStagePicked)
            onStagePicked (s);
        dragFrom = dragTo = i;
        dragging = false;
        down = e.mousePosition;
        dragX = e.mousePosition.x;
        grabDx = e.mousePosition.x - b.left;
        invalid ();
        e.consumed = true;
        return;
    }
}

void StageStrip::onMouseMoveEvent (MouseMoveEvent& e)
{
    if (dragFrom < 0 || !e.buttonState.isLeft ())
        return;
    if (!dragging && std::fabs (e.mousePosition.x - down.x) < 4.0)
        return;
    dragging = true;
    dragX = e.mousePosition.x;
    dragTo = positionAt (dragX);
    invalid ();
    e.consumed = true;
}

void StageStrip::onMouseUpEvent (MouseUpEvent& e)
{
    if (dragFrom >= 0 && dragging)
        move (dragFrom, dragTo);
    dragFrom = dragTo = -1;
    dragging = false;
    invalid ();
    e.consumed = true;
}

void StageStrip::onMouseCancelEvent (MouseCancelEvent& e)
{
    dragFrom = dragTo = -1;
    dragging = false;
    invalid ();
    e.consumed = true;
}

// --- HitView ---------------------------------------------------------------------------

HitView::HitView (const CRect& r, MeterSource m) : CView (r), meters (std::move (m)) {}

void HitView::idle ()
{
    const Meters* m = meters ? meters () : nullptr;
    if (!m)
        return;
    const uint32_t w = m->scope.written ();
    if (w == lastWritten)
        return;
    lastWritten = w;
    const int n = std::clamp ((int)(seconds * m->sampleRate.load ()), 256, Meters::kScopeSize);
    in.resize ((size_t)n);
    out.resize ((size_t)n);
    const int got = m->scope.read (in.data (), out.data (), n);
    std::fill (in.begin (), in.end () - got, 0.0f);
    std::fill (out.begin (), out.end () - got, 0.0f);
    // the peak of each column
    const int cols = std::max (1, (int)getViewSize ().getWidth () - 2);
    colIn.assign ((size_t)cols, 0.0f);
    colOut.assign ((size_t)cols, 0.0f);
    for (int c = 0; c < cols; ++c)
    {
        const int a = (int)((int64_t)c * n / cols), b = std::max (a + 1, (int)((int64_t)(c + 1) * n / cols));
        float pi = 0.0f, po = 0.0f;
        for (int i = a; i < b && i < n; ++i)
        {
            pi = std::max (pi, std::fabs (in[(size_t)i]));
            po = std::max (po, std::fabs (out[(size_t)i]));
        }
        colIn[(size_t)c] = pi;
        colOut[(size_t)c] = po;
    }
    invalid ();
}

void HitView::draw (CDrawContext* ctx)
{
    const CRect all = getViewSize ();
    ctx->setFillColor (theme::kWaveBg);
    ctx->drawRect (all, kDrawFilled);
    ctx->setClipRect (all);
    constexpr double kFloor = -60.0;
    const double top = all.top + 20, bottom = all.bottom - 4;
    auto yOf = [&] (float v) {
        const double db = v > 1e-6f ? std::max (kFloor, 20.0 * std::log10 ((double)v)) : kFloor;
        return bottom - (bottom - top) * (std::min (db, 0.0) - kFloor) / -kFloor;
    };
    ctx->setLineWidth (1.0);
    for (double db : {-48.0, -36.0, -24.0, -12.0, 0.0})
    {
        const double y = bottom - (bottom - top) * (db - kFloor) / -kFloor;
        ctx->setFrameColor (db == 0.0 ? CColor (70, 50, 50) : theme::kGrid);
        ctx->drawLine (CPoint (all.left, y), CPoint (all.right, y));
        char buf[16];
        std::snprintf (buf, sizeof (buf), "%.0f", db);
        text (ctx, buf, CRect (all.right - 34, y - 12, all.right - 4, y), theme::kTextDim, 9.0, kRightText);
    }
    auto fill = [&] (const std::vector<float>& col, const CColor& fillC, const CColor& lineC) {
        if (col.empty ())
            return;
        auto path = owned (ctx->createGraphicsPath ());
        auto line = owned (ctx->createGraphicsPath ());
        if (!path || !line)
            return;
        path->beginSubpath (CPoint (all.left + 1, bottom));
        for (size_t c = 0; c < col.size (); ++c)
        {
            const CPoint p (all.left + 1 + (double)c, yOf (col[c]));
            path->addLine (p);
            if (c == 0)
                line->beginSubpath (p);
            else
                line->addLine (p);
        }
        path->addLine (CPoint (all.left + 1 + (double)col.size (), bottom));
        path->closeSubpath ();
        ctx->setFillColor (fillC);
        ctx->drawGraphicsPath (path, CDrawContext::kPathFilled);
        ctx->setFrameColor (lineC);
        ctx->setLineWidth (1.2);
        ctx->drawGraphicsPath (line, CDrawContext::kPathStroked);
    };
    fill (colIn, CColor (190, 196, 204, 50), CColor (190, 196, 204, 130));
    fill (colOut, CColor (255, 164, 40, 60), theme::kAccent);
    char title[64];
    std::snprintf (title, sizeof (title), seconds < 1.0 ? "%.0f ms" : "%.0f s", seconds < 1.0 ? seconds * 1000.0 : seconds);
    text (ctx, "HIT", CRect (all.left + 6, all.top + 3, all.left + 60, all.top + 17), theme::kTextBright, 10.5, kLeftText, true);
    text (ctx, "in", CRect (all.left + 40, all.top + 3, all.left + 60, all.top + 17), CColor (190, 196, 204), 9.5);
    text (ctx, "out", CRect (all.left + 60, all.top + 3, all.left + 90, all.top + 17), theme::kAccent, 9.5);
    text (ctx, title, CRect (all.right - 90, all.top + 3, all.right - 6, all.top + 17), theme::kTextDim, 9.5, kRightText);
    ctx->resetClipRect ();
    setDirty (false);
}

void HitView::onMouseDownEvent (MouseDownEvent& e)
{
    if (!e.buttonState.isLeft ())
        return;
    static const double steps[4] = {0.25, 0.5, 1.0, 2.0};
    int k = 0;
    while (k < 4 && steps[k] <= seconds + 1e-9)
        ++k;
    seconds = steps[k % 4];
    lastWritten = 0;
    idle ();
    invalid ();
    e.consumed = true;
    e.ignoreFollowUpMoveAndUpEvents (true);
}

// --- RecordingSlot -----------------------------------------------------------------------

namespace {
class SlotDropTarget : public DropTargetAdapter, public NonAtomicReferenceCounted
{
public:
    explicit SlotDropTarget (RecordingSlot* v) : view (v) {}
    DragOperation onDragEnter (DragEventData d) override { return hover (acceptable (d)); }
    DragOperation onDragMove (DragEventData d) override { return acceptable (d) ? DragOperation::Copy : DragOperation::None; }
    void onDragLeave (DragEventData) override { hover (false); }
    bool onDrop (DragEventData d) override
    {
        hover (false);
        std::string path;
        if (!firstPath (d, path) || !view->onFileDropped)
            return false;
        view->onFileDropped (path);
        return true;
    }

private:
    DragOperation hover (bool on)
    {
        view->dropHover = on;
        view->invalid ();
        return on ? DragOperation::Copy : DragOperation::None;
    }
    static bool firstPath (DragEventData d, std::string& out)
    {
        for (const auto& p : pk::droppedPaths (d.drag))
            if (smemplr::isSupportedAudioFile (p))
            {
                out = p;
                return true;
            }
        return false;
    }
    static bool acceptable (DragEventData d)
    {
        std::string p;
        return firstPath (d, p);
    }
    RecordingSlot* view;
};
} // namespace

RecordingSlot::RecordingSlot (const CRect& r, int s) : CView (r), slot (s) { dropTarget = makeOwned<SlotDropTarget> (this); }

SharedPointer<IDropTarget> RecordingSlot::getDropTarget () { return dropTarget; }

void RecordingSlot::setRecording (std::shared_ptr<const Carrier> c, const std::string& n)
{
    if (c == audio && n == name)
        return;
    audio = std::move (c);
    name = n;
    error.clear ();
    peaks.clear ();
    if (audio && audio->frames > 0)
    {
        const int cols = std::max (1, (int)getViewSize ().getWidth () - 8);
        peaks.assign ((size_t)cols, 0.0f);
        for (int c2 = 0; c2 < cols; ++c2)
        {
            const int a = (int)((int64_t)c2 * audio->frames / cols), b = std::max (a + 1, (int)((int64_t)(c2 + 1) * audio->frames / cols));
            float pk = 0.0f;
            for (int i = a; i < b && i < audio->frames; ++i)
                pk = std::max ({pk, std::fabs (audio->ch[0][(size_t)i]), std::fabs (audio->ch[1][(size_t)i])});
            peaks[(size_t)c2] = pk;
        }
        const float top = std::max (1e-6f, *std::max_element (peaks.begin (), peaks.end ()));
        for (auto& p : peaks)
            p /= top;
    }
    invalid ();
}

void RecordingSlot::setError (const std::string& e)
{
    error = e;
    invalid ();
}

void RecordingSlot::draw (CDrawContext* ctx)
{
    const CRect all = getViewSize ();
    ctx->setFillColor (dropHover ? CColor (60, 50, 34) : theme::kWaveBg);
    ctx->setFrameColor (dropHover ? theme::kAccent : theme::kPanelEdge);
    ctx->setLineWidth (1.0);
    ctx->drawRect (all, kDrawFilledAndStroked);
    char head[32];
    std::snprintf (head, sizeof (head), "%d", slot + 1);
    text (ctx, head, CRect (all.left + 5, all.top + 3, all.left + 20, all.top + 16), theme::kTextDim, 9.5, kLeftText, true);
    if (!peaks.empty ())
    {
        const double mid = (all.top + all.bottom) / 2 + 6, half = (all.getHeight () - 30) / 2;
        ctx->setFrameColor (CColor (255, 164, 40, 170));
        for (size_t c = 0; c < peaks.size (); ++c)
        {
            const double x = all.left + 4 + (double)c, h = std::max (0.5, half * peaks[c]);
            ctx->drawLine (CPoint (x, mid - h), CPoint (x, mid + h));
        }
        text (ctx, name, CRect (all.left + 18, all.top + 3, all.right - 4, all.top + 16), theme::kText, 9.5);
    }
    else
        text (ctx, error.empty () ? "drop a recording" : error, CRect (all.left + 6, all.top + 18, all.right - 6, all.bottom - 4),
              error.empty () ? theme::kTextDim : CColor (240, 110, 110), 9.5, kCenterText);
    setDirty (false);
}

void RecordingSlot::onMouseDownEvent (MouseDownEvent& e)
{
    if (!e.buttonState.isLeft ())
        return;
    if (onClick)
        onClick ();
    e.consumed = true;
    e.ignoreFollowUpMoveAndUpEvents (true);
}

// --- TransientCurve ----------------------------------------------------------------------

TransientCurve::TransientCurve (const CRect& r, pk::ParamHost* h) : CView (r), host (h) {}

void TransientCurve::draw (CDrawContext* ctx)
{
    const CRect all = getViewSize ();
    ctx->setFillColor (theme::kWaveBg);
    ctx->drawRect (all, kDrawFilled);
    ctx->setClipRect (all);
    const double spike = host->plainValue (kSpike), fall = host->plainValue (kFall), drop = host->plainValue (kDrop);
    const bool on = host->plainValue (kTransientOn) >= 0.5;
    constexpr double kSpanMs = 80.0, kFloor = -48.0;
    const double top = all.top + 20, bottom = all.bottom - 16, left = all.left + 30, right = all.right - 8;
    auto xOf = [&] (double ms) { return left + (right - left) * std::clamp (ms, 0.0, kSpanMs) / kSpanMs; };
    auto yOf = [&] (double db) { return top + (bottom - top) * std::clamp (db, kFloor, 0.0) / kFloor; };
    ctx->setLineWidth (1.0);
    for (double db : {0.0, -12.0, -24.0, -36.0, -48.0})
    {
        ctx->setFrameColor (theme::kGrid);
        ctx->drawLine (CPoint (left, yOf (db)), CPoint (right, yOf (db)));
        char buf[12];
        std::snprintf (buf, sizeof (buf), "%.0f", db);
        text (ctx, buf, CRect (all.left + 2, yOf (db) - 7, left - 4, yOf (db) + 7), theme::kTextDim, 9.0, kRightText);
    }
    for (double ms : {0.0, 20.0, 40.0, 60.0, 80.0})
    {
        char buf[12];
        std::snprintf (buf, sizeof (buf), "%.0f ms", ms);
        text (ctx, buf, CRect (xOf (ms) - 24, bottom + 2, xOf (ms) + 24, bottom + 14), theme::kTextDim, 9.0, kCenterText);
    }
    // the gain: full for the spike, a smooth fall to the drop, then held
    auto path = owned (ctx->createGraphicsPath ());
    if (path)
    {
        const double d = on ? -drop : 0.0;
        path->beginSubpath (CPoint (xOf (0.0), yOf (0.0)));
        path->addLine (CPoint (xOf (spike), yOf (0.0)));
        const int steps = 24;
        for (int i = 1; i <= steps; ++i)
        {
            const double t = (double)i / steps, s = 0.5 - 0.5 * std::cos (t * 3.14159265358979);
            path->addLine (CPoint (xOf (spike + fall * t), yOf (d * s)));
        }
        path->addLine (CPoint (xOf (kSpanMs), yOf (d)));
        ctx->setFrameColor (on ? CColor (240, 110, 110) : theme::kTextDim);
        ctx->setLineWidth (2.0);
        ctx->drawGraphicsPath (path, CDrawContext::kPathStroked);
    }
    text (ctx, "LEVEL AFTER A HIT", CRect (all.left + 6, all.top + 3, all.right - 6, all.top + 17), theme::kTextBright, 10.0, kLeftText, true);
    ctx->resetClipRect ();
    setDirty (false);
}

} // namespace detonatr
