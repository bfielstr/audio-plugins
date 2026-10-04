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
// as in most limiter displays: the output a solid body (copper, with a pale copper edge), what the
// limiter took off the input a lighter warm grey above it; the ceiling a dashed pale copper line. The
// reductions are the lit part: the lows' cinnabar and solid, the highs' pale copper and dashed.
const CColor kInFill = theme::withAlpha (theme::kTextDim, 70), kInSolid = theme::kTextDim, kOutFill = theme::withAlpha (theme::kCopper, 110),
             kOutLine = theme::kCopperPale, kCeilingLine = theme::kCopperPale;
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

CColor HistoryView::lowColor (uint8_t alpha) { return theme::withAlpha (theme::kEnergyLive, alpha); }
CColor HistoryView::highColor (uint8_t alpha) { return theme::withAlpha (theme::kCopperPale, alpha); }

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
    {
        const uint64_t key = shownState ();
        if (key != shownKey)
        {
            shownKey = key;
            invalid ();
        }
    }
}

uint64_t HistoryView::shownState () const
{
    // every value as it is drawn: the levels and the reductions clamped to the scale (the levels'
    // silence is -120 dB, far under it), the holds as their readouts round them
    auto clampDb = [] (double db) { return std::clamp (db, -kRangeDb, 0.0); };
    pk::LayerKey key;
    key.params (host).add (have);
    for (int i = 0; i < have && i < (int)inPk.size (); ++i)
        key.add (clampDb (toDb (inPk[(size_t)i])), clampDb (toDb (outPk[(size_t)i])), clampDb (-grLow[(size_t)i]), clampDb (-grHigh[(size_t)i]));
    const double lo = have > 0 ? grLow[(size_t)have - 1] : 0.0, hi = have > 0 ? grHigh[(size_t)have - 1] : 0.0;
    key.add (dbText (-lo), dbText (-hi));
    for (int c = 0; c < 2; ++c)
        key.add (clampDb (barIn[c]), clampDb (barOut[c]));
    key.add (clampDb (-barLow), clampDb (-barHigh));
    key.add (dbText (holdIn), dbText (holdOut), holdGr > 0.05 ? dbText (-holdGr) : std::string ("0.0"), holdIn > host->plainValue (kCeiling));
    return key.value ();
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

void HistoryView::meterLayout (double& top, double& bottom, double& w, double& gap, double x[3]) const
{
    const CRect m = metersRect ();
    top = m.top + 16.0;
    bottom = m.bottom - 16.0;
    w = 12.0;
    gap = 2.0;
    x[0] = m.left;
    x[1] = x[0] + 2 * w + gap + 8.0;
    x[2] = x[1] + 2 * w + gap + 8.0;
}

void HistoryView::paintBase (CDrawContext* ctx)
{
    const CRect r = getViewSize ();
    const CRect p = plotRect ();
    // a display well in a dim hairline with copper corner brackets
    ctx->setFillColor (theme::kWell);
    ctx->drawRect (r, kDrawFilled);
    pk::draw::outline (ctx, r, theme::kLineDim, 0);
    pk::draw::brackets (ctx, r, 6, theme::kCopper);

    // the header: what is what (the reduction now is drawn over the layer, at the right)
    {
        double x = r.left + 10.0;
        // each key a sample of how its trace is drawn: a swatch, or a short (dashed) line
        auto key = [&] (const CColor& c, const char* name, double w, int line = 0) {
            if (line == 0)
            {
                ctx->setFillColor (c);
                ctx->drawRect (CRect (x, r.top + 7.0, x + 10.0, r.top + 15.0), kDrawFilled);
            }
            else
            {
                ctx->setFrameColor (c);
                ctx->setLineWidth (1.5);
                if (line == 2)
                    ctx->setLineStyle (CLineStyle (CLineStyle::kLineCapButt, CLineStyle::kLineJoinMiter, 0.0, {2.0, 1.4}));
                ctx->drawLine (CPoint (x, r.top + 11.5), CPoint (x + 11.0, r.top + 11.5));
                ctx->setLineStyle (kLineSolid);
                ctx->setLineWidth (1.0);
            }
            text (ctx, name, CRect (x + 14.0, r.top + 2.0, x + w, r.top + 20.0), theme::kText, 10.0, kLeftText);
            x += w;
        };
        key (kInSolid, "input", 56.0);
        key (kOutFill, "output", 60.0);
        key (lowColor (), "reduction on the lows", 138.0, 1);
        key (highColor (), "on the highs", 90.0, 2);
    }

    // the grid, every 6 dB
    ctx->setLineWidth (1.0);
    for (double db = 0.0; db >= -kRangeDb; db -= 6.0)
    {
        const double y = std::floor (yOfDb (db)) + 0.5;
        ctx->setFrameColor (theme::kGridMinor);
        ctx->drawLine (CPoint (p.left, y), CPoint (p.right, y));
        if (db > -kRangeDb)
            text (ctx, dbText (db), CRect (p.right - 30.0, y + 1.0, p.right - 3.0, y + 13.0), theme::kTextDim, 9.0, kRightText);
    }

    // the meters' beds (thin bars on a dim track) and their names below them
    double top, bottom, w, gap, x[3];
    meterLayout (top, bottom, w, gap, x);
    ctx->setFillColor (theme::kLineDim);
    for (int k = 0; k < 3; ++k)
        for (int c = 0; c < 2; ++c)
            ctx->drawRect (CRect (x[k] + c * (w + gap), top, x[k] + c * (w + gap) + w, bottom), kDrawFilled);
    const char* names[3] = {"IN", "OUT", "GR"};
    for (int k = 0; k < 3; ++k)
        text (ctx, names[k], CRect (x[k] - 6.0, bottom + 2.0, x[k] + 2 * w + gap + 6.0, bottom + 15.0), theme::kTextDim, 9.0);
}

void HistoryView::draw (CDrawContext* ctx)
{
    baseLayer.draw (ctx, getViewSize (), 0, [this] (CDrawContext* c) { paintBase (c); });
    ctx->setLineStyle (kLineSolid); // (as the header's keys left it)
    drawHistory (ctx);
    drawMeters (ctx);
}

void HistoryView::drawHistory (CDrawContext* ctx)
{
    const CRect r = getViewSize ();
    const CRect p = plotRect ();

    // the reduction now, at the right of the header
    {
        const double lo = have > 0 ? grLow[(size_t)have - 1] : 0.0, hi = have > 0 ? grHigh[(size_t)have - 1] : 0.0;
        text (ctx, "lows " + dbText (-lo) + " dB", CRect (p.right - 190.0, r.top + 2.0, p.right - 96.0, r.top + 20.0), lowColor (), 10.5,
              kRightText, true);
        text (ctx, "highs " + dbText (-hi) + " dB", CRect (p.right - 96.0, r.top + 2.0, p.right - 2.0, r.top + 20.0), highColor (), 10.5,
              kRightText, true);
    }

    ctx->setLineWidth (1.0);
    const int n = have;
    if (n > 1)
    {
        ctx->saveGlobalState ();
        ctx->setClipRect (p);
        auto xOf = [&] (int i) { return p.right - (double)(n - 1 - i); };
        // filled from the bottom (levels) or hanging from the top (reductions)
        auto area = [&] (const std::vector<float>& v, bool level, const CColor& fill, const CColor* line, bool dashed = false) {
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
                ctx->setLineWidth (1.0);
                if (dashed)
                    ctx->setLineStyle (theme::dashed ());
                ctx->drawGraphicsPath (edge, CDrawContext::kPathStroked);
                ctx->setLineStyle (kLineSolid);
            }
        };
        area (inPk, true, kInFill, nullptr);
        area (outPk, true, kOutFill, &kOutLine);
        const CColor hiLine = highColor (), loLine = lowColor ();
        area (grHigh, false, highColor (22), &hiLine, true);
        area (grLow, false, lowColor (50), &loLine);
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
    ctx->setFillColor (theme::withAlpha (theme::kWell, 200));
    ctx->drawRect (tag, kDrawFilled);
    text (ctx, "ceiling " + dbText (ceilDb) + " dB", tag, kCeilingLine, 9.0);
}

void HistoryView::drawMeters (CDrawContext* ctx)
{
    const CRect m = metersRect ();
    double top, bottom, w, gap, xs[3];
    meterLayout (top, bottom, w, gap, xs);
    auto yOf = [&] (double db) { return top + std::clamp (-db / kRangeDb, 0.0, 1.0) * (bottom - top); };
    auto bar = [&] (double x, double db, bool fromTop, const CColor& c) {
        // (on its bed, in the cached layer)
        const double y = yOf (fromTop ? -db : db);
        ctx->setFillColor (c);
        if (fromTop)
            ctx->drawRect (CRect (x, top, x + w, y), kDrawFilled);
        else
            ctx->drawRect (CRect (x, y, x + w, bottom), kDrawFilled);
    };
    // over the ceiling: the input bars turn peak colour above it (the part the limiter takes off)
    const double ceilDb = host->plainValue (kCeiling);
    const double x0 = xs[0], x1 = xs[1], x2 = xs[2];
    for (int c = 0; c < 2; ++c)
    {
        const double x = x0 + c * (w + gap);
        bar (x, barIn[c], false, kInSolid);
        if (barIn[c] > ceilDb)
        {
            ctx->setFillColor (theme::kEnergyPeak);
            ctx->drawRect (CRect (x, yOf (barIn[c]), x + w, yOf (ceilDb)), kDrawFilled);
        }
        bar (x1 + c * (w + gap), barOut[c], false, theme::kEnergyLive);
    }
    bar (x2, barLow, true, lowColor ());
    bar (x2 + w + gap, barHigh, true, highColor ());
    // the ceiling across the input and output bars
    ctx->setFrameColor (kCeilingLine);
    ctx->setLineWidth (1.0);
    const double yc = std::floor (yOf (ceilDb)) + 0.5;
    ctx->drawLine (CPoint (x0 - 2.0, yc), CPoint (x1 + 2 * w + gap + 2.0, yc));

    // the holds above (the names below are in the cached layer)
    auto label = [&] (double x, const std::string& s, const CColor& c) {
        text (ctx, s, CRect (x - 6.0, m.top, x + 2 * w + gap + 6.0, top - 2.0), c, 9.0);
    };
    label (x0, dbText (holdIn), holdIn > ceilDb ? theme::kEnergyPeak : theme::kText);
    label (x1, dbText (holdOut), theme::kText);
    label (x2, holdGr > 0.05 ? dbText (-holdGr) : "0.0", theme::kText);
}

} // namespace smoothr
