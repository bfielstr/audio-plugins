#include "pluginkit/ui/ScopeView.h"

#include "pluginkit/ui/Theme.h"

#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cgraphicspath.h"
#include "vstgui/lib/events.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace pk {

using namespace VSTGUI;

namespace {
constexpr double kRange = 1.25; // amplitude shown at the top and bottom edges
const CColor kClipLine (210, 70, 70, 150);

void text (CDrawContext* ctx, const std::string& s, const CRect& r, const CColor& c, double size, CHoriTxtAlign a,
           bool bold = false)
{
    ctx->setFont (theme::font (size, bold));
    ctx->setFontColor (c);
    ctx->drawString (s.c_str (), r, a, true);
}
} // namespace

ScopeView::ScopeView (const CRect& rect, Reader rd, std::function<double ()> sr, int cap, std::string t)
: CView (rect), reader (std::move (rd)), rate (std::move (sr)), capacity (std::max (64, cap)), title (std::move (t))
{
    setTooltipText ("The final output: left bright, right dim, 0 dBFS in red. Click to change the time span.");
}

void ScopeView::draw (CDrawContext* ctx)
{
    const CRect all = getViewSize ();
    ctx->setFillColor (theme::kWaveBg);
    ctx->drawRect (all, kDrawFilled);
    ctx->setClipRect (all);
    const CRect plot (all.left + 4, all.top + 20, all.right - 4, all.bottom - 4);
    auto yOf = [&] (double v) { return plot.getCenter ().y - std::clamp (v, -kRange, kRange) / kRange * plot.getHeight () * 0.5; };

    // grid: the centre, half scale and the 0 dBFS lines
    ctx->setLineWidth (1.0);
    ctx->setFrameColor (CColor (58, 58, 64));
    ctx->drawLine (CPoint (plot.left, yOf (0.0)), CPoint (plot.right, yOf (0.0)));
    ctx->setFrameColor (theme::kGrid);
    for (double v : {-0.5, 0.5})
        ctx->drawLine (CPoint (plot.left, yOf (v)), CPoint (plot.right, yOf (v)));
    ctx->setFrameColor (kClipLine);
    for (double v : {-1.0, 1.0})
        ctx->drawLine (CPoint (plot.left, yOf (v)), CPoint (plot.right, yOf (v)));

    // the samples of the span (twice as many for the trigger search on short spans)
    const double sr = rate ? std::max (1000.0, rate ()) : 48000.0;
    const int want = std::clamp ((int)(kSpanMs[spanIndex] * 0.001 * sr), 16, capacity / 2);
    const bool trigger = kSpanMs[spanIndex] <= 50.0;
    const int need = trigger ? 2 * want : want;
    l.assign ((size_t)need, 0.0f);
    r.assign ((size_t)need, 0.0f);
    const int got = reader ? reader (l.data (), r.data (), need) : 0;
    int start = need - want;
    if (trigger)
        for (int i = need - want; i > 0; --i)
            if (l[(size_t)i - 1] + r[(size_t)i - 1] < 0.0f && l[(size_t)i] + r[(size_t)i] >= 0.0f)
            {
                start = i;
                break;
            }

    // min / max per pixel column, drawn as a filled envelope (a line when zoomed in)
    const int cols = std::max (2, (int)plot.getWidth ());
    float peak = 0.0f;
    auto channel = [&] (const std::vector<float>& x, const CColor& fill, const CColor& stroke) {
        hi.assign ((size_t)cols, 0.0f);
        lo.assign ((size_t)cols, 0.0f);
        for (int c = 0; c < cols; ++c)
        {
            const int i0 = start + (int)((long long)c * want / cols);
            const int i1 = std::max (i0 + 1, start + (int)((long long)(c + 1) * want / cols));
            float mn = x[(size_t)std::min (i0, need - 1)], mx = mn;
            for (int i = i0; i < i1 && i < need; ++i)
            {
                mn = std::min (mn, x[(size_t)i]);
                mx = std::max (mx, x[(size_t)i]);
            }
            hi[(size_t)c] = mx;
            lo[(size_t)c] = mn;
            peak = std::max (peak, std::max (std::fabs (mn), std::fabs (mx)));
        }
        auto path = owned (ctx->createGraphicsPath ());
        if (!path)
            return;
        path->beginSubpath (CPoint (plot.left, yOf (hi[0])));
        for (int c = 1; c < cols; ++c)
            path->addLine (CPoint (plot.left + c, yOf (hi[(size_t)c])));
        for (int c = cols - 1; c >= 0; --c)
            path->addLine (CPoint (plot.left + c, yOf (lo[(size_t)c])));
        path->closeSubpath ();
        ctx->setFillColor (fill);
        ctx->drawGraphicsPath (path, CDrawContext::kPathFilled);
        ctx->setLineWidth (1.0);
        ctx->setFrameColor (stroke);
        ctx->drawGraphicsPath (path, CDrawContext::kPathStroked);
    };
    if (got > 0)
    {
        channel (r, CColor (120, 170, 230, 60), CColor (120, 170, 230, 140));
        channel (l, CColor (255, 164, 40, 80), theme::kAccent);
    }
    else
        text (ctx, "no output yet", plot, theme::kTextDim, 10.0, kCenterText);

    // labels
    char buf[48];
    text (ctx, title, CRect (all.left + 6, all.top + 3, all.right - 6, all.top + 17), theme::kTextBright, 10.0, kLeftText, true);
    const double ms = kSpanMs[spanIndex];
    std::snprintf (buf, sizeof (buf), ms >= 1000.0 ? "%.0f s" : "%.0f ms", ms >= 1000.0 ? ms / 1000.0 : ms);
    text (ctx, buf, CRect (all.left + 6, all.top + 3, all.right - 6, all.top + 17), theme::kTextDim, 9.5, kRightText);
    if (got > 0)
    {
        if (peak < 1e-5f)
            std::snprintf (buf, sizeof (buf), "peak -inf dBFS");
        else
            std::snprintf (buf, sizeof (buf), "peak %.1f dBFS", 20.0 * std::log10 (peak));
        text (ctx, buf, CRect (all.left + 6, all.top + 3, all.right - 60, all.top + 17), peak >= 0.999f ? kClipLine : theme::kTextDim,
              9.5, kCenterText);
    }
    ctx->resetClipRect ();
}

void ScopeView::onMouseDownEvent (MouseDownEvent& e)
{
    if (!e.buttonState.isLeft ())
        return;
    spanIndex = (spanIndex + 1) % kSpans;
    invalid ();
    e.consumed = true;
    e.ignoreFollowUpMoveAndUpEvents (true);
}

} // namespace pk
