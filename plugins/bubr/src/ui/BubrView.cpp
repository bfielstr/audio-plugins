#include "BubrView.h"

#include "pluginkit/ui/Theme.h"

#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cframe.h"
#include "vstgui/lib/cgraphicspath.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace bubr {

using namespace VSTGUI;
namespace theme = pk::theme;

namespace {
constexpr int kPoints = 200; // along the frequency axis

void text (CDrawContext* ctx, const std::string& s, const CRect& r, const CColor& c, double size, CHoriTxtAlign a = kCenterText,
           bool bold = false)
{
    ctx->setFont (theme::font (size, bold));
    ctx->setFontColor (c);
    ctx->drawString (s.c_str (), r, a, true);
}
// the gate's static gain (dB) for an input peak (dB)
double gateDbAt (double in, double thr, double ratio)
{
    if (in >= thr)
        return 0.0;
    return std::max (-100.0, (ratio - 1.0) * (in - thr));
}
} // namespace

BubrView::BubrView (const CRect& r, pk::ParamHost* h, MeterSource m) : CView (r), host (h), meters (std::move (m)) {}

CRect BubrView::gateRect () const
{
    const CRect all = getViewSize ();
    const double side = all.getHeight () - 16.0;
    if (all.getWidth () < side + 300.0)
        return CRect ();
    return CRect (all.right - side - 8.0, all.top + 8.0, all.right - 8.0, all.bottom - 8.0);
}

CRect BubrView::responseRect () const
{
    const CRect all = getViewSize ();
    const CRect c = gateRect ();
    return c.isEmpty () ? all : CRect (all.left, all.top, c.left - 10.0, all.bottom);
}

double BubrView::xOfHz (double hz) const
{
    const CRect r = responseRect ();
    return r.left + std::log (std::clamp (hz, kMinHz, kMaxHz) / kMinHz) / std::log (kMaxHz / kMinHz) * r.getWidth ();
}

double BubrView::yOfDb (double db) const
{
    const CRect r = responseRect ();
    return r.top + (kMaxDb - std::clamp (db, kMinDb, kMaxDb)) / (kMaxDb - kMinDb) * (r.getHeight () - kScaleH);
}

void BubrView::paintGrid (CDrawContext* ctx)
{
    const CRect all = getViewSize ();
    ctx->setFillColor (theme::kWell);
    ctx->drawRect (all, kDrawFilled);
    ctx->setLineWidth (1.0);
    const CRect r = responseRect ();
    const double plotBottom = r.bottom - kScaleH;
    const double freqs[] = {20.0, 30.0, 50.0, 100.0, 200.0, 300.0, 500.0};
    const char* labels[] = {"20", "30", "50", "100", "200", "300", "500"};
    for (int i = 0; i < 7; ++i)
    {
        const double x = xOfHz (freqs[i]);
        ctx->setFrameColor (freqs[i] == 100.0 ? theme::kGridMajor : theme::kGridMinor);
        ctx->drawLine (CPoint (x, r.top), CPoint (x, plotBottom));
        text (ctx, labels[i], CRect (x - 20, plotBottom + 1, x + 20, r.bottom - 1), theme::kTextDim, 9.5);
    }
    for (double db = -24.0; db <= 48.0; db += 12.0)
    {
        const bool major = db == 0.0 || db == 24.0 || db == 48.0;
        ctx->setFrameColor (db == 0.0 ? theme::kGridZero : (major ? theme::kGridMajor : theme::kGridMinor));
        ctx->drawLine (CPoint (r.left, yOfDb (db)), CPoint (r.right, yOfDb (db)));
        char buf[8];
        std::snprintf (buf, sizeof (buf), db > 0.0 ? "+%.0f" : "%.0f", db);
        text (ctx, buf, CRect (r.left + 3, yOfDb (db) - 12, r.left + 31, yOfDb (db)), theme::kTextDim, 9.0, kLeftText);
    }
    // the gate's box: its grid every 12 dB and the unity line
    const CRect c = gateRect ();
    if (c.isEmpty ())
        return;
    ctx->setFrameColor (theme::kGridMajor);
    ctx->drawRect (c, kDrawStroked);
    auto bx = [&] (double db) { return c.left + (db - kMinIn) / (kMaxIn - kMinIn) * c.getWidth (); };
    auto by = [&] (double db) { return c.bottom - (db - kMinIn) / (kMaxIn - kMinIn) * c.getHeight (); };
    ctx->setFrameColor (theme::kGridMinor);
    for (double db = -60.0; db < 0.0; db += 12.0)
    {
        ctx->drawLine (CPoint (bx (db), c.top), CPoint (bx (db), c.bottom));
        ctx->drawLine (CPoint (c.left, by (db)), CPoint (c.right, by (db)));
    }
    ctx->setFrameColor (theme::kGridZero);
    ctx->drawLine (CPoint (bx (kMinIn), by (kMinIn)), CPoint (bx (kMaxIn), by (kMaxIn)));
}

void BubrView::draw (CDrawContext* ctx)
{
    const CRect all = getViewSize ();
    gridLayer.draw (ctx, all, 0, [this] (CDrawContext* c) { paintGrid (c); });
    ctx->setClipRect (all);
    const CRect r = responseRect ();

    // the effect's response: a solid line over a faint fill
    ParamArray p = defaultParams ();
    for (uint32_t id = 0; id < kNumParams; ++id)
        p[id] = host->plainValue (id);
    const double rate = 48000.0;
    if (auto path = owned (ctx->createGraphicsPath ()))
    {
        const double base = yOfDb (kMinDb);
        for (int i = 0; i <= kPoints; ++i)
        {
            const double f = kMinHz * std::pow (kMaxHz / kMinHz, (double)i / kPoints);
            const double db = 20.0 * std::log10 (std::max (1e-9, std::abs (wetResponse (p, f, rate))));
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
    // Freq and Low Cut
    const double plotBottom = r.bottom - kScaleH;
    ctx->setFrameColor (theme::kCopper);
    ctx->drawLine (CPoint (xOfHz (p[kFreq]), r.top + 20), CPoint (xOfHz (p[kFreq]), plotBottom));
    ctx->setFrameColor (theme::kTextDim);
    ctx->drawLine (CPoint (xOfHz (p[kLowCut]), r.top + 20), CPoint (xOfHz (p[kLowCut]), plotBottom));
    char buf[96];
    std::snprintf (buf, sizeof (buf), "EFFECT   %.0f Hz   peak %+.1f dB", p[kFreq],
                   20.0 * std::log10 (std::max (1e-9, std::abs (wetResponse (p, p[kFreq], rate)))));
    text (ctx, buf, CRect (r.left + 36, r.top + 4, r.right - 8, r.top + 18), theme::kText, 10.5, kRightText, true);

    // the gate's curve: the band's level out against the input's peak, with the level now
    const CRect c = gateRect ();
    if (!c.isEmpty ())
    {
        auto bx = [&] (double db) { return c.left + (std::clamp (db, kMinIn, kMaxIn) - kMinIn) / (kMaxIn - kMinIn) * c.getWidth (); };
        auto by = [&] (double db) { return c.bottom - (std::clamp (db, kMinIn, kMaxIn) - kMinIn) / (kMaxIn - kMinIn) * c.getHeight (); };
        const double thr = p[kGate], ratio = p[kRatio];
        if (auto path = owned (ctx->createGraphicsPath ()))
        {
            for (int i = 0; i <= 144; ++i)
            {
                const double in = kMinIn + (kMaxIn - kMinIn) * i / 144.0;
                const CPoint pt (bx (in), by (in + gateDbAt (in, thr, ratio)));
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
        text (ctx, "GATE", CRect (c.left + 6, c.top + 4, c.right - 6, c.top + 18), theme::kTextDim, 9.5, kLeftText, true);
        if (live ())
        {
            const double in = shownIn;
            const CPoint pt (bx (in), by (in + gateDbAt (in, thr, ratio)));
            ctx->setFillColor (theme::kEnergyLive);
            ctx->drawEllipse (CRect (pt.x - 4, pt.y - 4, pt.x + 4, pt.y + 4), kDrawFilled);
            std::snprintf (buf, sizeof (buf), "gate %.1f dB   limit %.1f dB", shownGate, shownLimit);
            text (ctx, buf, CRect (c.left + 6, c.bottom - 18, c.right - 6, c.bottom - 4), theme::kTextDim, 9.5, kRightText);
        }
    }
    ctx->resetClipRect ();
}

void BubrView::idle ()
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
    const float in = m->inDb.load (std::memory_order_relaxed);
    const float g = m->gateDb.load (std::memory_order_relaxed);
    const float l = m->limitDb.load (std::memory_order_relaxed);
    if (std::fabs (in - shownIn) > 0.1f || std::fabs (g - shownGate) > 0.05f || std::fabs (l - shownLimit) > 0.05f)
    {
        shownIn = in;
        shownGate = g;
        shownLimit = l;
        changed = changed || live ();
    }
    if (changed)
        invalid ();
}

} // namespace bubr
