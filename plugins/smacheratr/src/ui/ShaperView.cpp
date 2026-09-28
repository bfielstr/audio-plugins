#include "ShaperView.h"

#include "plugin/Controller.h"

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

ShaperSettings shaperSettingsFrom (pk::ParamHost* host)
{
    ShaperSettings s;
    s.curve = std::clamp ((int)std::lround (host->plainValue (kCurve)), 0, kNumCurves - 1);
    s.bassThresholdDb = host->plainValue (kBassThreshold);
    s.ws.drive = host->plainValue (kWsDrive);
    s.ws.curve = host->plainValue (kWsCurve);
    s.ws.depth = host->plainValue (kWsDepth);
    s.ws.linear = host->plainValue (kWsLinear);
    s.ws.damp = host->plainValue (kWsDamp);
    s.ws.period = host->plainValue (kWsPeriod);
    return s;
}

ShaperView::ShaperView (const CRect& r, pk::ParamHost* h, Controller* c) : CView (r), host (h), controller (c) {}

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
    ctx->setFillColor (theme::kWaveBg);
    ctx->drawRect (all, kDrawFilled);
    ctx->setClipRect (all);

    // grid: zero and the clipping points
    ctx->setLineWidth (1.0);
    for (double v : {-1.0, 0.0, 1.0})
    {
        ctx->setFrameColor (v == 0.0 ? CColor (58, 58, 64) : theme::kGrid);
        ctx->drawLine (CPoint (xOf (v), all.top), CPoint (xOf (v), all.bottom));
        ctx->drawLine (CPoint (all.left, yOf (v)), CPoint (all.right, yOf (v)));
    }
    ctx->setFrameColor (theme::kGrid);
    ctx->drawLine (CPoint (xOf (-kRange), yOf (-kRange)), CPoint (xOf (kRange), yOf (kRange)));

    // where the driven signal sits
    const double reach = std::min ((double)shownIn, kRange);
    if (reach > 0.005)
    {
        ctx->setFillColor (CColor (255, 164, 40, 22));
        ctx->drawRect (CRect (xOf (-reach), all.top, xOf (reach), all.bottom), kDrawFilled);
    }

    // the curve, with the reached part highlighted
    const ShaperSettings s = shaperSettingsFrom (host);
    const int steps = 240;
    auto curve = [&] (double from, double to, const CColor& c, double width) {
        auto path = owned (ctx->createGraphicsPath ());
        if (!path)
            return;
        bool started = false;
        for (int i = 0; i <= steps; ++i)
        {
            const double x = from + (to - from) * i / steps;
            const CPoint pt (xOf (x), yOf (shape (x, s)));
            if (!started)
                path->beginSubpath (pt);
            else
                path->addLine (pt);
            started = true;
        }
        ctx->setLineWidth (width);
        ctx->setFrameColor (c);
        ctx->drawGraphicsPath (path, CDrawContext::kPathStroked);
    };
    curve (-kRange, kRange, theme::kCurve, 1.6);
    if (reach > 0.005)
        curve (-reach, reach, theme::kTextBright, 2.6);

    // labels
    const auto& info = host->table ().info (kCurve);
    const char* name = info.choices[(size_t)s.curve];
    text (ctx, name, CRect (all.left + 6, all.top + 4, all.right - 6, all.top + 18), theme::kTextBright, 10.5, kLeftText, true);
    char buf[64];
    std::snprintf (buf, sizeof (buf), "Drive %s", host->valueText (kDrive).c_str ());
    text (ctx, buf, CRect (all.left + 6, all.top + 19, all.right - 6, all.top + 32), theme::kTextDim, 9.5, kLeftText);
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
    if (!e.buttonState.isLeft ())
        return;
    if (e.clickCount == 2)
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
    SharedMeters* s = controller->getShared ();
    if (!s)
        return;
    auto ease = [] (float& v, float t, float up, float down) { v += (t - v) * (t > v ? up : down); };
    ease (shownIn, s->meters.inPeak.load (std::memory_order_relaxed), 0.7f, 0.12f);
    ease (shownOut, s->meters.outPeak.load (std::memory_order_relaxed), 0.7f, 0.12f);
    if (shownIn < 1e-4f)
        shownIn = 0.0f;
    invalid ();
}

} // namespace smacheratr
