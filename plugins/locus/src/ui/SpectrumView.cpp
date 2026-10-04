#include "SpectrumView.h"

#include "plugin/Controller.h"

#include "pluginkit/ui/Theme.h"

#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cframe.h"
#include "vstgui/lib/cgraphicspath.h"
#include "vstgui/lib/events.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace locus {

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

SpectrumView::SpectrumView (const CRect& r, pk::ParamHost* h, Controller* c) : CView (r), host (h), controller (c) {}

CRect SpectrumView::plot () const
{
    CRect r = getViewSize ();
    r.bottom -= kGainStrip + 14;
    return r;
}

double SpectrumView::xOfHz (double hz) const
{
    const CRect r = getViewSize ();
    return r.left + std::log (std::clamp (hz, kMinHz, kMaxHz) / kMinHz) / std::log (kMaxHz / kMinHz) * r.getWidth ();
}

double SpectrumView::hzOfX (double x) const
{
    const CRect r = getViewSize ();
    return kMinHz * std::pow (kMaxHz / kMinHz, std::clamp ((x - r.left) / r.getWidth (), 0.0, 1.0));
}

void SpectrumView::paintBase (CDrawContext* ctx)
{
    const CRect all = getViewSize ();
    const CRect pr = plot ();
    const CRect strip (all.left, pr.bottom + 14, all.right, all.bottom);
    ctx->setFillColor (theme::kWell);
    ctx->drawRect (all, kDrawFilled);
    auto yOf = [&] (double db) { return pr.top + (kMaxDb - std::clamp (db, kMinDb, kMaxDb)) / (kMaxDb - kMinDb) * pr.getHeight (); };

    // grid
    ctx->setLineWidth (1.0);
    for (double f : {30.0, 40.0, 50.0, 60.0, 80.0, 100.0, 200.0, 300.0, 400.0, 500.0, 1000.0})
    {
        const bool major = f == 100.0 || f == 1000.0 || f == 50.0 || f == 500.0;
        ctx->setFrameColor (major ? theme::kGridMajor : theme::kGridMinor);
        ctx->drawLine (CPoint (xOfHz (f), pr.top), CPoint (xOfHz (f), strip.bottom));
        char buf[16];
        std::snprintf (buf, sizeof (buf), f >= 1000 ? "%.0fk" : "%.0f", f >= 1000 ? f / 1000 : f);
        if (major || f == 30.0 || f == 200.0)
            text (ctx, buf, CRect (xOfHz (f) - 20, pr.bottom, xOfHz (f) + 20, pr.bottom + 14), theme::kTextDim, 9.5);
    }
    for (double db = -84; db <= -12; db += 12)
    {
        ctx->setFrameColor (theme::kGridMinor);
        ctx->drawLine (CPoint (pr.left, yOf (db)), CPoint (pr.right, yOf (db)));
    }
    ctx->setFrameColor (theme::kGridZero);
    ctx->drawLine (CPoint (strip.left, strip.getCenter ().y), CPoint (strip.right, strip.getCenter ().y));

    // focus range
    const double lo = host->plainValue (kLowFreq), hi = std::max (host->plainValue (kHighFreq), lo * 1.05);
    const double xl = xOfHz (lo), xh = xOfHz (hi);
    const double contrast = host->plainValue (kContrast);
    // the range a faint copper shade, deeper the stronger the Contrast either way (its sign is in the
    // readout and in the gain strip's bars, up or down); its edges pale copper, cinnabar while dragged
    ctx->setFillColor (theme::withAlpha (theme::kCopper, (uint8_t)(14 + 36 * std::fabs (contrast))));
    ctx->drawRect (CRect (xl, all.top, xh, all.bottom), kDrawFilled);
    ctx->setLineWidth (1.0);
    ctx->setFrameColor (drag == Drag::Low || drag == Drag::Range ? theme::kEnergyLive : theme::kCopperPale);
    ctx->drawLine (CPoint (xl, all.top), CPoint (xl, all.bottom));
    ctx->setFrameColor (drag == Drag::High || drag == Drag::Range ? theme::kEnergyLive : theme::kCopperPale);
    ctx->drawLine (CPoint (xh, all.top), CPoint (xh, all.bottom));
}

void SpectrumView::draw (CDrawContext* ctx)
{
    const CRect all = getViewSize ();
    const CRect pr = plot ();
    const CRect strip (all.left, pr.bottom + 14, all.right, all.bottom);
    baseLayer.draw (ctx, all, pk::LayerKey ().add (host->plainValue (kLowFreq), host->plainValue (kHighFreq), host->plainValue (kContrast), drag),
                    [this] (CDrawContext* c) { paintBase (c); });
    ctx->setClipRect (all);
    ctx->setLineWidth (1.0);
    auto yOf = [&] (double db) { return pr.top + (kMaxDb - std::clamp (db, kMinDb, kMaxDb)) / (kMaxDb - kMinDb) * pr.getHeight (); };
    auto yGain = [&] (double db) { return strip.getCenter ().y - std::clamp (db, -24.0, 24.0) / 24.0 * strip.getHeight () * 0.5; };
    const double lo = host->plainValue (kLowFreq), hi = std::max (host->plainValue (kHighFreq), lo * 1.05);
    const double xl = xOfHz (lo), xh = xOfHz (hi);

    // spectra
    const int n = (int)shownIn.size ();
    if (n > 2 && binHz > 0.0f)
    {
        auto curve = [&] (const std::vector<float>& v, bool fill, const CColor& c, double width) {
            auto path = owned (ctx->createGraphicsPath ());
            if (!path)
                return;
            bool started = false;
            for (int k = 1; k < n; ++k)
            {
                const double hz = k * binHz;
                if (hz < kMinHz * 0.9)
                    continue;
                const CPoint pt (xOfHz (hz), yOf (v[(size_t)k]));
                if (!started)
                {
                    path->beginSubpath (fill ? CPoint (pt.x, pr.bottom) : pt);
                    if (fill)
                        path->addLine (pt);
                    started = true;
                }
                else
                    path->addLine (pt);
                if (hz > kMaxHz)
                    break;
            }
            if (!started)
                return;
            if (fill)
            {
                path->addLine (CPoint (pr.right, pr.bottom));
                path->closeSubpath ();
                ctx->setFillColor (c);
                ctx->drawGraphicsPath (path, CDrawContext::kPathFilled);
            }
            else
            {
                ctx->setLineWidth (width);
                ctx->setFrameColor (c);
                ctx->drawGraphicsPath (path, CDrawContext::kPathStroked);
            }
        };
        // the input a faint copper body, the output a text-coloured trace
        curve (shownIn, true, theme::withAlpha (theme::kCopper, 60), 1.0);
        curve (shownOut, false, theme::kText, 1.0);
        // per-band gain as bars in the bottom strip
        for (int k = 1; k < n; ++k)
        {
            const double hz = k * binHz;
            if (hz < kMinHz || hz > kMaxHz)
                continue;
            const double g = shownGain[(size_t)k];
            if (std::fabs (g) < 0.05)
                continue;
            const double x = xOfHz (hz);
            // the gain being applied: lit (cinnabar), a boost up from the centre line, a cut down from it
            ctx->setFillColor (theme::kEnergyLive);
            ctx->drawRect (CRect (x - 1.5, std::min (yGain (0), yGain (g)), x + 1.5, std::max (yGain (0), yGain (g))), kDrawFilled);
        }
    }
    else
        text (ctx, "Play audio to see the low-end spectrum", pr, theme::kTextDim, 12.0);

    text (ctx, "gain per band", CRect (strip.left + 6, strip.top, strip.left + 140, strip.top + 12), theme::kTextDim, 9.0, kLeftText);
    char buf[64];
    std::snprintf (buf, sizeof (buf), "Contrast %s", host->valueText (kContrast).c_str ());
    text (ctx, buf, CRect (xl + 6, all.top + 4, std::max (xh - 6, xl + 130), all.top + 18), theme::kText, 10.5, kLeftText, true);
    ctx->resetClipRect ();
}

SpectrumView::Drag SpectrumView::hit (const CPoint& p) const
{
    const double lo = host->plainValue (kLowFreq), hi = std::max (host->plainValue (kHighFreq), lo * 1.05);
    const double xl = xOfHz (lo), xh = xOfHz (hi);
    if (std::fabs (p.x - xl) <= 5.0)
        return Drag::Low;
    if (std::fabs (p.x - xh) <= 5.0)
        return Drag::High;
    if (p.x > xl && p.x < xh)
        return Drag::Range;
    return Drag::None;
}

void SpectrumView::onMouseDownEvent (MouseDownEvent& e)
{
    const bool right = e.buttonState.isRight (); // a right click resets, like a double-click
    if (!e.buttonState.isLeft () && !right)
        return;
    drag = hit (e.mousePosition);
    if (drag == Drag::None)
        return;
    if (right && drag != Drag::Range)
    {
        const uint32_t id = drag == Drag::Low ? kLowFreq : kHighFreq;
        host->setOnce (id, host->table ().defaultNormalized (id));
        drag = Drag::None;
        invalid ();
        e.consumed = true;
        e.ignoreFollowUpMoveAndUpEvents (true);
        return;
    }
    if ((e.clickCount == 2 || right) && drag == Drag::Range)
    {
        host->setOnce (kContrast, host->table ().defaultNormalized (kContrast));
        drag = Drag::None;
        invalid ();
        e.consumed = true;
        e.ignoreFollowUpMoveAndUpEvents (true);
        return;
    }
    down = e.mousePosition;
    startLow = host->plainValue (kLowFreq);
    startHigh = host->plainValue (kHighFreq);
    startContrastN = host->norm (kContrast);
    movedH = movedV = false;
    host->beginEdit (kLowFreq);
    host->beginEdit (kHighFreq);
    host->beginEdit (kContrast);
    e.consumed = true;
}

void SpectrumView::onMouseMoveEvent (MouseMoveEvent& e)
{
    if (drag == Drag::None)
    {
        const Drag h = hit (e.mousePosition);
        if (auto* f = getFrame ())
            f->setCursor (h == Drag::Low || h == Drag::High ? kCursorHSize : h == Drag::Range ? kCursorSizeAll : kCursorDefault);
        return;
    }
    const double fine = e.modifiers.has (ModifierKey::Shift) ? 0.2 : 1.0;
    const double dx = (e.mousePosition.x - down.x) * fine, dy = (e.mousePosition.y - down.y) * fine;
    const double factor = std::pow (kMaxHz / kMinHz, dx / getViewSize ().getWidth ());
    auto setHz = [this] (uint32_t id, double hz) { host->setNorm (id, host->table ().toNormalized (id, hz)); };
    switch (drag)
    {
        case Drag::Low: setHz (kLowFreq, std::min (startLow * factor, host->plainValue (kHighFreq) / 1.1)); break;
        case Drag::High: setHz (kHighFreq, std::max (startHigh * factor, host->plainValue (kLowFreq) * 1.1)); break;
        case Drag::Range:
        {
            // the dominant direction decides: sideways moves the range, vertical sets contrast
            if (!movedH && !movedV)
            {
                if (std::fabs (dx) > 4 && std::fabs (dx) > std::fabs (dy))
                    movedH = true;
                else if (std::fabs (dy) > 4)
                    movedV = true;
            }
            if (movedH)
            {
                const double f = std::clamp (factor, 20.0 / startLow, 1000.0 / startHigh);
                setHz (kLowFreq, startLow * f);
                setHz (kHighFreq, startHigh * f);
            }
            else if (movedV)
                host->setNorm (kContrast, std::clamp (startContrastN - dy / 240.0, 0.0, 1.0));
            break;
        }
        default: break;
    }
    invalid ();
    e.consumed = true;
}

void SpectrumView::onMouseUpEvent (MouseUpEvent& e)
{
    if (drag == Drag::None)
        return;
    host->endEdit (kLowFreq);
    host->endEdit (kHighFreq);
    host->endEdit (kContrast);
    drag = Drag::None;
    e.consumed = true;
}

void SpectrumView::onMouseExitEvent (MouseExitEvent& e)
{
    if (auto* f = getFrame ())
        f->setCursor (kCursorDefault);
    e.consumed = true;
}

void SpectrumView::idle ()
{
    SharedSpectrum* s = controller->getShared ();
    if (!s)
        return;
    const int n = s->spectrum.bins.load (std::memory_order_acquire);
    if (n <= 0)
        return;
    binHz = s->spectrum.binHz.load (std::memory_order_relaxed);
    if ((int)shownIn.size () != n)
    {
        shownIn.assign ((size_t)n, -120.0f);
        shownOut.assign ((size_t)n, -120.0f);
        shownGain.assign ((size_t)n, 0.0f);
    }
    for (int k = 0; k < n; ++k)
    {
        auto ease = [] (float& v, float t, float up, float down) { v += (t - v) * (t > v ? up : down); };
        ease (shownIn[(size_t)k], s->spectrum.inputDb[(size_t)k].load (std::memory_order_relaxed), 0.6f, 0.15f);
        ease (shownOut[(size_t)k], s->spectrum.outputDb[(size_t)k].load (std::memory_order_relaxed), 0.6f, 0.15f);
        ease (shownGain[(size_t)k], s->spectrum.gainDb[(size_t)k].load (std::memory_order_relaxed), 0.4f, 0.4f);
    }
    // repainted when the spectra or a setting moved (it used to repaint on every tick, also once the
    // audio had stopped and the spectra stood still)
    pk::LayerKey key;
    key.params (host).add (binHz);
    for (int k = 0; k < n; ++k)
        key.add (shownIn[(size_t)k], shownOut[(size_t)k], shownGain[(size_t)k]);
    if (key.value () != shownKey)
    {
        shownKey = key.value ();
        invalid ();
    }
}

} // namespace locus
