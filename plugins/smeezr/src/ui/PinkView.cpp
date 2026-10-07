#include "PinkView.h"

#include "pluginkit/ui/Theme.h"

#include "vstgui/lib/cdrawcontext.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace smeezr {

using namespace VSTGUI;
namespace theme = pk::theme;

namespace {
constexpr double kTitle = 18.0;
constexpr double kPad = 6.0;
constexpr double kLeftLabels = 28.0; // the dB labels left of the plots
constexpr double kFMin = 20.0, kFMax = 20000.0;
constexpr double kDbMin = -84.0, kDbMax = 0.0;   // the levels
constexpr double kGainRange = 24.0;              // the GAIN lane: +- this
inline float quant (float v, float step) { return std::round (v / step) * step; }

void text (CDrawContext* ctx, const std::string& s, const CRect& r, const CColor& c, double size, CHoriTxtAlign a = kLeftText,
           bool bold = false)
{
    ctx->setFont (theme::font (size, bold));
    ctx->setFontColor (c);
    ctx->drawString (s.c_str (), r, a, true);
}
// a band's edges (Hz)
double edgeLo (int b) { return b == 0 ? kFMin : Engine::crossover (b - 1); }
double edgeHi (int b) { return b == kBands - 1 ? kFMax : Engine::crossover (b); }
} // namespace

PinkView::PinkView (const CRect& r, MeterSource m) : CView (r), meters (std::move (m)) {}

CRect PinkView::lane () const
{
    const CRect all = getViewSize ();
    const double h = std::clamp (0.26 * all.getHeight (), 48.0, 70.0);
    return CRect (all.left + kLeftLabels, all.bottom - kPad - h, all.right - kPad, all.bottom - kPad);
}

CRect PinkView::levels () const
{
    const CRect all = getViewSize ();
    return CRect (all.left + kLeftLabels, all.top + kTitle, all.right - kPad, lane ().top - 14.0); // (the frequency labels under it)
}

double PinkView::xOf (double hz) const
{
    const CRect p = levels ();
    return p.left + p.getWidth () * std::log (std::clamp (hz, kFMin, kFMax) / kFMin) / std::log (kFMax / kFMin);
}

double PinkView::yLevel (double db) const
{
    const CRect p = levels ();
    return p.top + p.getHeight () * (kDbMax - std::clamp (db, kDbMin, kDbMax)) / (kDbMax - kDbMin);
}

double PinkView::yGain (double db) const
{
    const CRect l = lane ();
    return l.top + l.getHeight () * (kGainRange - std::clamp (db, -kGainRange, kGainRange)) / (2.0 * kGainRange);
}

void PinkView::idle ()
{
    const Meters* m = meters ? meters () : nullptr;
    if (!m)
        return;
    const uint32_t s = m->blocks.load (std::memory_order_acquire);
    if (s == seen)
        return;
    seen = s;
    constexpr auto rx = std::memory_order_relaxed;
    const bool a = m->active.load (rx);
    bool changed = a != active;
    active = a;
    if (!active)
    {
        if (changed)
            invalid (); // (once, to take the live part away)
        return;
    }
    auto take = [&] (float& v, float now, float step) {
        now = quant (now, step);
        changed = changed || now != v;
        v = now;
    };
    take (pink, m->pink.load (rx), 0.01f);
    take (ott, m->ott.load (rx), 0.01f);
    take (target, m->targetDb.load (rx), 0.5f);
    for (int b = 0; b < kBands; ++b)
    {
        take (level[b], m->levelDb[(size_t)b].load (rx), 0.5f);
        take (pinkDb[b], m->pinkDb[(size_t)b].load (rx), 0.25f);
        take (ottDb[b], m->ottDb[(size_t)b].load (rx), 0.25f);
    }
    if (changed)
        invalid ();
}

void PinkView::paintBase (CDrawContext* ctx)
{
    const CRect all = getViewSize ();
    ctx->setFillColor (theme::kWell);
    ctx->drawRect (all, kDrawFilled);
    const CRect p = levels (), l = lane ();
    ctx->setLineWidth (1.0);

    // the band borders (one octave each) through both plots
    for (int b = 0; b < kXovers; ++b)
    {
        const double x = std::round (xOf (Engine::crossover (b))) + 0.5;
        ctx->setFrameColor (theme::kGridMinor);
        ctx->drawLine (CPoint (x, p.top), CPoint (x, p.bottom));
        ctx->drawLine (CPoint (x, l.top), CPoint (x, l.bottom));
    }
    // the levels' dB lines
    const double step = p.getHeight () < 140.0 ? 24.0 : 12.0; // (a short plot: a line every 24 dB)
    for (double db = -step; db > kDbMin; db -= step)
    {
        ctx->setFrameColor (theme::kGridMinor);
        const double y = std::round (yLevel (db)) + 0.5;
        ctx->drawLine (CPoint (p.left, y), CPoint (p.right, y));
        char buf[16];
        std::snprintf (buf, sizeof (buf), "%.0f", db);
        text (ctx, buf, CRect (all.left + 2.0, y - 6.0, p.left - 3.0, y + 6.0), theme::kTextDim, 8.5, kRightText);
    }
    ctx->setFrameColor (theme::kLineDim);
    ctx->drawRect (p, kDrawStroked);
    for (double f : {100.0, 1000.0, 10000.0})
    {
        const char* s = f == 100.0 ? "100" : f == 1000.0 ? "1k" : "10k";
        text (ctx, s, CRect (xOf (f) - 20.0, p.bottom + 1.0, xOf (f) + 20.0, p.bottom + 13.0), theme::kTextDim, 9.0, kCenterText);
    }
    // the GAIN lane: 0 and +- 12 dB
    for (double db : {12.0, 0.0, -12.0})
    {
        ctx->setFrameColor (db == 0.0 ? theme::kGridZero : theme::kGridMinor);
        const double y = std::round (yGain (db)) + 0.5;
        ctx->drawLine (CPoint (l.left, y), CPoint (l.right, y));
        char buf[16];
        std::snprintf (buf, sizeof (buf), "%+.0f", db);
        text (ctx, db == 0.0 ? "0" : buf, CRect (all.left + 2.0, y - 6.0, l.left - 3.0, y + 6.0), theme::kTextDim, 8.5, kRightText);
    }
    ctx->setFrameColor (theme::kLineDim);
    ctx->drawRect (l, kDrawStroked);
    text (ctx, "GAIN", CRect (l.left + 4.0, l.top + 2.0, l.left + 60.0, l.top + 13.0), theme::kCopperPale, 9.0, kLeftText, true);

    text (ctx, "PINK BALANCE", CRect (all.left + kPad, all.top + 3.0, all.left + 200.0, all.top + 17.0), theme::kCopperPale, 10.0,
          kLeftText, true);
    // the legend
    const double lx = all.right - kPad - 190.0;
    ctx->setFillColor (theme::withAlpha (theme::kCopper, 110));
    ctx->drawRect (CRect (lx, all.top + 6.0, lx + 10.0, all.top + 14.0), kDrawFilled);
    text (ctx, "level", CRect (lx + 13.0, all.top + 3.0, lx + 60.0, all.top + 17.0), theme::kTextDim, 9.0);
    ctx->setFrameColor (theme::kEnergyLive);
    ctx->setLineWidth (1.5);
    ctx->drawLine (CPoint (lx + 62.0, all.top + 10.5), CPoint (lx + 74.0, all.top + 10.5));
    ctx->setLineWidth (1.0);
    text (ctx, "pink target", CRect (lx + 77.0, all.top + 3.0, lx + 140.0, all.top + 17.0), theme::kTextDim, 9.0);
    ctx->setFrameColor (theme::kCopperPale);
    ctx->setLineWidth (2.0);
    ctx->drawLine (CPoint (lx + 142.0, all.top + 10.5), CPoint (lx + 152.0, all.top + 10.5));
    ctx->setLineWidth (1.0);
    text (ctx, "after", CRect (lx + 155.0, all.top + 3.0, lx + 190.0, all.top + 17.0), theme::kTextDim, 9.0);
}

void PinkView::draw (CDrawContext* ctx)
{
    const CRect all = getViewSize ();
    pk::LayerKey key; // (the base depends on nothing but the view's size, which the layer follows itself)
    key.add (1);
    baseLayer.draw (ctx, all, key, [this] (CDrawContext* c) { paintBase (c); });

    ctx->setClipRect (all);
    if (active)
    {
        const CRect p = levels ();
        const double y0 = std::round (yGain (0.0));
        for (int b = 0; b < kBands; ++b)
        {
            const double x0 = std::round (xOf (edgeLo (b))) + 3.0, x1 = std::round (xOf (edgeHi (b))) - 2.0;
            // the measured level
            if (level[b] > kDbMin)
            {
                ctx->setFillColor (theme::withAlpha (theme::kCopper, 110));
                ctx->drawRect (CRect (x0, std::round (yLevel (level[b])), x1, p.bottom - 1.0), kDrawFilled);
            }
            // where the gains put it
            const double after = level[b] + pinkDb[b] + ottDb[b];
            if (after > kDbMin && level[b] > kDbMin)
            {
                ctx->setFrameColor (theme::kCopperPale);
                ctx->setLineWidth (2.0);
                const double y = std::round (yLevel (after)) + 0.5;
                ctx->drawLine (CPoint (x0, y), CPoint (x1, y));
                ctx->setLineWidth (1.0);
            }
            // the gains: the pink stage's bar, the total (with the OTT stage) a cinnabar mark
            const double yp = std::round (yGain (pinkDb[b]));
            if (std::fabs (yp - y0) >= 1.0)
            {
                ctx->setFillColor (theme::withAlpha (theme::kCopper, 150));
                ctx->drawRect (CRect (x0, std::min (yp, y0), x1, std::max (yp, y0)), kDrawFilled);
            }
            if (ott > 0.0f)
            {
                ctx->setFrameColor (theme::kEnergyLive);
                ctx->setLineWidth (2.0);
                const double yt = std::round (yGain (pinkDb[b] + ottDb[b])) + 0.5;
                ctx->drawLine (CPoint (x0, yt), CPoint (x1, yt));
                ctx->setLineWidth (1.0);
            }
        }
        // the pink target: every band's equal share of the power
        if (target > kDbMin && pink > 0.0f)
        {
            ctx->setFrameColor (theme::kEnergyLive);
            ctx->setLineWidth (1.5);
            const double y = std::round (yLevel (target)) + 0.5;
            ctx->drawLine (CPoint (p.left + 1.0, y), CPoint (p.right - 1.0, y));
            ctx->setLineWidth (1.0);
        }
    }
    ctx->resetClipRect ();
    setDirty (false);
}

} // namespace smeezr
