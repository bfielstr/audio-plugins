#include "HistoryView.h"

#include "pluginkit/ui/Theme.h"

#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cgraphicspath.h"
#include "vstgui/lib/events.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace smoothr {

using namespace VSTGUI;
namespace theme = pk::theme;

namespace {
// as in most limiter displays: the output a solid body, what the limiter took off the input lighter above it
const CColor kInFill (128, 133, 142, 150), kOutFill (70, 75, 84), kOutLine (150, 156, 168), kCeilingLine (255, 230, 120, 170);
constexpr double kFallDbPerIdle = 24.0 / 30.0; // the bars fall 24 dB a second (idle runs about 30 times a second)

void text (CDrawContext* ctx, const std::string& s, const CRect& r, const CColor& c, double size, CHoriTxtAlign a = kCenterText,
           bool bold = false)
{
    ctx->setFont (theme::font (size, bold));
    ctx->setFontColor (c);
    ctx->drawString (s.c_str (), r, a, true);
}

double toDb (float linear) { return linear > 1e-6f ? 20.0 * std::log10 (linear) : -120.0; }

std::string dbText (double db, bool sign = false)
{
    char buf[24];
    if (db <= -99.0)
        return "-inf";
    std::snprintf (buf, sizeof (buf), sign ? "%+.1f" : "%.1f", db);
    return buf;
}
} // namespace

CColor HistoryView::lowColor (uint8_t alpha) { return CColor (255, 164, 40, alpha); }
CColor HistoryView::highColor (uint8_t alpha) { return CColor (238, 82, 76, alpha); }

HistoryView::HistoryView (const CRect& r, pk::ParamHost* h, MeterSource m) : CView (r), host (h), meters (std::move (m)) {}

CRect HistoryView::plotRect () const
{
    const CRect r = getViewSize ();
    return CRect (r.left + 8.0, r.top + kHeaderH + 4.0, r.right - kMetersW, r.bottom - 8.0);
}

CRect HistoryView::metersRect () const
{
    const CRect r = getViewSize ();
    return CRect (r.right - kMetersW + 10.0, r.top + kHeaderH + 4.0, r.right - 8.0, r.bottom - 8.0);
}

double HistoryView::yOfDb (double db) const
{
    const CRect p = plotRect ();
    const double t = std::clamp (-db / kRangeDb, 0.0, 1.0);
    return p.top + t * p.getHeight ();
}

void HistoryView::idle ()
{
    Meters* m = meters ? meters () : nullptr;
    bool changed = false;
    if (m)
    {
        const uint32_t w = m->level.written ();
        if (w != lastWritten)
        {
            lastWritten = w;
            const int n = std::max (1, (int)plotRect ().getWidth ());
            inPk.resize ((size_t)n);
            outPk.resize ((size_t)n);
            grLow.resize ((size_t)n);
            grHigh.resize ((size_t)n);
            const int a = m->level.read (inPk.data (), outPk.data (), n);
            const int b = m->gr.read (grLow.data (), grHigh.data (), n);
            have = std::min (a, b);
            // (read returns the last n entries, oldest first; with fewer written, the real ones are at the end)
            if (have < n)
            {
                const size_t off = (size_t)(n - have);
                std::move (inPk.begin () + (long)off, inPk.end (), inPk.begin ());
                std::move (outPk.begin () + (long)off, outPk.end (), outPk.begin ());
                std::move (grLow.begin () + (long)off, grLow.end (), grLow.begin ());
                std::move (grHigh.begin () + (long)off, grHigh.end (), grHigh.begin ());
            }
            changed = true;
        }
        for (int c = 0; c < 2; ++c)
        {
            const double in = toDb (m->inPeak[c].exchange (0.0f, std::memory_order_relaxed));
            const double out = toDb (m->outPeak[c].exchange (0.0f, std::memory_order_relaxed));
            barIn[c] = std::max (in, barIn[c] - kFallDbPerIdle);
            barOut[c] = std::max (out, barOut[c] - kFallDbPerIdle);
            holdIn = std::max (holdIn, in);
            holdOut = std::max (holdOut, out);
        }
        const double lo = have > 0 ? grLow[(size_t)have - 1] : 0.0, hi = have > 0 ? grHigh[(size_t)have - 1] : 0.0;
        barLow = std::max (lo, barLow - kFallDbPerIdle);
        barHigh = std::max (hi, barHigh - kFallDbPerIdle);
        holdGr = std::max ({holdGr, lo, hi});
        changed = true;
    }
    if (changed)
        invalid ();
}

void HistoryView::onMouseDownEvent (MouseDownEvent& e)
{
    if (!e.buttonState.isLeft ())
        return;
    if (metersRect ().pointInside (e.mousePosition))
    {
        holdIn = holdOut = -120.0;
        holdGr = 0.0;
        invalid ();
        e.consumed = true;
    }
}

void HistoryView::draw (CDrawContext* ctx)
{
    const CRect r = getViewSize ();
    if (auto p = owned (ctx->createGraphicsPath ()))
    {
        p->addRoundRect (r, 5.0);
        ctx->setFillColor (theme::kWaveBg);
        ctx->drawGraphicsPath (p, CDrawContext::kPathFilled);
        ctx->setFrameColor (theme::kPanelEdge);
        ctx->setLineWidth (1.0);
        ctx->drawGraphicsPath (p, CDrawContext::kPathStroked);
    }
    drawHistory (ctx);
    drawMeters (ctx);
}

void HistoryView::drawHistory (CDrawContext* ctx)
{
    const CRect r = getViewSize ();
    const CRect p = plotRect ();

    // the header: what is what, and the reduction now
    {
        double x = r.left + 10.0;
        auto key = [&] (const CColor& c, const char* name, double w) {
            ctx->setFillColor (c);
            ctx->drawRect (CRect (x, r.top + 7.0, x + 10.0, r.top + 15.0), kDrawFilled);
            text (ctx, name, CRect (x + 14.0, r.top + 2.0, x + w, r.top + 20.0), theme::kText, 10.0, kLeftText);
            x += w;
        };
        key (CColor (128, 133, 142), "input", 56.0);
        key (kOutFill, "output", 60.0);
        key (lowColor (), "reduction on the lows", 138.0);
        key (highColor (), "on the highs", 90.0);
        const double lo = have > 0 ? grLow[(size_t)have - 1] : 0.0, hi = have > 0 ? grHigh[(size_t)have - 1] : 0.0;
        text (ctx, "lows " + dbText (-lo) + " dB", CRect (p.right - 190.0, r.top + 2.0, p.right - 96.0, r.top + 20.0), lowColor (), 10.5,
              kRightText, true);
        text (ctx, "highs " + dbText (-hi) + " dB", CRect (p.right - 96.0, r.top + 2.0, p.right - 2.0, r.top + 20.0), highColor (), 10.5,
              kRightText, true);
    }

    // the grid, every 6 dB
    ctx->setLineWidth (1.0);
    for (double db = 0.0; db >= -kRangeDb; db -= 6.0)
    {
        const double y = std::floor (yOfDb (db)) + 0.5;
        ctx->setFrameColor (theme::kGrid);
        ctx->drawLine (CPoint (p.left, y), CPoint (p.right, y));
        if (db > -kRangeDb)
            text (ctx, dbText (db), CRect (p.right - 30.0, y + 1.0, p.right - 3.0, y + 13.0), theme::kTextDim, 9.0, kRightText);
    }

    const int n = have;
    if (n > 1)
    {
        ctx->saveGlobalState ();
        ctx->setClipRect (p);
        auto xOf = [&] (int i) { return p.right - (double)(n - 1 - i); };
        // filled from the bottom (levels) or hanging from the top (reductions)
        auto area = [&] (const std::vector<float>& v, bool level, const CColor& fill, const CColor* line) {
            auto path = owned (ctx->createGraphicsPath ());
            auto edge = owned (ctx->createGraphicsPath ());
            if (!path || !edge)
                return;
            const double base = level ? p.bottom : p.top;
            path->beginSubpath (CPoint (xOf (0), base));
            for (int i = 0; i < n; ++i)
            {
                const double y = level ? yOfDb (toDb (v[(size_t)i])) : yOfDb (-v[(size_t)i]);
                const CPoint pt (xOf (i), y);
                path->addLine (pt);
                if (i == 0)
                    edge->beginSubpath (pt);
                else
                    edge->addLine (pt);
            }
            path->addLine (CPoint (xOf (n - 1), base));
            path->closeSubpath ();
            ctx->setFillColor (fill);
            ctx->drawGraphicsPath (path, CDrawContext::kPathFilled);
            if (line)
            {
                ctx->setFrameColor (*line);
                ctx->setLineWidth (1.5);
                ctx->drawGraphicsPath (edge, CDrawContext::kPathStroked);
            }
        };
        area (inPk, true, kInFill, nullptr);
        area (outPk, true, kOutFill, &kOutLine);
        const CColor hiLine = highColor (), loLine = lowColor ();
        area (grHigh, false, highColor (34), &hiLine);
        area (grLow, false, lowColor (40), &loLine);
        ctx->restoreGlobalState ();
    }
    else
        text (ctx, "the last five seconds show here while audio plays", p, theme::kTextDim, 10.5);

    // the ceiling
    const double ceilDb = host->plainValue (kCeiling);
    const double yc = std::floor (yOfDb (ceilDb)) + 0.5;
    ctx->setFrameColor (kCeilingLine);
    ctx->setLineWidth (1.0);
    ctx->setLineStyle (CLineStyle (CLineStyle::kLineCapButt, CLineStyle::kLineJoinMiter, 0.0, {4.0, 3.0}));
    ctx->drawLine (CPoint (p.left, yc), CPoint (p.right, yc));
    ctx->setLineStyle (kLineSolid);
    const CRect tag (p.left + 4.0, yc + 2.0, p.left + 84.0, yc + 15.0);
    ctx->setFillColor (CColor (22, 22, 22, 200));
    ctx->drawRect (tag, kDrawFilled);
    text (ctx, "ceiling " + dbText (ceilDb) + " dB", tag, kCeilingLine, 9.0);
}

void HistoryView::drawMeters (CDrawContext* ctx)
{
    const CRect m = metersRect ();
    const double top = m.top + 16.0, bottom = m.bottom - 16.0;
    auto yOf = [&] (double db) { return top + std::clamp (-db / kRangeDb, 0.0, 1.0) * (bottom - top); };
    const double w = 12.0, gap = 2.0;
    auto bar = [&] (double x, double db, bool fromTop, const CColor& c) {
        ctx->setFillColor (theme::kControlBg);
        ctx->drawRect (CRect (x, top, x + w, bottom), kDrawFilled);
        const double y = yOf (fromTop ? -db : db);
        ctx->setFillColor (c);
        if (fromTop)
            ctx->drawRect (CRect (x, top, x + w, y), kDrawFilled);
        else
            ctx->drawRect (CRect (x, y, x + w, bottom), kDrawFilled);
    };
    // over the ceiling: the input bars turn the reduction's colour above it
    const double ceilDb = host->plainValue (kCeiling);
    const double x0 = m.left, x1 = x0 + 2 * w + gap + 8.0, x2 = x1 + 2 * w + gap + 8.0;
    for (int c = 0; c < 2; ++c)
    {
        const double x = x0 + c * (w + gap);
        bar (x, barIn[c], false, CColor (128, 133, 142));
        if (barIn[c] > ceilDb)
        {
            ctx->setFillColor (highColor (200));
            ctx->drawRect (CRect (x, yOf (barIn[c]), x + w, yOf (ceilDb)), kDrawFilled);
        }
        bar (x1 + c * (w + gap), barOut[c], false, CColor (190, 196, 206));
    }
    bar (x2, barLow, true, lowColor ());
    bar (x2 + w + gap, barHigh, true, highColor ());
    // the ceiling across the input and output bars
    ctx->setFrameColor (kCeilingLine);
    ctx->setLineWidth (1.0);
    const double yc = std::floor (yOf (ceilDb)) + 0.5;
    ctx->drawLine (CPoint (x0 - 2.0, yc), CPoint (x1 + 2 * w + gap + 2.0, yc));

    // the holds above, the names below
    auto label = [&] (double x, const std::string& s, const CColor& c, bool below) {
        const CRect rr = below ? CRect (x - 6.0, bottom + 2.0, x + 2 * w + gap + 6.0, bottom + 15.0)
                               : CRect (x - 6.0, m.top, x + 2 * w + gap + 6.0, top - 2.0);
        text (ctx, s, rr, c, 9.0);
    };
    label (x0, dbText (holdIn), holdIn > ceilDb ? highColor () : theme::kText, false);
    label (x1, dbText (holdOut), theme::kText, false);
    label (x2, holdGr > 0.05 ? dbText (-holdGr) : "0.0", theme::kText, false);
    label (x0, "IN", theme::kTextDim, true);
    label (x1, "OUT", theme::kTextDim, true);
    label (x2, "GR", theme::kTextDim, true);
}

} // namespace smoothr
