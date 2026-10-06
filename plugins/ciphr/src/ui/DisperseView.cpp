#include "DisperseView.h"

#include "Disperse.h"

#include "pluginkit/ui/Theme.h"

#include "vstgui/lib/cdrawcontext.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace ciphr {

using namespace VSTGUI;
namespace theme = pk::theme;

namespace {
void text (CDrawContext* ctx, const std::string& s, const CRect& r, const CColor& c, CHoriTxtAlign a)
{
    ctx->setFont (theme::font (9.0, false));
    ctx->setFontColor (c);
    ctx->drawString (s.c_str (), r, a, true);
}
} // namespace

DisperseView::DisperseView (const CRect& r, pk::ParamHost* h) : CView (r), host (h) {}

void DisperseView::paint (CDrawContext* ctx)
{
    const CRect all = getViewSize ();
    ctx->setFillColor (theme::kWell);
    ctx->drawRect (all, kDrawFilled);
    const bool on = host->plainValue (kDisperseOn) >= 0.5;
    const int bands = std::clamp ((int)std::lround (host->plainValue (kDisperseBands)), kMinBands, kMaxBands);
    const int seed = (int)std::lround (host->plainValue (kDisperseSeed));
    const double amount = std::clamp (host->plainValue (kDisperse), 0.0, 1.0);
    int rankOf[Disperse::kMaxBands];
    Disperse::order (bands, seed, rankOf);

    // the bars' area: a line of text under it (the frequency range and the bands revealed)
    const CRect area (all.left + 4.0, all.top + 4.0, all.right - 4.0, all.bottom - 14.0);
    ctx->setFrameColor (theme::kGridMajor);
    ctx->setLineWidth (1.0);
    ctx->drawLine (CPoint (area.left, area.bottom), CPoint (area.right, area.bottom));
    const double slot = area.getWidth () / bands;
    const double barW = std::max (1.0, std::floor (slot * 0.7));
    // the next band to rise: the lowest rank not yet full
    int next = -1, full = 0;
    for (int b = 0; b < bands; ++b)
    {
        const double g = Disperse::revealGain (amount, rankOf[b], bands);
        full += g >= 1.0 ? 1 : 0;
        if (g < 1.0 && (next < 0 || rankOf[b] < rankOf[next]))
            next = b;
    }
    for (int b = 0; b < bands; ++b)
    {
        const double x = area.left + slot * b + 0.5 * (slot - barW);
        const double g = Disperse::revealGain (amount, rankOf[b], bands);
        const double db = g > 0.0 ? 20.0 * std::log10 (g) : -1e9;
        const double h = std::clamp (1.0 - db / kFloorDb, 0.0, 1.0) * area.getHeight ();
        if (b == next)
        {
            ctx->setFrameColor (theme::kLineDim);
            ctx->drawRect (CRect (x, area.top, x + barW, area.bottom), kDrawStroked);
        }
        if (h > 0.0)
        {
            ctx->setFillColor (on ? (g >= 1.0 ? theme::kCopperPale : theme::kCopper) : theme::kLineDim);
            ctx->drawRect (CRect (x, area.bottom - h, x + barW, area.bottom), kDrawFilled);
        }
    }
    text (ctx, "80 Hz", CRect (area.left, area.bottom + 1.0, area.left + 60.0, all.bottom - 1.0), theme::kTextDim, kLeftText);
    text (ctx, "12 kHz", CRect (area.right - 60.0, area.bottom + 1.0, area.right, all.bottom - 1.0), theme::kTextDim, kRightText);
    const std::string state = on ? std::to_string (full) + " of " + std::to_string (bands) + " bands full" : "off";
    text (ctx, state, CRect (area.left + 60.0, area.bottom + 1.0, area.right - 60.0, all.bottom - 1.0), on ? theme::kText : theme::kTextDim,
          kCenterText);
}

void DisperseView::draw (CDrawContext* ctx)
{
    paint (ctx);
    setDirty (false);
}

} // namespace ciphr
