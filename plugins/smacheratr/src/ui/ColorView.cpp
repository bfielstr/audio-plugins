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
CColor clarityColor (int band, uint8_t alpha = 255) { return ColorView::bandColor (band, alpha); }
// Clarity's gain at a frequency for a cut (dB) at the band's peak (with the band's phase, as it sounds)
double clarityGainDb (const ClarityBand& b, double hz, double sr, double cutDb) { return clarityCutAtDb (b, hz, sr, cutDb); }
} // namespace

// One colour for every Gentlr band (docs/THEME.md allows one metal and one energy colour): pale copper
// for their regions, edges, outlines and labels. The bands are told apart by where they sit and by
// their names (1, 2, Sub, High); the cut each makes right now is drawn in cinnabar by the views.
CColor ColorView::bandColor (int band, uint8_t alpha)
{
    (void)band;
    return theme::withAlpha (theme::kCopperPale, alpha);
}

ColorView::ColorView (const CRect& r, pk::ParamHost* h, RateSource rs, MeterSource ms)
    : CView (r), host (h), rate (std::move (rs)), meters (std::move (ms))
{
}

double ColorView::sampleRate () const
{
    const double r = rate ? rate () : 0.0;
    return r > 1000.0 ? r : 48000.0;
}

bool ColorView::clarityShown (int) const { return host->plainValue (kClarity) >= 0.5; } // (every band, Sub and High too)

GentlrLayout ColorView::layoutNow () const
{
    GentlrLayout l = readLayout (host, smacheratrBandParams ());
    if (host->plainValue (kClarityNoOverlap) >= 0.5)
        resolveOverlaps (l);
    return l;
}

ClarityBand ColorView::bandOf (int band) const
{
    const GentlrLayout l = layoutNow ();
    if (band == kSubBand)
        return subBand (sampleRate (), l.freq[band]);
    if (band == kHighBand)
        return highBand (sampleRate (), l.freq[band]);
    return clarityBand (sampleRate (), l.freq[band], l.width[band], claritySlopeOf (host->plainValue (kClaritySlope)));
}

bool ColorView::clarityOn (int band) const { return smacheratrBandParams ().works (host, band); }

void ColorView::idle ()
{
    const Meters* m = meters ? meters () : nullptr;
    bool changed = false;
    for (int k = 0; k < kGentlrBands; ++k)
    {
        const float cut = !m ? 0.0f : clarityCutMeter (*m, k).load (std::memory_order_relaxed);
        const float target = clarityOn (k) ? cut : 0.0f;
        const float before = shownCut[k];
        shownCut[k] += (target - shownCut[k]) * 0.35f;
        if (std::fabs (shownCut[k]) < 0.01f)
            shownCut[k] = 0.0f;
        changed |= std::fabs (shownCut[k] - before) > 0.005f;
    }
    if (changed)
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

CPoint ColorView::clarityHandle (int band) const
{
    return CPoint (xOfHz (layoutNow ().freq[band]), yOfDb (-host->plainValue (kGentlrRangeIds[band])));
}

double ColorView::clarityEdgeX (int band, bool high) const
{
    const ClarityBand b = bandOf (band);
    return xOfHz (high ? b.highHz : b.lowHz);
}

void ColorView::draw (CDrawContext* ctx)
{
    const CRect all = getViewSize ();
    const bool on = host->plainValue (kColorOn) >= 0.5;
    ctx->setFillColor (theme::kWell);
    ctx->drawRect (all, kDrawFilled);
    ctx->setClipRect (all);

    // grid
    ctx->setLineWidth (1.0);
    for (double f : {50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0})
    {
        const bool major = f == 100.0 || f == 1000.0 || f == 10000.0;
        ctx->setFrameColor (major ? theme::kGridMajor : theme::kGridMinor);
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
        ctx->setFrameColor (db == 0.0 ? theme::kGridZero : theme::kGridMinor);
        ctx->drawLine (CPoint (all.left, yOfDb (db)), CPoint (all.right, yOfDb (db)));
    }

    // Gentlr's bands, behind the curves: their ranges shaded, their edges (drag them for the width)
    bool clarity[kGentlrBands];
    ClarityBand bands[kGentlrBands];
    for (int k = 0; k < kGentlrBands; ++k)
    {
        clarity[k] = clarityOn (k);
        bands[k] = bandOf (k);
        if (!clarity[k])
            continue;
        const double x0 = xOfHz (bands[k].lowHz), x1 = xOfHz (bands[k].highHz);
        ctx->setFillColor (clarityColor (k, 16));
        ctx->drawRect (CRect (x0, all.top, x1, all.bottom), kDrawFilled);
        ctx->setLineWidth (1.0);
        const bool edgeActive = dragBand == k && (drag == Drag::ClarityLow || drag == Drag::ClarityHigh || drag == Drag::ClarityWidth);
        ctx->setFrameColor (edgeActive ? theme::kEnergyLive : clarityColor (k, 90));
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
        // the colour filters' response: a faint copper body under a text-coloured trace (dim when off)
        ctx->setFillColor (on ? theme::withAlpha (theme::kCopper, 40) : theme::withAlpha (theme::kLineDim, 60));
        ctx->drawGraphicsPath (path, CDrawContext::kPathFilled);
        ctx->setLineWidth (1.0);
        ctx->setFrameColor (on ? theme::kText : theme::kTextDim);
        ctx->drawGraphicsPath (path, CDrawContext::kPathStroked);
    }

    // Gentlr at work, as a multiband compressor shows its bands: the most each can cut outlined, the
    // cut it is making right now filled in from the 0 dB line, a handle at its centre and its readout
    int pills = 0;
    for (int k = 0; k < kGentlrBands; ++k)
    {
        if (!clarity[k])
        {
            // Gentlr on, this band's Range at 0 (flat at 0 dB): a dim handle to pull down
            if (clarityShown (k))
            {
                pk::draw::handle (ctx, clarityHandle (k), kHandleRadius, false, false);
            }
            continue;
        }
        const ClarityBand& band = bands[k];
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
        if (auto range = gainPath (-host->plainValue (kGentlrRangeIds[k]), false))
        {
            ctx->setLineWidth (1.0);
            ctx->setFrameColor (clarityColor (k, 110));
            ctx->setLineStyle (theme::dashed ());
            ctx->drawGraphicsPath (range, CDrawContext::kPathStroked);
            ctx->setLineStyle (kLineSolid);
        }
        if (shownCut[k] < -0.05f)
            if (auto live = gainPath (shownCut[k], true))
            {
                // the cut it makes now: the lit part of the display
                ctx->setFillColor (theme::withAlpha (theme::kEnergyLive, 70));
                ctx->drawGraphicsPath (live, CDrawContext::kPathFilled);
                ctx->setLineWidth (1.0);
                ctx->setFrameColor (theme::kEnergyLive);
                ctx->drawGraphicsPath (live, CDrawContext::kPathStroked);
            }
        const CPoint h = clarityHandle (k);
        // the readout, in a pill above the band (a second pill goes under the first)
        char cb[64];
        std::snprintf (cb, sizeof (cb), "%s  %s   %.1f dB", k == 0 ? "Gentlr" : k == 1 ? "Gentlr 2" : k == kSubBand ? "Sub" : "High",
                       host->valueText (kGentlrFreqIds[k]).c_str (), (double)shownCut[k]);
        const double pw = 160.0, px = std::clamp (h.x - pw / 2, all.left + 4, all.right - pw - 4);
        const double py = all.top + 36 + 20 * pills++;
        const CRect pill (px, py, px + pw, py + 16);
        // the band's name in the pill tells it apart: a well behind the text in a copper outline
        ctx->setFillColor (theme::withAlpha (theme::kWell, 220));
        ctx->drawRect (pill, kDrawFilled);
        pk::draw::outline (ctx, pill, theme::kCopper, 0);
        text (ctx, cb, pill, theme::kCopperPale, 9.5, kCenterText, true);
        pk::draw::handle (ctx, h, kHandleRadius + 1, drag != Drag::None && dragBand == k);
    }

    // handles
    for (const CPoint& h : {loHandle (), hiHandle ()})
        pk::draw::handle (ctx, h, kHandleRadius, false, on);

    // labels
    text (ctx, on ? "COLOR" : "COLOR  (off)", CRect (all.left + 6, all.top + 4, all.right - 6, all.top + 18),
          on ? theme::kCopperPale : theme::kTextDim, 10.5, kLeftText, true);
    char buf[96];
    std::snprintf (buf, sizeof (buf), "Lo %s   Hi %s @ %s", host->valueText (kColorLo).c_str (),
                   host->valueText (kColorHi).c_str (), host->valueText (kColorFreq).c_str ());
    text (ctx, buf, CRect (all.left + 6, all.top + 19, all.right - 6, all.top + 32), theme::kTextDim, 9.5, kLeftText);
    ctx->setLineWidth (1.0);
    ctx->resetClipRect ();
}

int ColorView::bandUnder (const CPoint& p) const
{
    // the second band is drawn on top, so it is found first (Sub and High have no width to drag: skipped)
    for (int k = kClarityBands - 1; k >= 0; --k)
        if (clarityOn (k) && p.x >= clarityEdgeX (k, false) - 4.0 && p.x <= clarityEdgeX (k, true) + 4.0)
            return k;
    return -1;
}

ColorView::Drag ColorView::hit (const CPoint& p, int* band) const
{
    auto near = [&] (const CPoint& h) { return std::hypot (p.x - h.x, p.y - h.y) <= kHandleRadius + 4.0; };
    if (near (hiHandle ()))
        return Drag::Hi;
    if (near (loHandle ()))
        return Drag::Lo;
    // the second band is drawn on top, so it is found first
    for (int k = kGentlrBands - 1; k >= 0; --k)
    {
        if (!clarityShown (k))
            continue;
        Drag d = Drag::None;
        if (near (clarityHandle (k)))
            d = Drag::Clarity;
        else if (!hasWidth (k))
            continue; // (Sub, High) no edges to grab
        else if (clarityOn (k) && std::fabs (p.x - clarityEdgeX (k, false)) <= 4.0)
            d = Drag::ClarityLow;
        else if (clarityOn (k) && std::fabs (p.x - clarityEdgeX (k, true)) <= 4.0)
            d = Drag::ClarityHigh;
        if (d != Drag::None)
        {
            if (band)
                *band = k;
            return d;
        }
    }
    return Drag::None;
}

void ColorView::onMouseDownEvent (MouseDownEvent& e)
{
    const bool right = e.buttonState.isRight (); // a right click resets, like a double-click
    if (!e.buttonState.isLeft () && !right)
        return;
    drag = hit (e.mousePosition, &dragBand);
    // Alt (Option) held on a band, its handle or anywhere in its region: a sideways drag sets its width
    if (!right && e.clickCount < 2 && e.modifiers.has (ModifierKey::Alt) && drag != Drag::Lo && drag != Drag::Hi)
    {
        const int k = drag != Drag::None ? dragBand : bandUnder (e.mousePosition);
        if (k >= 0 && hasWidth (k))
        {
            drag = Drag::ClarityWidth;
            dragBand = k;
        }
    }
    if (drag == Drag::None)
        return;
    const bool isSub = !hasWidth (dragBand); // (Sub or High: no width)
    const uint32_t cFreq = kGentlrFreqIds[dragBand], cRange = kGentlrRangeIds[dragBand];
    const uint32_t cWidth = isSub ? 0u : kClarityWidthIds[dragBand]; // Sub and High have none (never used)
    const bool onBand = drag == Drag::Clarity || drag == Drag::ClarityLow || drag == Drag::ClarityHigh || drag == Drag::ClarityWidth;
    if (onBand && onBandPicked)
        onBandPicked (dragBand);
    if (e.clickCount == 2 || right)
    {
        if (drag == Drag::Clarity || drag == Drag::ClarityLow || drag == Drag::ClarityHigh)
        {
            const GentlrLayout before = readLayout (host, smacheratrBandParams ());
            host->setOnce (cFreq, host->table ().defaultNormalized (cFreq));
            if (!isSub)
                host->setOnce (cWidth, host->table ().defaultNormalized (cWidth));
            if (drag == Drag::Clarity)
                host->setOnce (cRange, host->table ().defaultNormalized (cRange));
            pushOnce (host, smacheratrBandParams (), dragBand, before); // (No Overlap: the band back where it was pushes too)
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
    movedH = movedV = false;
    if (drag == Drag::Clarity)
    {
        host->beginEdit (cFreq);
        host->beginEdit (cRange);
        push.begin (host, smacheratrBandParams (), dragBand, {cFreq, cRange}); // (No Overlap: first splits what overlaps)
    }
    else if (drag == Drag::ClarityLow || drag == Drag::ClarityHigh || drag == Drag::ClarityWidth)
    {
        host->beginEdit (cWidth);
        push.begin (host, smacheratrBandParams (), dragBand, {cWidth});
    }
    startLo = host->plainValue (kColorLo);
    startHi = host->plainValue (kColorHi);
    startFreq = host->plainValue (kColorFreq);
    startClarity = host->plainValue (cFreq);
    startRange = host->plainValue (cRange);
    startWidth = isSub ? 0.0 : host->plainValue (cWidth);
    if (drag == Drag::Lo)
        host->beginEdit (kColorLo);
    else if (drag == Drag::Hi)
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
        int hb = 0;
        Drag h = hit (e.mousePosition, &hb);
        if (e.modifiers.has (ModifierKey::Alt) && h != Drag::Lo && h != Drag::Hi &&
            (h != Drag::None ? hasWidth (hb) : bandUnder (e.mousePosition) >= 0))
            h = Drag::ClarityWidth; // Alt: the band's width, sideways
        if (auto* f = getFrame ())
            f->setCursor (h == Drag::Hi || h == Drag::Clarity ? kCursorSizeAll
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
    {
        // sideways the frequency, down the Range (the handle follows the depth of the cut)
        const double hz = startClarity * std::pow (kMaxHz / kMinHz, dx / r.getWidth ());
        setPlain (kGentlrFreqIds[dragBand], dragBand == kSubBand    ? std::clamp (hz, kSubMinHz, kSubMaxHz)
                                            : dragBand == kHighBand ? std::clamp (hz, kHighMinHz, kHighMaxHz)
                                                                    : hz);
        const double dbPerPixel = kMaxDb / (r.getHeight () * 0.5 - 12.0);
        setPlain (kGentlrRangeIds[dragBand], std::clamp (startRange + dy * dbPerPixel, 0.0, 24.0));
        push.update (host, smacheratrBandParams ());
    }
    else if (drag == Drag::ClarityWidth)
    {
        // Alt-drag: right widens, left narrows, an octave of width for an octave of mouse travel (the
        // band stays centred; Shift: fine)
        const double octavesPerPixel = std::log2 (kMaxHz / kMinHz) / r.getWidth ();
        setPlain (kClarityWidthIds[dragBand], startWidth + dx * octavesPerPixel);
        push.update (host, smacheratrBandParams ());
    }
    else if (drag == Drag::ClarityLow || drag == Drag::ClarityHigh)
    {
        // the edge follows the mouse; the band stays centred, so the width is twice the distance
        const double hz = kMinHz * std::pow (kMaxHz / kMinHz, (e.mousePosition.x - r.left) / r.getWidth ());
        setPlain (kClarityWidthIds[dragBand], 2.0 * std::fabs (std::log2 (hz / host->plainValue (kClarityFreqIds[dragBand]))));
        push.update (host, smacheratrBandParams ());
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
    push.end (host);
    if (drag == Drag::Clarity)
    {
        host->endEdit (kGentlrFreqIds[dragBand]);
        host->endEdit (kGentlrRangeIds[dragBand]);
    }
    else if (drag == Drag::ClarityLow || drag == Drag::ClarityHigh || drag == Drag::ClarityWidth)
        host->endEdit (kClarityWidthIds[dragBand]);
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
    // Gentlr's band: wheel up widens it
    int overBand = dragBand;
    const Drag over = drag != Drag::None ? drag : hit (e.mousePosition, &overBand);
    if (!hasWidth (overBand) && over != Drag::None && over != Drag::Lo && over != Drag::Hi)
        return; // Sub and High have no width
    const bool band = over == Drag::Clarity || over == Drag::ClarityLow || over == Drag::ClarityHigh || over == Drag::ClarityWidth;
    const uint32_t id = band ? kClarityWidthIds[overBand] : kColorWidth;
    const double dn = pk::wheelStep (e, host->table (), id);
    if (dn == 0.0)
        return;
    const GentlrLayout before = readLayout (host, smacheratrBandParams ());
    host->setOnce (id, std::clamp (host->norm (id) + (band ? dn : -dn), 0.0, 1.0));
    if (band)
    {
        if (push.active ())
            push.update (host, smacheratrBandParams ());
        else
            pushOnce (host, smacheratrBandParams (), overBand, before);
    }
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
