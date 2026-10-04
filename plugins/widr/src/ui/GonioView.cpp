#include "GonioView.h"


#include "pluginkit/ui/Theme.h"

#include "vstgui/lib/cdrawcontext.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace widr {

using namespace VSTGUI;
namespace theme = pk::theme;

namespace {
void text (CDrawContext* ctx, const std::string& s, const CRect& r, const CColor& c, double size,
           CHoriTxtAlign a = kCenterText, bool bold = false)
{
    ctx->setFont (theme::font (size, bold));
    ctx->setFontColor (c);
    ctx->drawString (s.c_str (), r, a, true);
}
} // namespace

GonioView::GonioView (const CRect& rect, MeterSource m) : CView (rect), meters (std::move (m))
{
    l.resize (kPoints);
    r.resize (kPoints);
}

double GonioView::dotScale () const
{
    const CRect all = getViewSize ();
    const CRect g (all.left, all.top, all.right, all.bottom - kMeter);
    const double half = std::min (g.getWidth (), g.getHeight ()) * 0.5 - 10.0;
    return half / std::max (0.02f, level * 2.5f);
}

void GonioView::paintBase (CDrawContext* ctx)
{
    const CRect all = getViewSize ();
    ctx->setFillColor (theme::kWell);
    ctx->drawRect (all, kDrawFilled);
    const CRect g (all.left, all.top, all.right, all.bottom - kMeter);
    const CPoint c = g.getCenter ();
    const double half = std::min (g.getWidth (), g.getHeight ()) * 0.5 - 10.0;

    // axes: M vertical, S horizontal, L and R on the diagonals
    ctx->setLineWidth (1.0);
    ctx->setFrameColor (theme::kGridMajor);
    ctx->drawLine (CPoint (c.x, c.y - half), CPoint (c.x, c.y + half));
    ctx->drawLine (CPoint (c.x - half, c.y), CPoint (c.x + half, c.y));
    const double d = half * M_SQRT1_2;
    ctx->drawLine (CPoint (c.x - d, c.y - d), CPoint (c.x + d, c.y + d));
    ctx->drawLine (CPoint (c.x + d, c.y - d), CPoint (c.x - d, c.y + d));
    text (ctx, "M", CRect (c.x - 20, g.top + 2, c.x + 20, g.top + 14), theme::kTextDim, 9.0);
    text (ctx, "L", CRect (c.x - d - 14, c.y - d - 14, c.x - d, c.y - d), theme::kTextDim, 9.0);
    text (ctx, "R", CRect (c.x + d, c.y - d - 14, c.x + d + 14, c.y - d), theme::kTextDim, 9.0);
    text (ctx, "S", CRect (g.right - 14, c.y - 14, g.right - 2, c.y), theme::kTextDim, 9.0);

    // the correlation meter's title, its bed (dim, the left half: out of phase, in energy idle) and scale
    const CRect mr (all.left + 10, all.bottom - kMeter + 22, all.right - 10, all.bottom - kMeter + 36);
    text (ctx, "CORRELATION", CRect (all.left + 8, all.bottom - kMeter + 4, all.right - 8, all.bottom - kMeter + 18),
          theme::kTextDim, 9.0, kLeftText, true);
    ctx->setFillColor (theme::kLineDim);
    ctx->drawRect (mr, kDrawFilled);
    ctx->setFillColor (theme::kEnergyIdle); // the left half: out of phase
    ctx->drawRect (CRect (mr.left, mr.top, mr.getCenter ().x, mr.bottom), kDrawFilled);
    for (int k = 0; k < 3; ++k)
    {
        const char* labels[3] = {"-1", "0", "+1"};
        const double tx = mr.left + k * 0.5 * mr.getWidth ();
        text (ctx, labels[k], CRect (tx - 14, mr.bottom + 3, tx + 14, mr.bottom + 16), theme::kTextDim, 9.0);
    }
}

void GonioView::draw (CDrawContext* ctx)
{
    const CRect all = getViewSize ();
    baseLayer.draw (ctx, all, 0, [this] (CDrawContext* cc) { paintBase (cc); });
    ctx->setClipRect (all);
    const CRect g (all.left, all.top, all.right, all.bottom - kMeter);
    const CPoint c = g.getCenter ();

    // the dots, scaled to the level so quiet material still fills the scope
    const double scale = dotScale ();
    ctx->setFillColor (theme::withAlpha (theme::kEnergyLive, 110)); // the signal lights the scope
    for (int i = 0; i < count; ++i)
    {
        const double m = (l[(size_t)i] + r[(size_t)i]) * M_SQRT1_2, s = (l[(size_t)i] - r[(size_t)i]) * M_SQRT1_2;
        const double x = std::clamp (c.x + s * scale, g.left, g.right), y = std::clamp (c.y - m * scale, g.top, g.bottom);
        ctx->drawRect (CRect (x - 0.75, y - 0.75, x + 0.75, y + 0.75), kDrawFilled);
    }

    // correlation meter: the readout and the needle (its title, bed and scale are in the layer)
    const CRect mr (all.left + 10, all.bottom - kMeter + 22, all.right - 10, all.bottom - kMeter + 36);
    char buf[16];
    std::snprintf (buf, sizeof (buf), "%+.2f", correlation);
    text (ctx, buf, CRect (all.left + 8, all.bottom - kMeter + 4, all.right - 8, all.bottom - kMeter + 18),
          correlation < 0.0f ? theme::kEnergyLive : theme::kText, 10.0, kRightText, true);
    // the needle text-coloured, cinnabar while the correlation is negative (the readout says it too)
    const double x = mr.left + (correlation + 1.0) * 0.5 * mr.getWidth ();
    ctx->setFillColor (correlation < 0.0f ? theme::kEnergyLive : theme::kText);
    ctx->drawRect (CRect (x - 2.0, mr.top - 3.0, x + 2.0, mr.bottom + 3.0), kDrawFilled);
    ctx->resetClipRect ();
}

void GonioView::idle ()
{
    Meters* m = meters ? meters () : nullptr;
    if (!m)
        return;
    count = m->scope.read (l.data (), r.data (), kPoints);
    float peak = 0.0f;
    for (int i = 0; i < count; ++i)
        peak = std::max ({peak, std::fabs (l[(size_t)i]), std::fabs (r[(size_t)i])});
    level += (std::max (peak * 0.4f, 1e-4f) - level) * (peak * 0.4f > level ? 0.5f : 0.05f);
    correlation = m->correlation.load (std::memory_order_relaxed);
    // repainted when a dot or the correlation moved (silence leaves every dot in the centre, however
    // the level eases: then nothing is repainted)
    const double scale = dotScale ();
    pk::LayerKey key;
    key.add (count, correlation);
    for (int i = 0; i < count; ++i)
    {
        const double mm = (l[(size_t)i] + r[(size_t)i]) * M_SQRT1_2 * scale, ss = (l[(size_t)i] - r[(size_t)i]) * M_SQRT1_2 * scale;
        key.add (std::lround (mm * 1000.0), std::lround (ss * 1000.0));
    }
    if (key.value () != shownKey)
    {
        shownKey = key.value ();
        invalid ();
    }
}

} // namespace widr
