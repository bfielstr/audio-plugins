#include "ColorView.h"

#include "../core/ClarityBand.h"
#include "../core/Color.h"

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
constexpr double kHandleRadius = 5.0;
const CColor kClarityColor (120, 210, 140);
// Clarity's gain at a frequency for a cut (dB) at the band's peak: the band scaled by its response
double clarityGainDb (const ClarityBand& b, double hz, double sr, double cutDb)
{
    const double m = std::pow (10.0, clarityBandDb (b, hz, sr) / 20.0);
    return 20.0 * std::log10 (std::max (1e-6, 1.0 - (1.0 - std::pow (10.0, cutDb / 20.0)) * m));
}
} // namespace

ColorView::ColorView (const CRect& r, pk::ParamHost* h, RateSource rs, MeterSource ms)
    : CView (r), host (h), rate (std::move (rs)), meters (std::move (ms))
{
}

double ColorView::sampleRate () const
{
    const double r = rate ? rate () : 0.0;
    return r > 1000.0 ? r : 48000.0;
}

bool ColorView::clarityOn () const { return host->plainValue (kClarity) >= 0.5; }

void ColorView::idle ()
{
    const Meters* m = meters ? meters () : nullptr;
    const float target = m && clarityOn () ? m->clarityDb.load (std::memory_order_relaxed) : 0.0f;
    const float before = shownCut;
    shownCut += (target - shownCut) * 0.35f;
    if (std::fabs (shownCut) < 0.01f)
        shownCut = 0.0f;
    if (std::fabs (shownCut - before) > 0.005f)
        invalid ();
}

double ColorView::xOfHz (double hz) const
{
    const CRect r = getViewSize ();
    return r.left + std::log (std::clamp (hz, kMinHz, kMaxHz) / kMinHz) / std::log (kMaxHz / kMinHz) * r.getWidth ();
}

double ColorView::yOfDb (double db) const
{
    const CRect r = getViewSize ();
    return r.getCenter ().y - std::clamp (db, -kMaxDb, kMaxDb) / kMaxDb * (r.getHeight () * 0.5 - 12.0);
}

CPoint ColorView::loHandle () const { return CPoint (xOfHz (kColorLowHz * 0.5), yOfDb (colorDb (host->plainValue (kColorLo)))); }

CPoint ColorView::hiHandle () const
{
    return CPoint (xOfHz (host->plainValue (kColorFreq)), yOfDb (colorDb (host->plainValue (kColorHi))));
}

CPoint ColorView::clarityHandle () const { return CPoint (xOfHz (host->plainValue (kClarityFreq)), yOfDb (0.0)); }

double ColorView::clarityEdgeX (bool high) const
{
    const ClarityBand b = clarityBand (sampleRate (), host->plainValue (kClarityFreq), host->plainValue (kClarityWidth));
    return xOfHz (high ? b.highHz : b.lowHz);
}

void ColorView::draw (CDrawContext* ctx)
{
    const CRect all = getViewSize ();
    const bool on = host->plainValue (kColorOn) >= 0.5;
    ctx->setFillColor (theme::kWaveBg);
    ctx->drawRect (all, kDrawFilled);
    ctx->setClipRect (all);

    // grid
    ctx->setLineWidth (1.0);
    for (double f : {50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0})
    {
        const bool major = f == 100.0 || f == 1000.0 || f == 10000.0;
        ctx->setFrameColor (major ? CColor (58, 58, 64) : theme::kGrid);
        ctx->drawLine (CPoint (xOfHz (f), all.top), CPoint (xOfHz (f), all.bottom));
        if (major)
        {
            char buf[16];
            std::snprintf (buf, sizeof (buf), f >= 1000 ? "%.0fk" : "%.0f", f >= 1000 ? f / 1000 : f);
            text (ctx, buf, CRect (xOfHz (f) - 20, all.bottom - 14, xOfHz (f) + 20, all.bottom - 2), theme::kTextDim, 9.5);
        }
    }
    for (double db : {-12.0, 0.0, 12.0})
    {
        ctx->setFrameColor (db == 0.0 ? CColor (58, 58, 64) : theme::kGrid);
        ctx->drawLine (CPoint (all.left, yOfDb (db)), CPoint (all.right, yOfDb (db)));
    }

    // Clarity's band, behind the curves: its range shaded, its edges (drag them for the width)
    const bool clarity = clarityOn ();
    const double sr0 = sampleRate ();
    const ClarityBand band = clarityBand (sr0, host->plainValue (kClarityFreq), host->plainValue (kClarityWidth));
    if (clarity)
    {
        const double x0 = xOfHz (band.lowHz), x1 = xOfHz (band.highHz);
        ctx->setFillColor (CColor (120, 210, 140, 16));
        ctx->drawRect (CRect (x0, all.top, x1, all.bottom), kDrawFilled);
        ctx->setLineWidth (1.0);
        const bool edgeActive = drag == Drag::ClarityLow || drag == Drag::ClarityHigh;
        ctx->setFrameColor (CColor (120, 210, 140, edgeActive ? 200 : 90));
        ctx->drawLine (CPoint (x0, all.top), CPoint (x0, all.bottom));
        ctx->drawLine (CPoint (x1, all.top), CPoint (x1, all.bottom));
    }

    // response
    const double sr = sampleRate ();
    const double lo = host->plainValue (kColorLo), hi = host->plainValue (kColorHi);
    const double freq = host->plainValue (kColorFreq), width = host->plainValue (kColorWidth);
    const int steps = 160;
    auto path = owned (ctx->createGraphicsPath ());
    if (path)
    {
        path->beginSubpath (CPoint (all.left, yOfDb (0.0)));
        for (int i = 0; i <= steps; ++i)
        {
            const double hz = kMinHz * std::pow (kMaxHz / kMinHz, (double)i / steps);
            path->addLine (CPoint (xOfHz (hz), yOfDb (colorResponseDb (hz, sr, lo, hi, freq, width))));
        }
        path->addLine (CPoint (all.right, yOfDb (0.0)));
        path->closeSubpath ();
        ctx->setFillColor (on ? CColor (255, 164, 40, 40) : CColor (120, 120, 120, 20));
        ctx->drawGraphicsPath (path, CDrawContext::kPathFilled);
        ctx->setLineWidth (1.6);
        ctx->setFrameColor (on ? theme::kCurve : theme::kTextDim);
        ctx->drawGraphicsPath (path, CDrawContext::kPathStroked);
    }

    // Clarity at work, as a multiband compressor shows a band: the most it can cut outlined, the cut it
    // is making right now filled in from the 0 dB line, a handle at its centre and its readout
    if (clarity)
    {
        auto gainPath = [&] (double cutDb, bool closed) {
            auto gp = owned (ctx->createGraphicsPath ());
            if (!gp)
                return gp;
            const double from = std::max (kMinHz, band.lowHz / 8.0), to = std::min (kMaxHz, band.highHz * 16.0);
            if (closed)
                gp->beginSubpath (CPoint (xOfHz (from), yOfDb (0.0)));
            for (int i = 0; i <= steps; ++i)
            {
                const double hz = from * std::pow (to / from, (double)i / steps);
                const CPoint pt (xOfHz (hz), yOfDb (clarityGainDb (band, hz, sr, cutDb)));
                if (i == 0 && !closed)
                    gp->beginSubpath (pt);
                else
                    gp->addLine (pt);
            }
            if (closed)
            {
                gp->addLine (CPoint (xOfHz (to), yOfDb (0.0)));
                gp->closeSubpath ();
            }
            return gp;
        };
        if (auto range = gainPath (-host->plainValue (kClarityRange), false))
        {
            ctx->setLineWidth (1.0);
            ctx->setFrameColor (CColor (120, 210, 140, 110));
            ctx->setLineStyle (CLineStyle (CLineStyle::kLineCapButt, CLineStyle::kLineJoinMiter, 0.0, {3.0, 3.0}));
            ctx->drawGraphicsPath (range, CDrawContext::kPathStroked);
            ctx->setLineStyle (kLineSolid);
        }
        if (shownCut < -0.05f)
            if (auto live = gainPath (shownCut, true))
            {
                ctx->setFillColor (CColor (120, 210, 140, 90));
                ctx->drawGraphicsPath (live, CDrawContext::kPathFilled);
                ctx->setLineWidth (1.8);
                ctx->setFrameColor (kClarityColor);
                ctx->drawGraphicsPath (live, CDrawContext::kPathStroked);
            }
        const CPoint h = clarityHandle ();
        // the readout, in a pill above the band
        char cb[64];
        std::snprintf (cb, sizeof (cb), "Clarity  %s   %.1f dB", host->valueText (kClarityFreq).c_str (), (double)shownCut);
        const double pw = 150.0, px = std::clamp (h.x - pw / 2, all.left + 4, all.right - pw - 4);
        const CRect pill (px, all.top + 36, px + pw, all.top + 52);
        ctx->setFillColor (CColor (20, 36, 26, 220));
        ctx->drawRect (pill, kDrawFilled);
        ctx->setFrameColor (CColor (120, 210, 140, 160));
        ctx->setLineWidth (1.0);
        ctx->drawRect (pill, kDrawStroked);
        text (ctx, cb, pill, kClarityColor, 9.5, kCenterText, true);
        const CRect hr (h.x - kHandleRadius - 1, h.y - kHandleRadius - 1, h.x + kHandleRadius + 1, h.y + kHandleRadius + 1);
        ctx->setFillColor (kClarityColor);
        ctx->drawEllipse (hr, kDrawFilled);
        ctx->setLineWidth (1.5);
        ctx->setFrameColor (theme::kTextBright);
        ctx->drawEllipse (hr, kDrawStroked);
    }

    // handles
    for (const CPoint& h : {loHandle (), hiHandle ()})
    {
        const CRect hr (h.x - kHandleRadius, h.y - kHandleRadius, h.x + kHandleRadius, h.y + kHandleRadius);
        ctx->setFillColor (on ? theme::kTextBright : theme::kTextDim);
        ctx->drawEllipse (hr, kDrawFilled);
        ctx->setLineWidth (1.5);
        ctx->setFrameColor (on ? theme::kAccent : theme::kKnobTrack);
        ctx->drawEllipse (hr, kDrawStroked);
    }

    // labels
    text (ctx, on ? "COLOR" : "COLOR  (off)", CRect (all.left + 6, all.top + 4, all.right - 6, all.top + 18),
          on ? theme::kTextBright : theme::kTextDim, 10.5, kLeftText, true);
    char buf[96];
    std::snprintf (buf, sizeof (buf), "Lo %s   Hi %s @ %s", host->valueText (kColorLo).c_str (),
                   host->valueText (kColorHi).c_str (), host->valueText (kColorFreq).c_str ());
    text (ctx, buf, CRect (all.left + 6, all.top + 19, all.right - 6, all.top + 32), theme::kTextDim, 9.5, kLeftText);
    ctx->setLineWidth (1.0);
    ctx->resetClipRect ();
}

ColorView::Drag ColorView::hit (const CPoint& p) const
{
    auto near = [&] (const CPoint& h) { return std::hypot (p.x - h.x, p.y - h.y) <= kHandleRadius + 4.0; };
    if (near (hiHandle ()))
        return Drag::Hi;
    if (near (loHandle ()))
        return Drag::Lo;
    if (clarityOn ())
    {
        if (near (clarityHandle ()))
            return Drag::Clarity;
        if (std::fabs (p.x - clarityEdgeX (false)) <= 4.0)
            return Drag::ClarityLow;
        if (std::fabs (p.x - clarityEdgeX (true)) <= 4.0)
            return Drag::ClarityHigh;
    }
    return Drag::None;
}

void ColorView::onMouseDownEvent (MouseDownEvent& e)
{
    const bool right = e.buttonState.isRight (); // a right click resets, like a double-click
    if (!e.buttonState.isLeft () && !right)
        return;
    drag = hit (e.mousePosition);
    if (drag == Drag::None)
        return;
    if (e.clickCount == 2 || right)
    {
        if (drag == Drag::Clarity || drag == Drag::ClarityLow || drag == Drag::ClarityHigh)
        {
            host->setOnce (kClarityFreq, host->table ().defaultNormalized (kClarityFreq));
            host->setOnce (kClarityWidth, host->table ().defaultNormalized (kClarityWidth));
        }
        else if (drag == Drag::Lo)
            host->setOnce (kColorLo, host->table ().defaultNormalized (kColorLo));
        else
        {
            host->setOnce (kColorHi, host->table ().defaultNormalized (kColorHi));
            host->setOnce (kColorFreq, host->table ().defaultNormalized (kColorFreq));
        }
        drag = Drag::None;
        invalid ();
        e.consumed = true;
        e.ignoreFollowUpMoveAndUpEvents (true);
        return;
    }
    down = e.mousePosition;
    startLo = host->plainValue (kColorLo);
    startHi = host->plainValue (kColorHi);
    startFreq = host->plainValue (kColorFreq);
    startClarity = host->plainValue (kClarityFreq);
    movedH = movedV = false;
    if (drag == Drag::Clarity)
        host->beginEdit (kClarityFreq);
    else if (drag == Drag::ClarityLow || drag == Drag::ClarityHigh)
        host->beginEdit (kClarityWidth);
    else if (drag == Drag::Lo)
        host->beginEdit (kColorLo);
    else
    {
        host->beginEdit (kColorHi);
        host->beginEdit (kColorFreq);
    }
    e.consumed = true;
}

void ColorView::onMouseMoveEvent (MouseMoveEvent& e)
{
    if (drag == Drag::None)
    {
        const Drag h = hit (e.mousePosition);
        if (auto* f = getFrame ())
            f->setCursor (h == Drag::Hi ? kCursorSizeAll
                          : h == Drag::Lo ? kCursorVSize
                          : h != Drag::None ? kCursorHSize
                                            : kCursorDefault);
        return;
    }
    const CRect r = getViewSize ();
    const double fine = e.modifiers.has (ModifierKey::Shift) ? 0.2 : 1.0;
    const double dx = (e.mousePosition.x - down.x) * fine, dy = (e.mousePosition.y - down.y) * fine;
    const double amountPerPixel = 2.0 / (r.getHeight () - 24.0); // +-100 % over the plot
    auto setPlain = [this] (uint32_t id, double v) { host->setNorm (id, host->table ().toNormalized (id, v)); };
    if (drag == Drag::Clarity)
        setPlain (kClarityFreq, startClarity * std::pow (kMaxHz / kMinHz, dx / r.getWidth ()));
    else if (drag == Drag::ClarityLow || drag == Drag::ClarityHigh)
    {
        // the edge follows the mouse; the band stays centred, so the width is twice the distance
        const double hz = kMinHz * std::pow (kMaxHz / kMinHz, (e.mousePosition.x - r.left) / r.getWidth ());
        setPlain (kClarityWidth, 2.0 * std::fabs (std::log2 (hz / host->plainValue (kClarityFreq))));
    }
    else if (drag == Drag::Lo)
        setPlain (kColorLo, startLo - dy * amountPerPixel);
    else
    {
        // the dominant direction decides: up/down sets the amount, sideways the frequency
        if (!movedH && !movedV)
        {
            if (std::fabs (dx) > 4 && std::fabs (dx) > std::fabs (dy))
                movedH = true;
            else if (std::fabs (dy) > 4)
                movedV = true;
        }
        if (movedH)
            setPlain (kColorFreq, startFreq * std::pow (kMaxHz / kMinHz, dx / r.getWidth ()));
        else if (movedV)
            setPlain (kColorHi, startHi - dy * amountPerPixel);
    }
    invalid ();
    e.consumed = true;
}

void ColorView::onMouseUpEvent (MouseUpEvent& e)
{
    if (drag == Drag::None)
        return;
    if (drag == Drag::Clarity)
        host->endEdit (kClarityFreq);
    else if (drag == Drag::ClarityLow || drag == Drag::ClarityHigh)
        host->endEdit (kClarityWidth);
    else if (drag == Drag::Lo)
        host->endEdit (kColorLo);
    else
    {
        host->endEdit (kColorHi);
        host->endEdit (kColorFreq);
    }
    drag = Drag::None;
    e.consumed = true;
}

void ColorView::onMouseWheelEvent (MouseWheelEvent& e)
{
    // the colour filter's intensity is its width: wheel up narrows it (a sharper, stronger band)
    const bool active = drag != Drag::None || (e.modifiers.has (ModifierKey::Shift) && hit (e.mousePosition) != Drag::None);
    if (!active)
        return;
    // Clarity's band: wheel up widens it
    const Drag over = drag != Drag::None ? drag : hit (e.mousePosition);
    const bool band = over == Drag::Clarity || over == Drag::ClarityLow || over == Drag::ClarityHigh;
    const uint32_t id = band ? kClarityWidth : kColorWidth;
    const double dn = pk::wheelStep (e, host->table (), id);
    if (dn == 0.0)
        return;
    host->setOnce (id, std::clamp (host->norm (id) + (band ? dn : -dn), 0.0, 1.0));
    invalid ();
    e.consumed = true;
}

void ColorView::onMouseExitEvent (MouseExitEvent& e)
{
    if (auto* f = getFrame ())
        f->setCursor (kCursorDefault);
    e.consumed = true;
}

} // namespace smacheratr
