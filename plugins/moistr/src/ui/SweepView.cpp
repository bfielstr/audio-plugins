#include "SweepView.h"

#include "Sweep.h"

#include "pluginkit/ui/Theme.h"

#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cgraphicspath.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace moistr {

using namespace VSTGUI;
namespace theme = pk::theme;

namespace {
constexpr double kTitle = 18.0, kPad = 6.0, kOrbitW = 104.0;
constexpr double kFMin = 20.0, kFMax = 5000.0, kDbMin = -24.0, kDbMax = 24.0;
constexpr uint32_t kLowIds[3] = {kALow, kBLow, kShelfLow}, kHighIds[3] = {kAHigh, kBHigh, kShelfHigh};

void text (CDrawContext* ctx, const std::string& s, const CRect& r, const CColor& c, double size, CHoriTxtAlign a = kLeftText,
           bool bold = false)
{
    ctx->setFont (theme::font (size, bold));
    ctx->setFontColor (c);
    ctx->drawString (s.c_str (), r, a, true);
}

// the corner at u (0 .. 1 across Low .. High, log)
double cornerAt (double u, double lo, double hi) { return lo * std::pow (hi / lo, u); }
} // namespace

bool SweepSnapshot::operator== (const SweepSnapshot& o) const
{
    return hz[0] == o.hz[0] && hz[1] == o.hz[1] && hz[2] == o.hz[2] && shelfDb == o.shelfDb && amount == o.amount &&
           shelfAmount == o.shelfAmount;
}

SweepView::SweepView (const CRect& r, pk::ParamHost* h, MeterSource m) : CView (r), host (h), meters (std::move (m))
{
    snap = snapshot (nullptr, host);
}

double SweepView::bellDb (double hz, double centre, double gainDb, double q)
{
    const double w = hz / centre, a = std::pow (10.0, gainDb / 40.0), re = 1.0 - w * w;
    const double num = re * re + (w * a / q) * (w * a / q), den = re * re + (w / (a * q)) * (w / (a * q));
    return 10.0 * std::log10 (num / den);
}

double SweepView::shelfDb (double hz, double corner, double gainDb, double q)
{
    // RBJ's high shelf: A (A s^2 + sqrt (A) / Q s + 1) / (s^2 + sqrt (A) / Q s + A)
    const double w = hz / corner, a = std::pow (10.0, gainDb / 40.0), im = std::sqrt (a) * w / q;
    const double n = 1.0 - a * w * w, d = a - w * w;
    return 10.0 * std::log10 (a * a * (n * n + im * im) / (d * d + im * im));
}

bool SweepView::shows (uint32_t id)
{
    return (id >= kSweep && id <= kShelfTilt) || id == kSeed;
}

SweepSnapshot SweepView::snapshot (const Meters* m, pk::ParamHost* host, const SweepSnapshot* held)
{
    SweepSnapshot s;
    auto plain = [host] (uint32_t id) { return host->plainValue (id); };
    // (before the engine runs: the bells at Low (Phase 0 starts there) and the shelf half way, at its middle gain)
    s.hz[0] = plain (kALow);
    s.hz[1] = plain (kBLow);
    s.hz[2] = std::sqrt (plain (kShelfLow) * plain (kShelfHigh));
    s.shelfDb = 0.5 * (plain (kShelfMin) + shelfCeilingDb (s.hz[2], plain (kShelfMin), plain (kShelfMax), plain (kShelfTilt)));
    s.amount = plain (kSweep) >= 0.5 ? 1.0 : 0.0;
    s.shelfAmount = plain (kShelf) >= 0.5 ? 1.0 : 0.0;
    if (m && m->blocks.load (std::memory_order_relaxed) > 0)
    {
        constexpr auto rx = std::memory_order_relaxed;
        if (held && !m->active.load (rx) && held->hz[0] > 0.0)
        {
            SweepSnapshot h = *held; // (quiet: the curves hold; only the switches' fades follow)
            h.amount = std::round (m->sweepAmount.load (rx) * 100.0f) / 100.0;
            h.shelfAmount = std::round (m->shelfAmount.load (rx) * 100.0f) / 100.0;
            return h;
        }
        for (int b = 0; b < 3; ++b)
            s.hz[b] = m->sweepHz[(size_t)b].load (rx);
        s.shelfDb = m->shelfDb.load (rx);
        s.amount = m->sweepAmount.load (rx);
        s.shelfAmount = m->shelfAmount.load (rx);
    }
    for (double& f : s.hz)
        f = std::round (f * 10.0) / 10.0;
    s.shelfDb = std::round (s.shelfDb * 20.0) / 20.0;
    s.amount = std::round (s.amount * 100.0) / 100.0;
    s.shelfAmount = std::round (s.shelfAmount * 100.0) / 100.0;
    return s;
}

CRect SweepView::plot () const
{
    const CRect all = getViewSize ();
    return CRect (all.left + kPad, all.top + kTitle, all.right - kPad - kOrbitW - kPad, all.bottom - kPad - 12.0);
}

CRect SweepView::orbit () const
{
    const CRect all = getViewSize (), p = plot ();
    return CRect (all.right - kPad - kOrbitW, p.top, all.right - kPad, all.bottom - kPad - 12.0);
}

double SweepView::xOf (double hz) const
{
    const CRect p = plot ();
    return p.left + p.getWidth () * std::log (std::clamp (hz, kFMin, kFMax) / kFMin) / std::log (kFMax / kFMin);
}

double SweepView::yOf (double db) const
{
    const CRect p = plot ();
    return p.top + p.getHeight () * (kDbMax - std::clamp (db, kDbMin, kDbMax)) / (kDbMax - kDbMin);
}

void SweepView::idle ()
{
    const SweepSnapshot s = snapshot (meters ? meters () : nullptr, host, &snap);
    if (s != snap)
    {
        snap = s;
        invalid ();
    }
}

void SweepView::paintBase (CDrawContext* ctx)
{
    const CRect all = getViewSize ();
    ctx->setFillColor (theme::kWell);
    ctx->drawRect (all, kDrawFilled);
    const CRect p = plot (), o = orbit ();
    auto plain = [this] (uint32_t id) { return host->plainValue (id); };

    // the grid: decades and their steps, 0 / +-12 dB
    ctx->setLineWidth (1.0);
    for (double dec = 10.0; dec < kFMax; dec *= 10.0)
        for (int k = 1; k <= 9; ++k)
        {
            const double f = dec * k;
            if (f < kFMin || f > kFMax)
                continue;
            ctx->setFrameColor (k == 1 ? theme::kGridMajor : theme::kGridMinor);
            const double x = std::round (xOf (f)) + 0.5;
            ctx->drawLine (CPoint (x, p.top), CPoint (x, p.bottom));
        }
    for (double db : {12.0, 0.0, -12.0})
    {
        ctx->setFrameColor (db == 0.0 ? theme::kGridZero : theme::kGridMinor);
        const double y = std::round (yOf (db)) + 0.5;
        ctx->drawLine (CPoint (p.left, y), CPoint (p.right, y));
    }
    for (double f : {50.0, 100.0, 500.0, 1000.0, 5000.0})
    {
        char buf[16];
        std::snprintf (buf, sizeof (buf), f >= 1000.0 ? "%.0fk" : "%.0f", f >= 1000.0 ? f / 1000.0 : f);
        const double x = std::min (xOf (f), p.right - 12.0);
        text (ctx, buf, CRect (x - 16.0, p.bottom + 1.0, x + 16.0, p.bottom + 12.0), theme::kTextDim, 9.0, kCenterText);
    }
    for (double db : {12.0, -12.0})
        text (ctx, db > 0 ? "+12" : "-12", CRect (p.right - 30.0, yOf (db) - 11.0, p.right - 2.0, yOf (db) - 1.0), theme::kTextDim, 8.5,
              kRightText);

    // where each bell sweeps (bars at the bottom) and the shelf's corner goes (at the top)
    const bool on = plain (kSweep) >= 0.5;
    for (int b = 0; b < 3; ++b)
    {
        if (b == 2 && plain (kShelf) < 0.5)
            continue;
        const double lo = std::min (plain (kLowIds[b]), plain (kHighIds[b])), hi = std::max (plain (kLowIds[b]), plain (kHighIds[b]));
        const double x0 = xOf (lo), x1 = std::max (xOf (hi), x0 + 2.0);
        const double y = b == 2 ? p.top + 2.0 : p.bottom - 4.0 - 5.0 * b;
        ctx->setFillColor (theme::withAlpha (b == 0 ? theme::kCopper : theme::kCopperPale, on ? 150 : 60));
        ctx->drawRect (CRect (x0, y, x1, y + 3.0), kDrawFilled);
    }
    text (ctx, "SWEEP", CRect (all.left + kPad, all.top + 3.0, all.left + 120.0, all.top + 17.0), theme::kCopperPale, 10.0, kLeftText, true);

    // the orbit's box: the corner across (Low .. High), the gain up (Min .. Max), and the ceiling Tilt sets
    ctx->setFrameColor (theme::kGridMajor);
    ctx->drawRect (o, kDrawStroked);
    const double lo = plain (kShelfLow), hi = plain (kShelfHigh);
    const double gMin = std::min (plain (kShelfMin), plain (kShelfMax)), gMax = std::max (plain (kShelfMin), plain (kShelfMax));
    if (gMax > gMin && hi > lo)
    {
        auto oy = [&] (double db) { return o.bottom - o.getHeight () * (db - gMin) / (gMax - gMin); };
        if (gMin < 0.0 && gMax > 0.0)
        {
            ctx->setFrameColor (theme::kGridZero);
            const double y = std::round (oy (0.0)) + 0.5;
            ctx->drawLine (CPoint (o.left, y), CPoint (o.right, y));
        }
        auto path = owned (ctx->createGraphicsPath ());
        if (path)
        {
            for (int i = 0; i <= 32; ++i)
            {
                const double u = i / 32.0;
                const CPoint pt (o.left + o.getWidth () * u, oy (shelfCeilingDb (cornerAt (u, lo, hi), gMin, gMax, plain (kShelfTilt))));
                i == 0 ? path->beginSubpath (pt) : path->addLine (pt);
            }
            ctx->setFrameColor (theme::kLineDim);
            ctx->setLineStyle (theme::dashed ());
            ctx->drawGraphicsPath (path, CDrawContext::kPathStroked);
            ctx->setLineStyle (kLineSolid);
        }
    }
    text (ctx, "SHELF ORBIT", CRect (o.left, all.top + 3.0, o.right, all.top + 17.0), theme::kCopperPale, 9.0, kCenterText, true);
    auto hzShort = [] (double hz) {
        char buf[16];
        if (hz >= 1000.0)
            std::snprintf (buf, sizeof (buf), "%.1fk", hz / 1000.0);
        else
            std::snprintf (buf, sizeof (buf), "%.0f", hz);
        return std::string (buf);
    };
    text (ctx, hzShort (lo), CRect (o.left, o.bottom + 1.0, o.left + 50.0, o.bottom + 12.0), theme::kTextDim, 8.5, kLeftText);
    text (ctx, hzShort (hi), CRect (o.right - 50.0, o.bottom + 1.0, o.right, o.bottom + 12.0), theme::kTextDim, 8.5, kRightText);
}

void SweepView::draw (CDrawContext* ctx)
{
    const CRect all = getViewSize ();
    pk::LayerKey key;
    for (uint32_t id = kSweep; id <= kShelfTilt; ++id)
        key.add (host->plainValue (id));
    baseLayer.draw (ctx, all, key, [this] (CDrawContext* c) { paintBase (c); });

    ctx->setClipRect (all);
    auto plain = [this] (uint32_t id) { return host->plainValue (id); };
    const CRect p = plot (), o = orbit ();
    const bool live = snap.amount > 0.0;
    const double gains[2] = {plain (kAGain) * snap.amount, plain (kBGain) * snap.amount};
    const double qs[3] = {plain (kAWidth), plain (kBWidth), plain (kShelfQ)};
    const double shelfGain = snap.shelfDb * snap.amount * snap.shelfAmount;
    // each filter's curve (thin) and the whole stage's (bold, the sum in dB)
    const int n = std::max (2, (int)p.getWidth () / 2);
    auto curve = [&] (int which) {
        auto path = owned (ctx->createGraphicsPath ());
        if (!path)
            return path;
        for (int i = 0; i <= n; ++i)
        {
            const double x = p.left + p.getWidth () * i / n;
            const double hz = kFMin * std::pow (kFMax / kFMin, (x - p.left) / p.getWidth ());
            double db = 0.0;
            if (which == 0 || which == 3)
                db += bellDb (hz, snap.hz[0], gains[0], qs[0]);
            if (which == 1 || which == 3)
                db += bellDb (hz, snap.hz[1], gains[1], qs[1]);
            if ((which == 2 || which == 3) && snap.shelfAmount > 0.0)
                db += shelfDb (hz, snap.hz[2], shelfGain, qs[2]);
            const CPoint pt (x, yOf (db));
            i == 0 ? path->beginSubpath (pt) : path->addLine (pt);
        }
        return path;
    };
    if (snap.hz[0] > 0.0 && snap.hz[1] > 0.0 && snap.hz[2] > 0.0)
    {
        for (int w = 0; w < 3; ++w)
        {
            if (w == 2 && snap.shelfAmount <= 0.0)
                continue;
            if (auto path = curve (w))
            {
                ctx->setFrameColor (theme::withAlpha (w == 0 ? theme::kCopper : theme::kCopperPale, live ? 170 : 70));
                if (w == 1)
                    ctx->setLineStyle (theme::dashed ());
                ctx->drawGraphicsPath (path, CDrawContext::kPathStroked);
                ctx->setLineStyle (kLineSolid);
            }
        }
        if (auto path = curve (3))
        {
            ctx->setFrameColor (live ? theme::kEnergyLive : theme::kLineDim);
            ctx->setLineWidth (live ? 2.0 : 1.5);
            ctx->drawGraphicsPath (path, CDrawContext::kPathStroked);
            ctx->setLineWidth (1.0);
        }
    }
    // the shelf on its orbit: a dot
    const double lo = plain (kShelfLow), hi = plain (kShelfHigh);
    const double gMin = std::min (plain (kShelfMin), plain (kShelfMax)), gMax = std::max (plain (kShelfMin), plain (kShelfMax));
    if (snap.shelfAmount > 0.0 && hi > lo && gMax > gMin && snap.hz[2] > 0.0)
    {
        const double u = std::clamp (std::log (snap.hz[2] / lo) / std::log (hi / lo), 0.0, 1.0);
        const double v = std::clamp ((snap.shelfDb - gMin) / (gMax - gMin), 0.0, 1.0);
        const double x = o.left + o.getWidth () * u, y = o.bottom - o.getHeight () * v;
        ctx->setFillColor (live ? theme::kEnergyLive : theme::kTextDim);
        ctx->drawEllipse (CRect (x - 3.5, y - 3.5, x + 3.5, y + 3.5), kDrawFilled);
    }
    if (live)
    {
        char buf[48];
        std::snprintf (buf, sizeof (buf), "SHELF %.0f Hz %+.1f dB", snap.hz[2], snap.shelfDb);
        if (snap.shelfAmount > 0.0)
            text (ctx, buf, CRect (p.right - 190.0, all.top + 3.0, p.right, all.top + 17.0), theme::kText, 9.5, kRightText);
    }
    ctx->resetClipRect ();
    setDirty (false);
}

} // namespace moistr
