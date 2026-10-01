#include "Views.h"

#include "pluginkit/ui/Theme.h"

#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cgraphicspath.h"
#include "vstgui/lib/events.h"

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

// each stage's colour (its light and the edge of its box); the tail's last
CColor stageColor (int stage, uint8_t alpha = 255)
{
    static const CColor c[kNumStages + 1] = {CColor (255, 164, 40),  CColor (120, 200, 190), CColor (150, 130, 255), CColor (240, 110, 110),
                                             CColor (110, 165, 255), CColor (240, 110, 110), CColor (120, 210, 120), CColor (120, 210, 120),
                                             CColor (255, 205, 90),  CColor (110, 165, 255), CColor (200, 200, 200)};
    CColor k = c[std::clamp (stage, 0, (int)kNumStages)];
    k.alpha = alpha;
    return k;
}

void background (CDrawContext* ctx, const CRect& all)
{
    ctx->setFillColor (theme::kWaveBg);
    ctx->drawRect (all, kDrawFilled);
}

double logX (double hz, double lo, double hi, double left, double right)
{
    return left + (right - left) * std::log (std::max (hz, lo) / lo) / std::log (hi / lo);
}

void freqAxis (CDrawContext* ctx, double left, double right, double y, double lo, double hi)
{
    for (double f : {50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0})
    {
        if (f < lo || f > hi)
            continue;
        const double x = logX (f, lo, hi, left, right);
        char buf[16];
        if (f >= 1000.0)
            std::snprintf (buf, sizeof (buf), "%.0fk", f / 1000.0);
        else
            std::snprintf (buf, sizeof (buf), "%.0f", f);
        text (ctx, buf, CRect (x - 20, y, x + 20, y + 12), theme::kTextDim, 8.5, kCenterText);
    }
}
} // namespace

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
    const double gap = 8.0, w = (all.getWidth () - gap * (int)kNumStages) / (double)((int)kNumStages + 1);
    const double x = all.left + pos * (w + gap);
    return CRect (x, all.top, x + w, all.bottom);
}

CRect StageStrip::lightRect (int pos) const
{
    const CRect b = boxRect (pos);
    return CRect (b.left + 6, b.top + 6, b.left + 18, b.top + 18);
}

int StageStrip::positionAt (double x) const
{
    for (int i = 0; i < kNumStages; ++i)
        if (x < boxRect (i).right + 4.0)
            return i;
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
    // page: a stage, or kTailBox
    auto drawBox = [&] (int page, int pos, const CRect& b, bool lifted) {
        const uint32_t onId = page == kTailBox ? kTailBase + pk::kTailOn : stageOnParam (page);
        const bool on = host->plainValue (onId) >= 0.5;
        const bool sel = page == selected;
        ctx->setFillColor (lifted ? CColor (64, 64, 68) : (sel ? CColor (54, 54, 58) : theme::kPanel));
        ctx->setFrameColor (sel ? stageColor (page) : theme::kPanelEdge);
        ctx->setLineWidth (sel ? 2.0 : 1.0);
        auto path = owned (ctx->createRoundRectGraphicsPath (b, 4.0));
        if (path)
        {
            ctx->drawGraphicsPath (path, CDrawContext::kPathFilled);
            ctx->drawGraphicsPath (path, CDrawContext::kPathStroked);
        }
        const CRect l (b.left + 6, b.top + 6, b.left + 18, b.top + 18);
        ctx->setFillColor (on ? stageColor (page) : CColor (70, 70, 70));
        ctx->drawEllipse (l, kDrawFilled);
        ctx->setFrameColor (CColor (0, 0, 0, 150));
        ctx->setLineWidth (1.0);
        ctx->drawEllipse (l, kDrawStroked);
        if (page != kTailBox)
        {
            char num[16];
            std::snprintf (num, sizeof (num), "%d", pos + 1);
            text (ctx, num, CRect (b.right - 20, b.top + 4, b.right - 5, b.top + 18), theme::kTextDim, 9.5, kRightText);
        }
        const char* name = page == kTailBox ? "Smacheratr" : stageShortName (page);
        text (ctx, name, CRect (b.left + 4, b.top + 24, b.right - 2, b.bottom - 14), on ? theme::kTextBright : theme::kTextDim,
              page == kTailBox ? 9.0 : 10.0, kLeftText, true);
        text (ctx, on ? "" : "off", CRect (b.left + 4, b.bottom - 16, b.right - 4, b.bottom - 3), theme::kTextDim, 9.0);
    };
    // the arrows between the boxes
    ctx->setFrameColor (theme::kTextDim);
    ctx->setLineWidth (1.2);
    for (int i = 0; i < kNumStages; ++i)
    {
        const CRect a = boxRect (i), b = boxRect (i + 1);
        const double y = (a.top + a.bottom) / 2, x0 = a.right + 1, x1 = b.left - 1;
        ctx->drawLine (CPoint (x0, y), CPoint (x1, y));
        ctx->drawLine (CPoint (x1 - 3, y - 3), CPoint (x1, y));
        ctx->drawLine (CPoint (x1 - 3, y + 3), CPoint (x1, y));
    }
    for (int i = 0; i < kNumStages; ++i)
        if (!(dragging && i == dragFrom))
            drawBox (o.stage[i], i, boxRect (i), false);
    drawBox (kTailBox, kTailBox, boxRect (kTailBox), false);
    if (dragging && dragFrom >= 0)
    {
        // where it would land, and the stage under the mouse
        const CRect t = boxRect (dragTo);
        ctx->setFrameColor (stageColor (o.stage[dragFrom], 160));
        ctx->setLineWidth (2.0);
        const double lx = dragTo > dragFrom ? t.right + 4 : t.left - 4;
        ctx->drawLine (CPoint (lx, t.top), CPoint (lx, t.bottom));
        CRect b = boxRect (dragFrom);
        const double w = b.getWidth ();
        b.left = std::clamp (dragX - grabDx, getViewSize ().left, boxRect (kNumStages - 1).right - w);
        b.right = b.left + w;
        drawBox (o.stage[dragFrom], dragFrom, b, true);
    }
    setDirty (false);
}

void StageStrip::onMouseDownEvent (MouseDownEvent& e)
{
    if (!e.buttonState.isLeft ())
        return;
    const Order o = order ();
    for (int i = 0; i <= kTailBox; ++i)
    {
        const CRect b = boxRect (i);
        if (!b.pointInside (e.mousePosition))
            continue;
        const int page = i == kTailBox ? kTailBox : o.stage[i];
        CRect l = lightRect (i);
        l.extend (6, 6);
        if (l.pointInside (e.mousePosition))
        {
            const uint32_t id = page == kTailBox ? kTailBase + pk::kTailOn : stageOnParam (page);
            host->setOnce (id, host->plainValue (id) >= 0.5 ? 0.0 : 1.0);
            invalid ();
            e.consumed = true;
            e.ignoreFollowUpMoveAndUpEvents (true);
            return;
        }
        selected = page;
        if (onPagePicked)
            onPagePicked (page);
        if (i != kTailBox) // the Smacheratr stays at the end
        {
            dragFrom = dragTo = i;
            dragging = false;
            down = e.mousePosition;
            dragX = e.mousePosition.x;
            grabDx = e.mousePosition.x - b.left;
        }
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
    background (ctx, all);
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

// --- VocoderView -------------------------------------------------------------------------

VocoderView::VocoderView (const CRect& r, pk::ParamHost* h, MeterSource m) : CView (r), host (h), meters (std::move (m)) {}

void VocoderView::idle ()
{
    const Meters* m = meters ? meters () : nullptr;
    if (!m)
        return;
    const int n = std::clamp (m->vocBands.load (), 0, Vocoder::kMaxBands);
    level.resize ((size_t)n);
    freq.resize ((size_t)n);
    for (int b = 0; b < n; ++b)
    {
        level[(size_t)b] = m->vocLevelDb[(size_t)b].load (std::memory_order_relaxed);
        freq[(size_t)b] = m->vocFreq[(size_t)b].load (std::memory_order_relaxed);
    }
    invalid ();
}

void VocoderView::draw (CDrawContext* ctx)
{
    const CRect all = getViewSize ();
    background (ctx, all);
    ctx->setClipRect (all);
    const double left = all.left + 8, right = all.right - 8, top = all.top + 22, bottom = all.bottom - 18;
    constexpr double kLo = 20.0, kHi = 20000.0, kFloor = -72.0;
    for (double db : {-60.0, -48.0, -36.0, -24.0, -12.0})
    {
        const double y = top + (bottom - top) * db / kFloor;
        ctx->setFrameColor (theme::kGrid);
        ctx->drawLine (CPoint (left, y), CPoint (right, y));
    }
    // the range
    const double lo = host->plainValue (kVocLow), hi = host->plainValue (kVocHigh);
    ctx->setFillColor (CColor (255, 164, 40, 18));
    ctx->drawRect (CRect (logX (lo, kLo, kHi, left, right), top, logX (hi, kLo, kHi, left, right), bottom), kDrawFilled);
    const bool on = host->plainValue (kVocOn) >= 0.5;
    const size_t n = level.size ();
    for (size_t b = 0; b < n; ++b)
    {
        const double x = logX (freq[b], kLo, kHi, left, right);
        const double w = std::max (1.5, (right - left) / (double)std::max<size_t> (n, 1) * 0.55);
        const double db = std::clamp ((double)level[b], kFloor, 0.0);
        const double y = top + (bottom - top) * db / kFloor;
        ctx->setFillColor (on ? CColor (255, 164, 40, 200) : CColor (130, 130, 130, 160));
        ctx->drawRect (CRect (x - w / 2, y, x + w / 2, bottom), kDrawFilled);
    }
    freqAxis (ctx, left, right, bottom + 3, kLo, kHi);
    char buf[64];
    std::snprintf (buf, sizeof (buf), "BANDS  %d, each times its own level", (int)std::lround (host->plainValue (kVocBands)));
    text (ctx, buf, CRect (all.left + 6, all.top + 3, all.right - 6, all.top + 17), theme::kTextBright, 10.0, kLeftText, true);
    ctx->resetClipRect ();
    setDirty (false);
}

// --- SpikeView ---------------------------------------------------------------------------

SpikeView::SpikeView (const CRect& r, pk::ParamHost* h, MeterSource m) : CView (r), host (h), meters (std::move (m)) {}

void SpikeView::idle ()
{
    const Meters* m = meters ? meters () : nullptr;
    if (!m)
        return;
    gain.resize (Spike::kBands);
    held.resize (Spike::kBands);
    freq.resize (Spike::kBands);
    for (int b = 0; b < Spike::kBands; ++b)
    {
        const float g = m->spikeGainDb[(size_t)b].load (std::memory_order_relaxed);
        gain[(size_t)b] = g;
        float& h = held[(size_t)b];
        h = std::fabs (g) > std::fabs (h) ? g : h * 0.92f; // peaks, falling back
        freq[(size_t)b] = m->spikeFreq[(size_t)b].load (std::memory_order_relaxed);
    }
    invalid ();
}

void SpikeView::draw (CDrawContext* ctx)
{
    const CRect all = getViewSize ();
    background (ctx, all);
    ctx->setClipRect (all);
    const double left = all.left + 30, right = all.right - 8, top = all.top + 22, bottom = all.bottom - 18;
    constexpr double kLo = 20.0, kHi = 20000.0, kRange = 18.0;
    const double mid = (top + bottom) / 2;
    auto yOf = [&] (double db) { return mid - (bottom - top) / 2 * std::clamp (db, -kRange, kRange) / kRange; };
    for (double db : {-18.0, -12.0, -6.0, 0.0, 6.0, 12.0, 18.0})
    {
        ctx->setFrameColor (db == 0.0 ? theme::kPanelEdge : theme::kGrid);
        ctx->drawLine (CPoint (left, yOf (db)), CPoint (right, yOf (db)));
        char buf[12];
        std::snprintf (buf, sizeof (buf), "%+.0f", db);
        text (ctx, buf, CRect (all.left + 2, yOf (db) - 6, left - 4, yOf (db) + 6), theme::kTextDim, 8.5, kRightText);
    }
    const bool on = host->plainValue (kSpkOn) >= 0.5;
    const size_t n = held.size ();
    const double w = (right - left) / 16.0 * 0.6;
    for (size_t b = 0; b < n; ++b)
    {
        const double x = logX (freq[b], kLo, kHi, left, right);
        const double y = yOf (held[b]);
        ctx->setFillColor (!on ? CColor (130, 130, 130, 120) : (held[b] >= 0.0f ? CColor (120, 200, 190, 200) : CColor (240, 110, 110, 200)));
        ctx->drawRect (CRect (x - w / 2, std::min (y, mid), x + w / 2, std::max (y, mid)), kDrawFilled);
    }
    freqAxis (ctx, left, right, bottom + 3, kLo, kHi);
    const bool boost = host->plainValue (kSpkMode) >= 0.5;
    text (ctx, boost ? "TRANSIENTS BOOSTED, PER BAND (dB)" : "TRANSIENTS CUT, PER BAND (dB)",
          CRect (all.left + 6, all.top + 3, all.right - 6, all.top + 17), theme::kTextBright, 10.0, kLeftText, true);
    ctx->resetClipRect ();
    setDirty (false);
}

// --- OrbView -----------------------------------------------------------------------------

OrbView::OrbView (const CRect& r, pk::ParamHost* h, MeterSource m) : CView (r), host (h), meters (std::move (m)) {}

void OrbView::idle ()
{
    const Meters* m = meters ? meters () : nullptr;
    if (!m)
        return;
    const uint32_t s = m->stageBlocks.load (std::memory_order_relaxed);
    if (s == seen)
        return;
    seen = s;
    orbs = std::clamp (m->orbs.load (), 0, Motion::kMaxOrbs);
    distance = m->motionDistance.load ();
    radius = m->motionRadius.load ();
    trailPos = (trailPos + 1) % kTrail;
    for (int k = 0; k < orbs; ++k)
    {
        x[k] = m->orbX[(size_t)k].load (std::memory_order_relaxed);
        y[k] = m->orbY[(size_t)k].load (std::memory_order_relaxed);
        tx[k][trailPos] = x[k];
        ty[k][trailPos] = y[k];
    }
    invalid ();
}

void OrbView::draw (CDrawContext* ctx)
{
    const CRect all = getViewSize ();
    background (ctx, all);
    ctx->setClipRect (all);
    // metres to pixels: the listener near the bottom, the swarm's ball and a margin in view
    const double cx = (all.left + all.right) / 2, ly = all.bottom - 26;
    const double span = std::max (1.0, (double)distance + (double)radius * 1.15);
    const double scale = std::min ((ly - all.top - 26) / span, (all.getWidth () / 2 - 12) / std::max (1.0, (double)radius * 1.15));
    auto px = [&] (double mx) { return cx + mx * scale; };
    auto py = [&] (double my) { return ly - my * scale; };
    // metre rings round the listener
    ctx->setLineWidth (1.0);
    for (double m = 1.0; m <= span + 0.5; m += (span > 8.0 ? 5.0 : 1.0))
    {
        ctx->setFrameColor (theme::kGrid);
        ctx->drawEllipse (CRect (px (-m), py (m), px (m), py (-m)), kDrawStroked);
    }
    // the swarm's ball
    ctx->setFrameColor (CColor (150, 130, 255, 120));
    ctx->drawEllipse (CRect (px (-radius), py (distance + radius), px (radius), py (distance - radius)), kDrawStroked);
    // the listener (a head with ears, facing up)
    ctx->setFillColor (theme::kText);
    ctx->drawEllipse (CRect (cx - 6, ly - 6, cx + 6, ly + 6), kDrawFilled);
    ctx->drawEllipse (CRect (cx - 9, ly - 2, cx - 5, ly + 2), kDrawFilled);
    ctx->drawEllipse (CRect (cx + 5, ly - 2, cx + 9, ly + 2), kDrawFilled);
    ctx->drawLine (CPoint (cx, ly - 6), CPoint (cx, ly - 11));
    const bool on = host->plainValue (kMotOn) >= 0.5;
    const CColor c = on ? CColor (150, 130, 255) : CColor (130, 130, 130);
    for (int k = 0; k < orbs; ++k)
    {
        // the trail, fading
        for (int t = 1; t < kTrail; ++t)
        {
            const int i0 = (trailPos - t + kTrail) % kTrail, i1 = (trailPos - t + 1 + kTrail) % kTrail;
            if (tx[k][i0] == 0.0f && ty[k][i0] == 0.0f)
                continue;
            CColor tc = c;
            tc.alpha = (uint8_t)(160 * (kTrail - t) / kTrail);
            ctx->setFrameColor (tc);
            ctx->setLineWidth (2.0);
            ctx->drawLine (CPoint (px (tx[k][i0]), py (ty[k][i0])), CPoint (px (tx[k][i1]), py (ty[k][i1])));
        }
        ctx->setFillColor (c);
        ctx->drawEllipse (CRect (px (x[k]) - 4, py (y[k]) - 4, px (x[k]) + 4, py (y[k]) + 4), kDrawFilled);
    }
    char buf[96];
    std::snprintf (buf, sizeof (buf), "ORBS FROM ABOVE  %d, %s, %.0f m/s", orbs, host->plainValue (kMotPattern) >= 0.5 ? "swarm" : "orbit",
                   host->plainValue (kMotSpeed));
    text (ctx, buf, CRect (all.left + 6, all.top + 3, all.right - 6, all.top + 17), theme::kTextBright, 10.0, kLeftText, true);
    text (ctx, "you", CRect (cx + 12, ly - 6, cx + 60, ly + 8), theme::kTextDim, 9.0);
    ctx->resetClipRect ();
    setDirty (false);
}

// --- HistoryView -------------------------------------------------------------------------

HistoryView::HistoryView (const CRect& r, std::string t, double rangeDb, Take tk)
    : CView (r), title (std::move (t)), range (rangeDb), take (std::move (tk))
{
}

void HistoryView::idle ()
{
    float b = 0.0f, c = 0.0f;
    if (!take || !take (b, c))
        return;
    pos = (pos + 1) % kLen;
    boost[pos] = b;
    cut[pos] = c;
    invalid ();
}

void HistoryView::draw (CDrawContext* ctx)
{
    const CRect all = getViewSize ();
    background (ctx, all);
    ctx->setClipRect (all);
    const double left = all.left + 30, right = all.right - 8, top = all.top + 22, bottom = all.bottom - 8;
    const bool twoSided = range > 0.0;
    const double r = std::fabs (range);
    const double zero = twoSided ? (top + bottom) / 2 : top;
    auto yOf = [&] (double db) {
        return twoSided ? zero - (bottom - top) / 2 * std::clamp (db, -r, r) / r : top + (bottom - top) * std::clamp (-db, 0.0, r) / r;
    };
    const double step = r >= 12.0 ? 6.0 : 3.0;
    for (double db = twoSided ? -r : -r; db <= (twoSided ? r : 0.0) + 1e-9; db += step)
    {
        ctx->setFrameColor (std::fabs (db) < 1e-9 ? theme::kPanelEdge : theme::kGrid);
        ctx->drawLine (CPoint (left, yOf (db)), CPoint (right, yOf (db)));
        char buf[12];
        std::snprintf (buf, sizeof (buf), twoSided ? "%+.0f" : "%.0f", db);
        text (ctx, buf, CRect (all.left + 2, yOf (db) - 6, left - 4, yOf (db) + 6), theme::kTextDim, 8.5, kRightText);
    }
    const double w = (right - left) / kLen;
    for (int i = 0; i < kLen; ++i)
    {
        const int k = (pos + 1 + i) % kLen;
        const double x = left + i * w;
        if (boost[k] > 0.01f)
        {
            ctx->setFillColor (CColor (120, 200, 190, 210));
            ctx->drawRect (CRect (x, yOf (boost[k]), x + w + 0.5, zero), kDrawFilled);
        }
        if (cut[k] < -0.01f)
        {
            ctx->setFillColor (CColor (240, 110, 110, 210));
            ctx->drawRect (CRect (x, zero, x + w + 0.5, yOf (cut[k])), kDrawFilled);
        }
    }
    text (ctx, title, CRect (all.left + 6, all.top + 3, all.right - 6, all.top + 17), theme::kTextBright, 10.0, kLeftText, true);
    ctx->resetClipRect ();
    setDirty (false);
}

// --- TtmView -----------------------------------------------------------------------------

TtmView::TtmView (const CRect& r, int c, pk::ParamHost* h, MeterSource m) : CView (r), comp (c), host (h), meters (std::move (m)) {}

void TtmView::idle ()
{
    const Meters* m = meters ? meters () : nullptr;
    if (!m)
        return;
    for (int b = 0; b < Ttm::kBands; ++b)
    {
        level[b] = m->compLevelDb[comp][b].load (std::memory_order_relaxed);
        target[b] = m->compTargetDb[comp][b].load (std::memory_order_relaxed);
        gain[b] = m->compGainDb[comp][b].load (std::memory_order_relaxed);
    }
    makeup = m->compMakeupDb[comp].load (std::memory_order_relaxed);
    invalid ();
}

void TtmView::draw (CDrawContext* ctx)
{
    const CRect all = getViewSize ();
    background (ctx, all);
    ctx->setClipRect (all);
    const double left = all.left + 30, right = all.right - 8, top = all.top + 22, bottom = all.bottom - 18;
    constexpr double kFloor = -72.0;
    auto yOf = [&] (double db) { return top + (bottom - top) * std::clamp (db, kFloor, 0.0) / kFloor; };
    for (double db : {0.0, -12.0, -24.0, -36.0, -48.0, -60.0, -72.0})
    {
        ctx->setFrameColor (theme::kGrid);
        ctx->drawLine (CPoint (left, yOf (db)), CPoint (right, yOf (db)));
        char buf[12];
        std::snprintf (buf, sizeof (buf), "%.0f", db);
        text (ctx, buf, CRect (all.left + 2, yOf (db) - 6, left - 4, yOf (db) + 6), theme::kTextDim, 8.5, kRightText);
    }
    const uint32_t base = kCompBase[comp];
    const bool on = host->plainValue (base + kCompOn) >= 0.5;
    const double colW = (right - left) / Ttm::kBands;
    static const char* names[Ttm::kBands] = {"LOW", "MID", "HIGH"};
    for (int b = 0; b < Ttm::kBands; ++b)
    {
        const double x0 = left + b * colW + colW * 0.2, x1 = left + (b + 1) * colW - colW * 0.2, xm = (x0 + x1) / 2;
        // the level, the target, and an arrow for the gain from the level
        ctx->setFillColor (CColor (190, 196, 204, 70));
        ctx->drawRect (CRect (x0, yOf (level[b]), xm - 2, bottom), kDrawFilled);
        ctx->setFrameColor (on ? CColor (120, 210, 120) : theme::kTextDim);
        ctx->setLineWidth (2.0);
        ctx->drawLine (CPoint (x0 - 6, yOf (target[b])), CPoint (x1 + 6, yOf (target[b])));
        const double y0 = yOf (level[b]), y1 = yOf (level[b] + gain[b]);
        ctx->setFillColor (gain[b] >= 0.0f ? CColor (120, 200, 190, 220) : CColor (240, 110, 110, 220));
        ctx->drawRect (CRect (xm + 2, std::min (y0, y1), x1, std::max (y0, y1) + 1), kDrawFilled);
        char buf[24];
        std::snprintf (buf, sizeof (buf), "%s  %+.1f dB", names[b], gain[b]);
        text (ctx, buf, CRect (x0 - 10, bottom + 3, x1 + 10, bottom + 15), theme::kTextDim, 9.0, kCenterText);
    }
    char buf[96];
    std::snprintf (buf, sizeof (buf), "LEVEL (grey), TARGET (line), GAIN (bar)   make-up %+.1f dB", makeup);
    text (ctx, buf, CRect (all.left + 6, all.top + 3, all.right - 6, all.top + 17), theme::kTextBright, 10.0, kLeftText, true);
    ctx->resetClipRect ();
    setDirty (false);
}

// --- TapeView ----------------------------------------------------------------------------

TapeView::TapeView (const CRect& r, pk::ParamHost* h) : CView (r), host (h), tape (std::make_unique<Tape> ()) {}

void TapeView::draw (CDrawContext* ctx)
{
    const CRect all = getViewSize ();
    background (ctx, all);
    ctx->setClipRect (all);
    tape->setDriveDb (0, host->plainValue (kTapeLowDrive));
    tape->setDriveDb (1, host->plainValue (kTapeHighDrive));
    // the curves for input -1 .. 1 (0 dBFS), in a square
    const double size = std::min (all.getWidth () - 16, all.getHeight () - 30);
    const double l = all.left + 8, t = all.top + 22, cxp = l + size / 2, cyp = t + size / 2;
    ctx->setFrameColor (theme::kGrid);
    ctx->setLineWidth (1.0);
    ctx->drawRect (CRect (l, t, l + size, t + size), kDrawStroked);
    ctx->drawLine (CPoint (l, cyp), CPoint (l + size, cyp));
    ctx->drawLine (CPoint (cxp, t), CPoint (cxp, t + size));
    ctx->drawLine (CPoint (l, t + size), CPoint (l + size, t));
    const bool on = host->plainValue (kTapeOn) >= 0.5;
    const CColor colors[2] = {CColor (255, 205, 90), CColor (255, 140, 60)};
    for (int b = 0; b < 2; ++b)
    {
        auto path = owned (ctx->createGraphicsPath ());
        if (!path)
            continue;
        const double level = std::pow (10.0, host->plainValue (b == 0 ? kTapeLowLevel : kTapeHighLevel) / 20.0);
        const double mix = host->plainValue (b == 0 ? kTapeLowMix : kTapeHighMix);
        for (int i = 0; i <= 100; ++i)
        {
            const float x = -1.0f + 2.0f * (float)i / 100.0f;
            const double y = level * (x + mix * (tape->curve (b, x) - x));
            const CPoint p (cxp + x * size / 2, cyp - std::clamp (y, -1.2, 1.2) * size / 2);
            if (i == 0)
                path->beginSubpath (p);
            else
                path->addLine (p);
        }
        ctx->setFrameColor (on ? colors[b] : theme::kTextDim);
        ctx->setLineWidth (2.0);
        ctx->drawGraphicsPath (path, CDrawContext::kPathStroked);
    }
    const double tx = l + size + 14;
    char buf[64];
    text (ctx, "WARM TAPE CURVES", CRect (all.left + 6, all.top + 3, all.right - 6, all.top + 17), theme::kTextBright, 10.0, kLeftText, true);
    std::snprintf (buf, sizeof (buf), "below %.0f Hz", host->plainValue (kTapeSplit));
    text (ctx, buf, CRect (tx, t + 4, all.right - 4, t + 18), colors[0], 9.5);
    std::snprintf (buf, sizeof (buf), "above %.0f Hz", host->plainValue (kTapeSplit));
    text (ctx, buf, CRect (tx, t + 20, all.right - 4, t + 34), colors[1], 9.5);
    text (ctx, "in: 0 dBFS at the edges", CRect (tx, t + 44, all.right - 4, t + 58), theme::kTextDim, 9.0);
    text (ctx, "level matched at -18 dBFS", CRect (tx, t + 58, all.right - 4, t + 72), theme::kTextDim, 9.0);
    ctx->resetClipRect ();
    setDirty (false);
}

} // namespace detonatr
