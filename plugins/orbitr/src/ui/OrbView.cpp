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
// the orbs are the sound sources, the one lit thing in the view: cinnabar, their trails fading out
const CColor kOrbColor = theme::kEnergyLive;

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
    grains = m->grains.load (std::memory_order_relaxed);
    trailPos = (trailPos + 1) % kTrail;
    for (int k = 0; k < orbs; ++k)
    {
        x[k] = m->orbX[(size_t)k].load (std::memory_order_relaxed);
        y[k] = m->orbY[(size_t)k].load (std::memory_order_relaxed);
        grain[k] = m->orbGrain[(size_t)k].load (std::memory_order_relaxed);
        tx[k][trailPos] = x[k];
        ty[k][trailPos] = y[k];
    }
    invalid ();
}

double OrbView::centreX () const { return (getViewSize ().left + getViewSize ().right) / 2; }
double OrbView::listenerY () const { return getViewSize ().bottom - 26; }

double OrbView::scale () const
{
    const CRect all = getViewSize ();
    const double span = std::max (1.0, (double)distance + (double)radius * 1.15);
    return std::min ((listenerY () - all.top - 26) / span, (all.getWidth () / 2 - 12) / std::max (1.0, (double)radius * 1.15));
}

void OrbView::paintBase (CDrawContext* ctx)
{
    const CRect all = getViewSize ();
    ctx->setFillColor (theme::kWell);
    ctx->drawRect (all, kDrawFilled);
    const double cx = centreX (), ly = listenerY ();
    const double span = std::max (1.0, (double)distance + (double)radius * 1.15);
    const double scale = this->scale ();
    auto px = [&] (double mx) { return cx + mx * scale; };
    auto py = [&] (double my) { return ly - my * scale; };
    // metre rings round the listener
    ctx->setLineWidth (1.0);
    for (double m = 1.0; m <= span + 0.5; m += (span > 8.0 ? 5.0 : 1.0))
    {
        ctx->setFrameColor (theme::kGridMajor);
        ctx->drawEllipse (CRect (px (-m), py (m), px (m), py (-m)), kDrawStroked);
    }
    // the swarm's ball: a dashed copper contour
    ctx->setFrameColor (theme::kCopper);
    ctx->setLineStyle (theme::dashed ());
    ctx->drawEllipse (CRect (px (-radius), py (distance + radius), px (radius), py (distance - radius)), kDrawStroked);
    ctx->setLineStyle (kLineSolid);
    // the listener (a head with ears, facing up), drawn as an outline in the text colour
    ctx->setFrameColor (theme::kText);
    ctx->drawEllipse (CRect (cx - 6, ly - 6, cx + 6, ly + 6), kDrawStroked);
    ctx->drawLine (CPoint (cx - 9, ly - 2), CPoint (cx - 9, ly + 2));
    ctx->drawLine (CPoint (cx + 9, ly - 2), CPoint (cx + 9, ly + 2));
    ctx->drawLine (CPoint (cx, ly - 6), CPoint (cx, ly - 11));
}

void OrbView::draw (CDrawContext* ctx)
{
    const CRect all = getViewSize ();
    baseLayer.draw (ctx, all, pk::LayerKey ().add (distance, radius), [this] (CDrawContext* c) { paintBase (c); });
    ctx->setClipRect (all);
    const double cx = centreX (), ly = listenerY ();
    const double scale = this->scale ();
    auto px = [&] (double mx) { return cx + mx * scale; };
    auto py = [&] (double my) { return ly - my * scale; };
    ctx->setLineWidth (1.0);
    ctx->setLineStyle (kLineSolid); // (as the ball's dashes left it)
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
            ctx->setLineWidth (1.5);
            ctx->drawLine (CPoint (px (tx[k][i0]), py (ty[k][i0])), CPoint (px (tx[k][i1]), py (ty[k][i1])));
        }
        ctx->setLineWidth (1.0);
        ctx->setFillColor (kOrbColor);
        const double ox = px (x[k]), oy = py (y[k]);
        if (grains)
        {
            // a grain cloud: a faint ring, the orb swelling with its newest grain
            const double g = 2.5 + 3.5 * std::clamp ((double)grain[k], 0.0, 1.0);
            CColor rc = kOrbColor;
            rc.alpha = 110;
            ctx->setFrameColor (rc);
            ctx->drawEllipse (CRect (ox - 7, oy - 7, ox + 7, oy + 7), kDrawStroked);
            ctx->drawEllipse (CRect (ox - g, oy - g, ox + g, oy + g), kDrawFilled);
        }
        else
            ctx->drawEllipse (CRect (ox - 4, oy - 4, ox + 4, oy + 4), kDrawFilled);
    }
    char buf[128];
    if (grains)
        std::snprintf (buf, sizeof (buf), "ORBS FROM ABOVE  %d, %s, %.0f m/s, grains of %.0f ms", orbs,
                       host->plainValue (kPattern) >= 0.5 ? "swarm" : "orbit", host->plainValue (kSpeed), host->plainValue (kGrainSize));
    else
        std::snprintf (buf, sizeof (buf), "ORBS FROM ABOVE  %d, %s, %.0f m/s", orbs, host->plainValue (kPattern) >= 0.5 ? "swarm" : "orbit",
                       host->plainValue (kSpeed));
    text (ctx, buf, CRect (all.left + 6, all.top + 3, all.right - 6, all.top + 17), theme::kCopperPale, 10.0, kLeftText, true);
    text (ctx, "you", CRect (cx + 12, ly - 6, cx + 60, ly + 8), theme::kTextDim, 9.0);
    ctx->resetClipRect ();
    setDirty (false);
}

} // namespace orbitr
