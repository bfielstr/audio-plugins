#include "OrbView.h"

#include "pluginkit/ui/Theme.h"

#include "vstgui/lib/cdrawcontext.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace orbitr {

using namespace VSTGUI;
namespace theme = pk::theme;

namespace {
const CColor kOrbColor (150, 130, 255);

void text (CDrawContext* ctx, const std::string& s, const CRect& r, const CColor& c, double size, CHoriTxtAlign a = kLeftText,
           bool bold = false)
{
    ctx->setFont (theme::font (size, bold));
    ctx->setFontColor (c);
    ctx->drawString (s.c_str (), r, a, true);
}
} // namespace

OrbView::OrbView (const CRect& r, pk::ParamHost* h, MeterSource m) : CView (r), host (h), meters (std::move (m)) {}

void OrbView::idle ()
{
    const Meters* m = meters ? meters () : nullptr;
    if (!m)
        return;
    const uint32_t s = m->blocks.load (std::memory_order_acquire);
    if (s == seen)
        return;
    seen = s;
    orbs = std::clamp (m->orbs.load (std::memory_order_relaxed), 0, Motion::kMaxOrbs);
    distance = m->distance.load (std::memory_order_relaxed);
    radius = m->radius.load (std::memory_order_relaxed);
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
    ctx->setFillColor (theme::kWaveBg);
    ctx->drawRect (all, kDrawFilled);
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
    ctx->setFrameColor (theme::kText);
    ctx->drawLine (CPoint (cx, ly - 6), CPoint (cx, ly - 11));
    for (int k = 0; k < orbs; ++k)
    {
        // the trail, fading
        for (int t = 1; t < kTrail; ++t)
        {
            const int i0 = (trailPos - t + kTrail) % kTrail, i1 = (trailPos - t + 1 + kTrail) % kTrail;
            if (tx[k][i0] == 0.0f && ty[k][i0] == 0.0f)
                continue;
            CColor tc = kOrbColor;
            tc.alpha = (uint8_t)(160 * (kTrail - t) / kTrail);
            ctx->setFrameColor (tc);
            ctx->setLineWidth (2.0);
            ctx->drawLine (CPoint (px (tx[k][i0]), py (ty[k][i0])), CPoint (px (tx[k][i1]), py (ty[k][i1])));
        }
        ctx->setFillColor (kOrbColor);
        ctx->drawEllipse (CRect (px (x[k]) - 4, py (y[k]) - 4, px (x[k]) + 4, py (y[k]) + 4), kDrawFilled);
    }
    char buf[96];
    std::snprintf (buf, sizeof (buf), "ORBS FROM ABOVE  %d, %s, %.0f m/s", orbs, host->plainValue (kPattern) >= 0.5 ? "swarm" : "orbit",
                   host->plainValue (kSpeed));
    text (ctx, buf, CRect (all.left + 6, all.top + 3, all.right - 6, all.top + 17), theme::kTextBright, 10.0, kLeftText, true);
    text (ctx, "you", CRect (cx + 12, ly - 6, cx + 60, ly + 8), theme::kTextDim, 9.0);
    ctx->resetClipRect ();
    setDirty (false);
}

} // namespace orbitr
