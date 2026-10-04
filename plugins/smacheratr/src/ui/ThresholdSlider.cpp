#include "ThresholdSlider.h"

#include "pluginkit/ui/Theme.h"

#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cgraphicspath.h"
#include "vstgui/lib/events.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace smacheratr {

using namespace VSTGUI;
namespace theme = pk::theme;

namespace {
void text (CDrawContext* ctx, const std::string& s, const CRect& r, const CColor& c, double size, bool bold = false)
{
    ctx->setFont (theme::font (size, bold));
    ctx->setFontColor (c);
    ctx->drawString (s.c_str (), r, kCenterText, true);
}
bool isFine (const Modifiers& m) { return m.has (ModifierKey::Shift) || m.has (ModifierKey::Control); }
} // namespace

ThresholdSlider::ThresholdSlider (const CRect& r, pk::ParamHost* h, int b, MeterSource m)
    : ParamView (r, h, kGentlrThresholdIds[std::clamp (b, 0, kGentlrBands - 1)]), band (std::clamp (b, 0, kGentlrBands - 1)), meters (std::move (m))
{
}

CRect ThresholdSlider::track () const
{
    const CRect r = getViewSize ();
    return CRect (r.left + 3, r.top + 16, r.right - 3, r.bottom - 16);
}

double ThresholdSlider::yOfDb (double db) const
{
    const auto& info = host->table ().info (param);
    const CRect t = track ();
    return t.bottom - (std::clamp (db, info.min, info.max) - info.min) / (info.max - info.min) * t.getHeight ();
}

void ThresholdSlider::idle ()
{
    const Meters* m = meters ? meters () : nullptr;
    const float level = !m ? -120.0f : clarityLevelMeter (*m, band).load (std::memory_order_relaxed);
    const float before = shownDb;
    // up at once, down eased (like a peak meter)
    shownDb = level > shownDb ? level : shownDb + (level - shownDb) * 0.25f;
    if (std::fabs (shownDb - before) > 0.05f)
        invalid ();
}

void ThresholdSlider::draw (CDrawContext* ctx)
{
    const CRect r = getViewSize ();
    const CRect t = track ();
    const bool on = enabledLook;
    // a meter bed: a well in a thin outline (copper; cinnabar while dragged), the band's name on top
    ctx->setFillColor (theme::kWell);
    ctx->drawRect (r, kDrawFilled);
    pk::draw::outline (ctx, r, dragging ? theme::kEnergyLive : (on ? theme::kCopper : theme::kLineDim), 0);
    text (ctx, band == 0 ? "1" : band == 1 ? "2" : band == kSubBand ? "S" : "H", CRect (r.left, r.top + 1, r.right, r.top + 15), on ? theme::kCopperPale : theme::kTextDim,
          10.0, true);

    // the level as a meter: energy idle below the threshold, lit live above it (the part being cut)
    const double thr = host->plainValue (param);
    const double yThr = yOfDb (thr), yLevel = yOfDb (shownDb);
    const CRect bar (t.left + 2, t.top, t.right - 2, t.bottom);
    ctx->setFillColor (theme::kPanel);
    ctx->drawRect (bar, kDrawFilled);
    if (shownDb > host->table ().info (param).min)
    {
        ctx->setFillColor (on ? theme::kEnergyIdle : theme::kLineDim);
        ctx->drawRect (CRect (bar.left, std::max (yLevel, yThr), bar.right, bar.bottom), kDrawFilled);
        if (yLevel < yThr)
        {
            ctx->setFillColor (on ? theme::kEnergyLive : theme::kEnergyIdle);
            ctx->drawRect (CRect (bar.left, yLevel, bar.right, yThr), kDrawFilled);
        }
    }
    // a tick every 12 dB
    ctx->setFrameColor (theme::kCopper);
    for (double db = -48.0; db < 0.0; db += 12.0)
        ctx->drawLine (CPoint (t.left, yOfDb (db)), CPoint (t.left + 3, yOfDb (db)));

    // the threshold: a line across and a handle on it
    ctx->setLineWidth (1.5);
    ctx->setFrameColor (dragging ? theme::kEnergyLive : (on ? theme::kText : theme::kTextDim));
    ctx->drawLine (CPoint (r.left + 1, yThr), CPoint (r.right - 1, yThr));
    ctx->setLineWidth (1.0);
    if (auto path = owned (ctx->createGraphicsPath ()))
    {
        path->beginSubpath (CPoint (r.right - 1, yThr - 4));
        path->addLine (CPoint (r.right - 7, yThr));
        path->addLine (CPoint (r.right - 1, yThr + 4));
        path->closeSubpath ();
        ctx->setFillColor (dragging ? theme::kEnergyLive : (on ? theme::kText : theme::kTextDim));
        ctx->drawGraphicsPath (path, CDrawContext::kPathFilled);
    }
    char buf[16];
    std::snprintf (buf, sizeof (buf), "%.0f", thr);
    text (ctx, buf, CRect (r.left, r.bottom - 15, r.right, r.bottom - 1), dragging ? theme::kEnergyLive : (on ? theme::kText : theme::kTextDim),
          9.5);
}

void ThresholdSlider::onMouseDownEvent (MouseDownEvent& e)
{
    if (resetOnRightClick (e))
        return;
    if (!e.buttonState.isLeft ())
        return;
    if (e.clickCount == 2)
    {
        host->setOnce (param, host->table ().defaultNormalized (param));
        invalid ();
        e.consumed = true;
        return;
    }
    dragging = true;
    startY = e.mousePosition.y;
    startValue = host->norm (param);
    host->beginEdit (param);
    invalid ();
    e.consumed = true;
}

void ThresholdSlider::onMouseMoveEvent (MouseMoveEvent& e)
{
    if (!dragging)
        return;
    // the handle follows the mouse over the track (Shift: a fifth as fast)
    const double h = std::max (20.0, track ().getHeight ()) * (isFine (e.modifiers) ? 5.0 : 1.0);
    host->setNorm (param, std::clamp (startValue + (startY - e.mousePosition.y) / h, 0.0, 1.0));
    invalid ();
    e.consumed = true;
}

void ThresholdSlider::onMouseUpEvent (MouseUpEvent& e)
{
    if (!dragging)
        return;
    dragging = false;
    host->endEdit (param);
    invalid ();
    e.consumed = true;
}

void ThresholdSlider::onMouseCancelEvent (MouseCancelEvent& e)
{
    if (dragging)
    {
        dragging = false;
        host->endEdit (param);
        invalid ();
    }
    e.consumed = true;
}

void ThresholdSlider::onMouseWheelEvent (MouseWheelEvent& e)
{
    const double d = e.deltaY != 0.0 ? e.deltaY : e.deltaX;
    if (d == 0.0)
        return;
    const double step = isFine (e.modifiers) ? 0.002 : 1.0 / 60.0; // 1 dB a notch over -60 .. 0 dB
    host->setOnce (param, std::clamp (host->norm (param) + (d > 0 ? step : -step), 0.0, 1.0));
    invalid ();
    e.consumed = true;
}

void ThresholdSlider::layout (ColorView* color, ThresholdSlider* const* sliders, const CRect& area, bool advanced,
                              const std::vector<CView*>& above)
{
    CRect display = area;
    if (advanced)
        display.right -= kStripWidth;
    if (color && color->getViewSize () != display)
    {
        color->setViewSize (display);
        color->setMouseableArea (display);
        color->invalid ();
    }
    const double left = area.right - kStripWidth + kGap;
    double top = area.top;
    for (auto* v : above)
    {
        if (!v)
            continue;
        v->setVisible (advanced);
        const CRect r (left, top, area.right, top + 18);
        v->setViewSize (r);
        v->setMouseableArea (r);
        top += 22;
    }
    for (int k = 0; k < kGentlrBands; ++k)
        if (auto* s = sliders[k])
        {
            const CRect r (left + k * (kWidth + kGap), top, left + k * (kWidth + kGap) + kWidth, area.bottom);
            s->setViewSize (r);
            s->setMouseableArea (r);
            s->setVisible (advanced);
            s->invalid ();
        }
}

} // namespace smacheratr
