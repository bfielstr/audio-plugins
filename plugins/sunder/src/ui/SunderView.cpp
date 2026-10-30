#include "SunderView.h"

#include "pluginkit/ui/Theme.h"

#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cframe.h"
#include "vstgui/lib/cgraphicspath.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace sunder {

using namespace VSTGUI;
namespace theme = pk::theme;

namespace {
constexpr int kPoints = 240; // along the frequency axis

void text (CDrawContext* ctx, const std::string& s, const CRect& r, const CColor& c, double size, CHoriTxtAlign a = kCenterText,
           bool bold = false)
{
    ctx->setFont (theme::font (size, bold));
    ctx->setFontColor (c);
    ctx->drawString (s.c_str (), r, a, true);
}
} // namespace

SunderView::SunderView (const CRect& r, pk::ParamHost* h, MeterSource m) : CView (r), host (h), meters (std::move (m)) {}

CRect SunderView::curveRect () const
{
    const CRect all = getViewSize ();
    const double side = all.getHeight () - 16.0;
    if (all.getWidth () < side + 300.0)
        return CRect ();
    return CRect (all.right - side - 8.0, all.top + 8.0, all.right - 8.0, all.bottom - 8.0);
}

CRect SunderView::responseRect () const
{
    const CRect all = getViewSize ();
    const CRect c = curveRect ();
    return c.isEmpty () ? all : CRect (all.left, all.top, c.left - 10.0, all.bottom);
}

double SunderView::xOfHz (double hz) const
{
    const CRect r = responseRect ();
    return r.left + std::log (std::clamp (hz, kMinHz, kMaxHz) / kMinHz) / std::log (kMaxHz / kMinHz) * r.getWidth ();
}

double SunderView::yOfDb (double db) const
{
    const CRect r = responseRect ();
    return r.top + (kMaxDb - std::clamp (db, kMinDb, kMaxDb)) / (kMaxDb - kMinDb) * (r.getHeight () - kScaleH);
}

void SunderView::paintGrid (CDrawContext* ctx)
{
    const CRect all = getViewSize ();
    ctx->setFillColor (theme::kWell);
    ctx->drawRect (all, kDrawFilled);
    ctx->setLineWidth (1.0);
    const CRect r = responseRect ();
    const double plotBottom = r.bottom - kScaleH;
    const double freqs[] = {20.0, 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0};
    const char* labels[] = {"20", "50", "100", "200", "500", "1k", "2k", "5k", "10k"};
    for (int i = 0; i < 9; ++i)
    {
        const double x = xOfHz (freqs[i]);
        ctx->setFrameColor (freqs[i] == 100.0 || freqs[i] == 1000.0 || freqs[i] == 10000.0 ? theme::kGridMajor : theme::kGridMinor);
        ctx->drawLine (CPoint (x, r.top), CPoint (x, plotBottom));
        text (ctx, labels[i], CRect (x - 20, plotBottom + 1, x + 20, r.bottom - 1), theme::kTextDim, 9.5);
    }
    for (double db : {-18.0, -12.0, -6.0, 0.0, 6.0, 12.0, 18.0})
    {
        const bool major = db == 0.0 || std::fabs (db) == 18.0;
        ctx->setFrameColor (db == 0.0 ? theme::kGridZero : (major ? theme::kGridMajor : theme::kGridMinor));
        // (on a pixel's centre: a line between two pixels rasterises onto either one, the cached grid and the
        // direct one apart, at a zoom like 150 %)
        const double y = std::floor (yOfDb (db)) + 0.5;
        ctx->drawLine (CPoint (r.left, y), CPoint (r.right, y));
        if (major)
        {
            char buf[8];
            std::snprintf (buf, sizeof (buf), db > 0.0 ? "+%.0f" : "%.0f", db);
            text (ctx, buf, CRect (r.left + 3, yOfDb (db) - 12, r.left + 31, yOfDb (db)), theme::kTextDim, 9.0, kLeftText);
        }
    }
    // the curve's box: its grid every 10 dB and the unity line
    const CRect c = curveRect ();
    if (c.isEmpty ())
        return;
    ctx->setFrameColor (theme::kGridMajor);
    ctx->drawRect (c, kDrawStroked);
    auto bx = [&] (double db) { return c.left + (db - kMinIn) / (kMaxIn - kMinIn) * c.getWidth (); };
    auto by = [&] (double db) { return c.bottom - (db - kMinIn) / (kMaxIn - kMinIn) * c.getHeight (); };
    ctx->setFrameColor (theme::kGridMinor);
    for (double db = -40.0; db <= 10.0; db += 10.0)
    {
        ctx->drawLine (CPoint (bx (db), c.top), CPoint (bx (db), c.bottom));
        ctx->drawLine (CPoint (c.left, by (db)), CPoint (c.right, by (db)));
    }
    ctx->setFrameColor (theme::kGridZero);
    ctx->drawLine (CPoint (bx (kMinIn), by (kMinIn)), CPoint (bx (kMaxIn), by (kMaxIn)));
}

void SunderView::draw (CDrawContext* ctx)
{
    const CRect all = getViewSize ();
    gridLayer.draw (ctx, all, 0, [this] (CDrawContext* c) { paintGrid (c); });
    ctx->setClipRect (all);
    const CRect r = responseRect ();

    // the output stage's response relative to 1 kHz: a solid line over a faint fill
    const StageParams sp = stageAt (stagePosition (host->plainValue (kDrive)), stagePosition (host->plainValue (kFeedback)));
    const double ref = std::abs (stageResponse (sp, 1000.0));
    if (auto path = owned (ctx->createGraphicsPath ()))
    {
        const double base = yOfDb (0.0);
        for (int i = 0; i <= kPoints; ++i)
        {
            const double f = kMinHz * std::pow (kMaxHz / kMinHz, (double)i / kPoints);
            const double db = 20.0 * std::log10 (std::max (1e-9, std::abs (stageResponse (sp, f)) / ref));
            const CPoint pt (xOfHz (f), yOfDb (db));
            if (i == 0)
            {
                path->beginSubpath (CPoint (pt.x, base));
                path->addLine (pt);
            }
            else
                path->addLine (pt);
        }
        path->addLine (CPoint (r.right, base));
        path->closeSubpath ();
        ctx->setFillColor (theme::withAlpha (theme::kCopper, 40));
        ctx->drawGraphicsPath (path, CDrawContext::kPathFilled);
        ctx->setLineWidth (1.5);
        ctx->setFrameColor (theme::kText);
        ctx->drawGraphicsPath (path, CDrawContext::kPathStroked);
        ctx->setLineWidth (1.0);
    }
    char buf[96];
    std::snprintf (buf, sizeof (buf), "OUTPUT STAGE   gain %+.1f dB", 20.0 * std::log10 (std::max (1e-9, ref)));
    text (ctx, buf, CRect (r.left + 36, r.top + 4, r.right - 8, r.top + 18), theme::kText, 10.5, kRightText, true);

    // the compression curve: out = in + gain reduction, with the detector's level now
    const CRect c = curveRect ();
    if (!c.isEmpty ())
    {
        auto bx = [&] (double db) { return c.left + (std::clamp (db, kMinIn, kMaxIn) - kMinIn) / (kMaxIn - kMinIn) * c.getWidth (); };
        auto by = [&] (double db) { return c.bottom - (std::clamp (db, kMinIn, kMaxIn) - kMinIn) / (kMaxIn - kMinIn) * c.getHeight (); };
        const int curve = (int)std::lround (host->plainValue (kCurve));
        const double ratio = host->plainValue (kRatio);
        if (auto path = owned (ctx->createGraphicsPath ()))
        {
            for (int i = 0; i <= 124; ++i)
            {
                const double in = kMinIn + (kMaxIn - kMinIn) * i / 124.0;
                const CPoint pt (bx (in), by (in + staticGrDb (curve, ratio, in)));
                if (i == 0)
                    path->beginSubpath (pt);
                else
                    path->addLine (pt);
            }
            ctx->setLineWidth (1.5);
            ctx->setFrameColor (theme::kText);
            ctx->drawGraphicsPath (path, CDrawContext::kPathStroked);
            ctx->setLineWidth (1.0);
        }
        text (ctx, "CURVE", CRect (c.left + 6, c.top + 4, c.right - 6, c.top + 18), theme::kTextDim, 9.5, kLeftText, true);
        if (live ())
        {
            const double in = shownLevel;
            const CPoint pt (bx (in), by (in + staticGrDb (curve, ratio, in)));
            // past the plot (a hot Input: up to +48 dB): a ring at its edge, and the level in the text
            const bool over = in > kMaxIn || in + staticGrDb (curve, ratio, in) > kMaxIn;
            ctx->setFillColor (theme::kEnergyLive);
            ctx->setFrameColor (theme::kEnergyLive);
            ctx->setLineWidth (1.5);
            ctx->drawEllipse (CRect (pt.x - 4, pt.y - 4, pt.x + 4, pt.y + 4), over ? kDrawStroked : kDrawFilled);
            ctx->setLineWidth (1.0);
            if (over)
                std::snprintf (buf, sizeof (buf), "IN %+.0f dB   GR %.1f dB", in, shownGr);
            else
                std::snprintf (buf, sizeof (buf), "GR %.1f dB", shownGr);
            text (ctx, buf, CRect (c.left + 6, c.bottom - 18, c.right - 6, c.bottom - 4), theme::kTextDim, 9.5, kRightText);
        }
    }
    ctx->resetClipRect ();
}

void SunderView::idle ()
{
    const Meters* m = meters ? meters () : nullptr;
    if (!m)
        return;
    const bool wasLive = live ();
    const uint32_t blocks = m->blocks.load (std::memory_order_acquire);
    if (blocks != lastBlocks)
    {
        lastBlocks = blocks;
        idleSinceBlock = 0;
    }
    else if (idleSinceBlock < (1 << 20))
        ++idleSinceBlock;
    bool changed = live () != wasLive;
    const float gr = std::min (m->grDb[0].load (std::memory_order_relaxed), m->grDb[1].load (std::memory_order_relaxed));
    const float level = m->levelDb.load (std::memory_order_relaxed);
    if (std::fabs (gr - shownGr) > 0.05f || std::fabs (level - shownLevel) > 0.1f)
    {
        shownGr = gr;
        shownLevel = level;
        changed = changed || live ();
    }
    if (changed)
        invalid ();
}

} // namespace sunder
