#include "ShaperView.h"

#include "pluginkit/ui/Theme.h"

#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cframe.h"
#include "vstgui/lib/cgraphicspath.h"
#include "vstgui/lib/events.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace smacheratr {

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

std::string levelText (float linear)
{
    char buf[32];
    if (linear < 1e-4f)
        return "-inf dB";
    std::snprintf (buf, sizeof (buf), "%+.1f dB", 20.0 * std::log10 (linear));
    return buf;
}
} // namespace

ShaperView::ShaperView (const CRect& r, pk::ParamHost* h, MeterSource m) : CView (r), host (h), meters (std::move (m)) {}

double ShaperView::xOf (double in) const
{
    const CRect r = getViewSize ();
    return r.left + (std::clamp (in, -kRange, kRange) + kRange) / (2.0 * kRange) * r.getWidth ();
}

double ShaperView::yOf (double out) const
{
    const CRect r = getViewSize ();
    return r.bottom - (std::clamp (out, -kRange, kRange) + kRange) / (2.0 * kRange) * r.getHeight ();
}

void ShaperView::draw (CDrawContext* ctx)
{
    const CRect all = getViewSize ();
    ctx->setFillColor (theme::kWell);
    ctx->drawRect (all, kDrawFilled);
    ctx->setClipRect (all);

    // grid: zero and the clipping points
    ctx->setLineWidth (1.0);
    for (double v : {-1.0, 0.0, 1.0})
    {
        ctx->setFrameColor (v == 0.0 ? theme::kGridZero : theme::kGridMinor);
        ctx->drawLine (CPoint (xOf (v), all.top), CPoint (xOf (v), all.bottom));
        ctx->drawLine (CPoint (all.left, yOf (v)), CPoint (all.right, yOf (v)));
    }
    ctx->setFrameColor (theme::kGridMinor);
    ctx->drawLine (CPoint (xOf (-kRange), yOf (-kRange)), CPoint (xOf (kRange), yOf (kRange)));

    // the pre-limiter's ceiling as the drive sees it: nothing reaches past these lines
    const bool limiting = host->plainValue (kPreLimit) >= 0.5;
    if (limiting)
    {
        const double ceil = std::pow (10.0, (host->plainValue (kPreLimitThreshold) + host->plainValue (kDrive)) / 20.0);
        if (ceil < kRange)
        {
            // the region past the ceiling faintly shaded copper, the ceiling itself dashed pale copper
            ctx->setFillColor (theme::withAlpha (theme::kCopper, 18));
            ctx->drawRect (CRect (all.left, all.top, xOf (-ceil), all.bottom), kDrawFilled);
            ctx->drawRect (CRect (xOf (ceil), all.top, all.right, all.bottom), kDrawFilled);
            ctx->setFrameColor (theme::kCopperPale);
            ctx->setLineStyle (theme::dashed ());
            ctx->drawLine (CPoint (xOf (-ceil), all.top), CPoint (xOf (-ceil), all.bottom));
            ctx->drawLine (CPoint (xOf (ceil), all.top), CPoint (xOf (ceil), all.bottom));
            ctx->setLineStyle (kLineSolid);
            text (ctx, "limit", CRect (xOf (ceil) + 3, all.bottom - 30, xOf (ceil) + 60, all.bottom - 18), theme::kCopperPale, 9.0,
                  kLeftText);
        }
    }

    // where the driven signal sits
    const double reach = std::min ((double)shownIn, kRange);
    if (reach > 0.005)
    {
        ctx->setFillColor (theme::withAlpha (theme::kEnergyLive, 24)); // signal present: a faint cinnabar
        ctx->drawRect (CRect (xOf (-reach), all.top, xOf (reach), all.bottom), kDrawFilled);
    }

    // the curve, with the reached part highlighted
    const int steps = 240;
    auto curve = [&] (double from, double to, const CColor& c, double width) {
        auto path = owned (ctx->createGraphicsPath ());
        if (!path)
            return;
        for (int i = 0; i <= steps; ++i)
        {
            const double x = from + (to - from) * i / steps;
            const CPoint pt (xOf (x), yOf (analogClip (x)));
            if (i == 0)
                path->beginSubpath (pt);
            else
                path->addLine (pt);
        }
        ctx->setLineWidth (width);
        ctx->setFrameColor (c);
        ctx->drawGraphicsPath (path, CDrawContext::kPathStroked);
    };
    // the curve a thin copper line; the part the signal reaches lit in cinnabar
    curve (-kRange, kRange, theme::kCopper, 1.0);
    if (reach > 0.005)
        curve (-reach, reach, theme::kEnergyLive, 2.0);

    // labels
    text (ctx, "Analog", CRect (all.left + 6, all.top + 4, all.right - 6, all.top + 18), theme::kText, 10.5, kLeftText, true);
    char buf[80];
    if (limiting)
        std::snprintf (buf, sizeof (buf), "Drive %s after Pre-Limit %s", host->valueText (kDrive).c_str (),
                       host->valueText (kPreLimitThreshold).c_str ());
    else
        std::snprintf (buf, sizeof (buf), "Drive %s", host->valueText (kDrive).c_str ());
    text (ctx, buf, CRect (all.left + 6, all.top + 19, all.right - 6, all.top + 32), theme::kTextDim, 9.5, kLeftText);
    // Gently at work: how far each band is turned down before the curve (band 2 under band 1, each
    // named; the cut as a lit cinnabar bar on a dim track)
    int rowsShown = 0;
    for (int k = 0; k < kClarityBands; ++k)
    {
        if (!clarityBandOn (host->plainValue (kClarity), host->plainValue (kClarityRangeIds[k])))
            continue;
        const CColor c = theme::kCopperPale;
        const double range = std::max (1.0, host->plainValue (kClarityRangeIds[k]));
        const double cut = std::clamp (-(double)(k == 0 ? shownClarity : shownClarity2), 0.0, range);
        std::snprintf (buf, sizeof (buf), "%s %.1f dB", k == 0 ? "Gently" : "Gently 2", -cut);
        const double top = all.top + 4 + 24 * rowsShown++;
        text (ctx, buf, CRect (all.right - 130, top, all.right - 6, top + 14), c, 9.5, kRightText, true);
        const CRect bar (all.right - 86, top + 17, all.right - 6, top + 21);
        ctx->setFillColor (theme::kLineDim);
        ctx->drawRect (bar, kDrawFilled);
        ctx->setFillColor (theme::kEnergyLive);
        ctx->drawRect (CRect (bar.right - bar.getWidth () * cut / range, bar.top, bar.right, bar.bottom), kDrawFilled);
    }
    if (shownIn > 1e-4f)
    {
        std::snprintf (buf, sizeof (buf), "in %s  out %s", levelText (shownIn).c_str (), levelText (shownOut).c_str ());
        text (ctx, buf, CRect (all.left + 6, all.bottom - 16, all.right - 6, all.bottom - 3), theme::kTextDim, 9.5, kRightText);
    }
    ctx->setLineWidth (1.0);
    ctx->resetClipRect ();
}

void ShaperView::onMouseDownEvent (MouseDownEvent& e)
{
    const bool right = e.buttonState.isRight (); // a right click resets, like a double-click
    if (!e.buttonState.isLeft () && !right)
        return;
    if (e.clickCount == 2 || right)
    {
        host->setOnce (kDrive, host->table ().defaultNormalized (kDrive));
        invalid ();
        e.consumed = true;
        e.ignoreFollowUpMoveAndUpEvents (true);
        return;
    }
    dragging = true;
    down = e.mousePosition;
    startDriveN = host->norm (kDrive);
    host->beginEdit (kDrive);
    e.consumed = true;
}

void ShaperView::onMouseMoveEvent (MouseMoveEvent& e)
{
    if (!dragging)
    {
        if (auto* f = getFrame ())
            f->setCursor (kCursorVSize);
        return;
    }
    const double fine = e.modifiers.has (ModifierKey::Shift) ? 0.2 : 1.0;
    const double dy = (e.mousePosition.y - down.y) * fine;
    host->setNorm (kDrive, std::clamp (startDriveN - dy / 240.0, 0.0, 1.0));
    invalid ();
    e.consumed = true;
}

void ShaperView::onMouseUpEvent (MouseUpEvent& e)
{
    if (!dragging)
        return;
    dragging = false;
    host->endEdit (kDrive);
    e.consumed = true;
}

void ShaperView::onMouseExitEvent (MouseExitEvent& e)
{
    if (auto* f = getFrame ())
        f->setCursor (kCursorDefault);
    e.consumed = true;
}

void ShaperView::idle ()
{
    const Meters* m = meters ? meters () : nullptr;
    if (!m)
        return;
    auto ease = [] (float& v, float t, float up, float dn) { v += (t - v) * (t > v ? up : dn); };
    ease (shownIn, m->inPeak.load (std::memory_order_relaxed), 0.7f, 0.12f);
    ease (shownOut, m->outPeak.load (std::memory_order_relaxed), 0.7f, 0.12f);
    ease (shownClarity, m->clarityDb.load (std::memory_order_relaxed), 0.3f, 0.3f);
    ease (shownClarity2, m->clarity2Db.load (std::memory_order_relaxed), 0.3f, 0.3f);
    if (shownIn < 1e-4f)
        shownIn = 0.0f;
    invalid ();
}

} // namespace smacheratr
