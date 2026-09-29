#include "BandView.h"

#include "pluginkit/ui/Theme.h"
#include "smacheratr/src/core/Biquad.h"

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
constexpr double kHandleRadius = 5.0;
CColor bandColor (int band, uint8_t alpha = 255)
{
    return band == 0 ? CColor (120, 210, 140, alpha) : CColor (130, 170, 255, alpha);
}
void text (CDrawContext* ctx, const std::string& s, const CRect& r, const CColor& c, double size, CHoriTxtAlign a = kCenterText,
           bool bold = false)
{
    ctx->setFont (theme::font (size, bold));
    ctx->setFontColor (c);
    ctx->drawString (s.c_str (), r, a, true);
}
} // namespace

BandView::BandView (const CRect& r, pk::ParamHost* h, MeterSource m) : CView (r), host (h), meters (std::move (m)) {}

double BandView::xOfHz (double hz) const
{
    const CRect r = getViewSize ();
    return r.left + std::log (std::clamp (hz, kMinHz, kMaxHz) / kMinHz) / std::log (kMaxHz / kMinHz) * r.getWidth ();
}

double BandView::yOfDb (double db) const
{
    const CRect r = getViewSize ();
    return r.getCenter ().y - std::clamp (db, -kMaxDb, kMaxDb) / kMaxDb * (r.getHeight () * 0.5 - 12.0);
}

CPoint BandView::handle (int b) const
{
    return CPoint (xOfHz (host->plainValue (bandParam (b, kFreq))), yOfDb (host->plainValue (bandParam (b, kGain))));
}

double BandView::edgeX (int b, bool high) const
{
    const double half = std::pow (2.0, 0.5 * host->plainValue (bandParam (b, kWidth)));
    const double f = host->plainValue (bandParam (b, kFreq));
    return xOfHz (high ? f * half : f / half);
}

bool BandView::live () const { return lastBlocks != 0 && idleSinceBlock < 10; }

void BandView::idle ()
{
    const Meters* m = meters ? meters () : nullptr;
    if (!m)
        return;
    const uint32_t blocks = m->blocks.load (std::memory_order_relaxed);
    if (blocks != lastBlocks)
    {
        lastBlocks = blocks;
        idleSinceBlock = 0;
    }
    else if (idleSinceBlock < (1 << 20))
        ++idleSinceBlock;
    bool changed = false;
    for (int b = 0; b < kBands; ++b)
    {
        const float f = m->freqHz[b].load (std::memory_order_relaxed), g = m->gainDb[b].load (std::memory_order_relaxed);
        changed |= std::fabs (f - shownFreq[b]) > 0.5f || std::fabs (g - shownDb[b]) > 0.05f;
        shownFreq[b] = f;
        shownDb[b] = g;
    }
    if (changed)
        invalid ();
}

void BandView::draw (CDrawContext* ctx)
{
    const CRect all = getViewSize ();
    ctx->setFillColor (theme::kWaveBg);
    ctx->drawRect (all, kDrawFilled);
    ctx->setClipRect (all);
    ctx->setLineWidth (1.0);
    for (double f : {50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0})
    {
        const bool major = f == 100.0 || f == 1000.0 || f == 10000.0;
        ctx->setFrameColor (major ? CColor (58, 58, 64) : theme::kGrid);
        ctx->drawLine (CPoint (xOfHz (f), all.top), CPoint (xOfHz (f), all.bottom));
        if (major)
        {
            char buf[16];
            std::snprintf (buf, sizeof (buf), f >= 1000 ? "%.0fk" : "%.0f", f >= 1000 ? f / 1000 : f);
            text (ctx, buf, CRect (xOfHz (f) - 20, all.bottom - 14, xOfHz (f) + 20, all.bottom - 2), theme::kTextDim, 9.5);
        }
    }
    for (double db : {-12.0, 0.0, 12.0})
    {
        ctx->setFrameColor (db == 0.0 ? CColor (58, 58, 64) : theme::kGrid);
        ctx->drawLine (CPoint (all.left, yOfDb (db)), CPoint (all.right, yOfDb (db)));
    }

    const double sr = meters && meters () ? (double)meters ()->sampleRate.load () : 48000.0;
    const int steps = 180;
    auto curve = [&] (double hz, double db, double q, bool closed) {
        auto path = owned (ctx->createGraphicsPath ());
        if (!path)
            return path;
        const auto c = smacheratr::peak (sr, std::clamp (hz, 20.0, 0.45 * sr), db, q);
        for (int i = 0; i <= steps; ++i)
        {
            const double f = kMinHz * std::pow (kMaxHz / kMinHz, (double)i / steps);
            const CPoint pt (xOfHz (f), yOfDb (smacheratr::magnitudeDb (c, f, sr)));
            if (i == 0)
                path->beginSubpath (closed ? CPoint (all.left, yOfDb (0.0)) : pt);
            if (i > 0 || closed)
                path->addLine (pt);
        }
        if (closed)
        {
            path->addLine (CPoint (all.right, yOfDb (0.0)));
            path->closeSubpath ();
        }
        return path;
    };
    // the unselected band first, so the selected one is on top
    for (int pass = 0; pass < kBands; ++pass)
    {
        const int b = pass == 0 ? 1 - selected : selected;
        const bool on = host->plainValue (bandParam (b, kBandOn)) >= 0.5;
        const double freq = host->plainValue (bandParam (b, kFreq)), gain = host->plainValue (bandParam (b, kGain));
        const double depth = host->plainValue (bandParam (b, kDepth)), sweep = host->plainValue (bandParam (b, kSweep));
        const double q = bellQ (host->plainValue (bandParam (b, kWidth)));
        const int target = (int)std::lround (host->plainValue (bandParam (b, kTarget)));
        const uint8_t a = on ? 255 : 90;
        // its edges (drag them for the width)
        if (on)
        {
            ctx->setLineWidth (1.0);
            ctx->setFrameColor (bandColor (b, drag == b && dragEdge ? 200 : (b == selected ? 90 : 45)));
            for (bool high : {false, true})
                ctx->drawLine (CPoint (edgeX (b, high), all.top + 20), CPoint (edgeX (b, high), all.bottom - 16));
        }
        // the range the shape covers
        ctx->setLineStyle (CLineStyle (CLineStyle::kLineCapButt, CLineStyle::kLineJoinMiter, 0.0, {3.0, 3.0}));
        ctx->setLineWidth (1.0);
        ctx->setFrameColor (bandColor (b, on ? 120 : 50));
        if (target != kTargetFreq)
            for (double s : {-1.0, 1.0})
                if (auto p = curve (freq, gain + s * depth, q, false))
                    ctx->drawGraphicsPath (p, CDrawContext::kPathStroked);
        if (target != kTargetGain)
            for (double s : {-1.0, 1.0})
            {
                const double x = xOfHz (freq * std::pow (2.0, 0.5 * s * sweep));
                ctx->drawLine (CPoint (x, all.top + 4), CPoint (x, all.bottom - 16));
            }
        ctx->setLineStyle (kLineSolid);
        // the band now (moving with its shape), or at its setting
        const double nowHz = on && live () ? shownFreq[b] : freq, nowDb = on && live () ? shownDb[b] : gain;
        if (auto p = curve (nowHz, nowDb, q, true))
        {
            ctx->setFillColor (bandColor (b, on ? 60 : 20));
            ctx->drawGraphicsPath (p, CDrawContext::kPathFilled);
            ctx->setLineWidth (b == selected ? 2.0 : 1.4);
            ctx->setFrameColor (bandColor (b, a));
            ctx->drawGraphicsPath (p, CDrawContext::kPathStroked);
        }
        const CPoint h = handle (b);
        const CRect hr (h.x - kHandleRadius - 1, h.y - kHandleRadius - 1, h.x + kHandleRadius + 1, h.y + kHandleRadius + 1);
        ctx->setFillColor (bandColor (b, a));
        ctx->drawEllipse (hr, kDrawFilled);
        ctx->setLineWidth (1.5);
        ctx->setFrameColor (b == selected ? theme::kTextBright : CColor (0, 0, 0, 160));
        ctx->drawEllipse (hr, kDrawStroked);
        char buf[48];
        std::snprintf (buf, sizeof (buf), "%d", b + 1);
        text (ctx, buf, CRect (h.x - 20, h.y - 22, h.x + 20, h.y - 8), bandColor (b, a), 9.5, kCenterText, true);
    }
    text (ctx, "BANDS", CRect (all.left + 6, all.top + 4, all.right - 6, all.top + 18), theme::kTextBright, 10.5, kLeftText, true);
    ctx->setLineWidth (1.0);
    ctx->resetClipRect ();
}

int BandView::hit (const CPoint& p) const
{
    for (int pass = 0; pass < kBands; ++pass)
    {
        const int b = pass == 0 ? selected : 1 - selected; // the one on top first
        const CPoint h = handle (b);
        if (std::hypot (p.x - h.x, p.y - h.y) <= kHandleRadius + 4.0)
            return b;
    }
    return -1;
}

int BandView::hitEdge (const CPoint& p) const
{
    for (int pass = 0; pass < kBands; ++pass)
    {
        const int b = pass == 0 ? selected : 1 - selected;
        if (host->plainValue (bandParam (b, kBandOn)) < 0.5)
            continue;
        for (bool high : {false, true})
            if (std::fabs (p.x - edgeX (b, high)) <= 4.0)
                return b;
    }
    return -1;
}

void BandView::onMouseDownEvent (MouseDownEvent& e)
{
    const bool right = e.buttonState.isRight ();
    if (!e.buttonState.isLeft () && !right)
        return;
    if (drag >= 0) // a drag the host never ended
    {
        MouseUpEvent up;
        onMouseUpEvent (up);
    }
    drag = hit (e.mousePosition);
    dragEdge = false;
    if (drag < 0 && (drag = hitEdge (e.mousePosition)) >= 0)
    {
        // an edge: the width
        if (onBandPicked)
            onBandPicked (drag);
        const uint32_t w = bandParam (drag, kWidth);
        if (e.clickCount == 2 || right)
        {
            host->setOnce (w, host->table ().defaultNormalized (w));
            drag = -1;
            invalid ();
            e.consumed = true;
            e.ignoreFollowUpMoveAndUpEvents (true);
            return;
        }
        dragEdge = true;
        host->beginEdit (w);
        e.consumed = true;
        return;
    }
    if (drag < 0)
        return;
    if (onBandPicked)
        onBandPicked (drag);
    const uint32_t f = bandParam (drag, kFreq), g = bandParam (drag, kGain);
    if (e.clickCount == 2 || right) // reset the band's frequency and gain
    {
        host->setOnce (f, host->table ().defaultNormalized (f));
        host->setOnce (g, host->table ().defaultNormalized (g));
        drag = -1;
        invalid ();
        e.consumed = true;
        e.ignoreFollowUpMoveAndUpEvents (true);
        return;
    }
    down = e.mousePosition;
    startFreq = host->plainValue (f);
    startGain = host->plainValue (g);
    host->beginEdit (f);
    host->beginEdit (g);
    e.consumed = true;
}

void BandView::onMouseMoveEvent (MouseMoveEvent& e)
{
    if (drag < 0)
    {
        if (auto* fr = getFrame ())
            fr->setCursor (hit (e.mousePosition) >= 0 ? kCursorSizeAll : (hitEdge (e.mousePosition) >= 0 ? kCursorHSize : kCursorDefault));
        return;
    }
    const CRect r = getViewSize ();
    if (dragEdge)
    {
        // the edge follows the mouse; the band stays centred, so the width is twice the distance
        const double hz = kMinHz * std::pow (kMaxHz / kMinHz, (e.mousePosition.x - r.left) / r.getWidth ());
        const double w = 2.0 * std::fabs (std::log2 (hz / host->plainValue (bandParam (drag, kFreq))));
        host->setNorm (bandParam (drag, kWidth), host->table ().toNormalized (bandParam (drag, kWidth), std::clamp (w, 0.5, 4.0)));
        invalid ();
        e.consumed = true;
        return;
    }
    const double fine = e.modifiers.has (ModifierKey::Shift) ? 0.2 : 1.0;
    const double dx = (e.mousePosition.x - down.x) * fine, dy = (e.mousePosition.y - down.y) * fine;
    auto setPlain = [this] (uint32_t id, double v) { host->setNorm (id, host->table ().toNormalized (id, v)); };
    setPlain (bandParam (drag, kFreq), startFreq * std::pow (kMaxHz / kMinHz, dx / r.getWidth ()));
    setPlain (bandParam (drag, kGain), std::clamp (startGain - dy * kMaxDb / (r.getHeight () * 0.5 - 12.0), -24.0, 24.0));
    invalid ();
    e.consumed = true;
}

void BandView::onMouseCancelEvent (MouseCancelEvent& e)
{
    MouseUpEvent up;
    onMouseUpEvent (up); // closes the edits of the drag
    e.consumed = true;
}

void BandView::onMouseUpEvent (MouseUpEvent& e)
{
    if (drag < 0)
        return;
    if (dragEdge)
        host->endEdit (bandParam (drag, kWidth));
    else
    {
        host->endEdit (bandParam (drag, kFreq));
        host->endEdit (bandParam (drag, kGain));
    }
    drag = -1;
    dragEdge = false;
    e.consumed = true;
}

void BandView::onMouseWheelEvent (MouseWheelEvent& e)
{
    const int b = drag >= 0 ? drag : hit (e.mousePosition);
    if (b < 0)
        return;
    const uint32_t id = bandParam (b, kWidth);
    const double dn = pk::wheelStep (e, host->table (), id);
    if (dn == 0.0)
        return;
    host->setOnce (id, std::clamp (host->norm (id) + dn, 0.0, 1.0));
    invalid ();
    e.consumed = true;
}

} // namespace wubr
