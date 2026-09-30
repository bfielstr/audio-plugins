#include "DeeprView.h"

#include "pluginkit/ui/Theme.h"

#include "smacheratr/src/core/Biquad.h"

#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cframe.h"
#include "vstgui/lib/cgraphicspath.h"
#include "vstgui/lib/events.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdio>
#include <string>

namespace deepr {

using namespace VSTGUI;
namespace theme = pk::theme;

namespace {
const CColor kSub (255, 176, 64);  // the sub band (amber)
const CColor kDip (90, 150, 255);  // the dip (blue: a cut, as everywhere in the suite)

void text (CDrawContext* ctx, const std::string& s, const CRect& r, const CColor& c, double size,
           CHoriTxtAlign a = kCenterText, bool bold = false)
{
    ctx->setFont (theme::font (size, bold));
    ctx->setFontColor (c);
    ctx->drawString (s.c_str (), r, a, true);
}

CColor withAlpha (CColor c, uint8_t a)
{
    c.alpha = a;
    return c;
}

// |H| in dB of a biquad at hz, and the dip built on the band-pass: 1 - (1 - g) * BP
std::complex<double> response (const smacheratr::BiquadCoeffs& c, double sr, double hz)
{
    const double w = 2.0 * M_PI * hz / sr;
    const std::complex<double> z1 = std::polar (1.0, -w), z2 = z1 * z1;
    return (c.b0 + c.b1 * z1 + c.b2 * z2) / (1.0 + c.a1 * z1 + c.a2 * z2);
}
} // namespace

DeeprView::DeeprView (const CRect& r, pk::ParamHost* h, RateSource rs, MeterSource ms)
: CView (r), host (h), rate (std::move (rs)), meters (std::move (ms))
{
}

double DeeprView::sampleRate () const
{
    const double sr = rate ? rate () : 48000.0;
    return sr > 1000.0 ? sr : 48000.0;
}

CRect DeeprView::plot () const
{
    CRect r = getViewSize ();
    r.right -= kMeterWidth + 6.0;
    r.bottom -= kAxisHeight;
    return r;
}

CRect DeeprView::meter () const
{
    const CRect all = getViewSize ();
    return CRect (all.right - kMeterWidth, all.top, all.right, all.bottom - kAxisHeight);
}

double DeeprView::xOfHz (double hz) const
{
    const CRect r = plot ();
    return r.left + std::log (std::clamp (hz, kMinHz, kMaxHz) / kMinHz) / std::log (kMaxHz / kMinHz) * r.getWidth ();
}

double DeeprView::yOfDb (double db) const
{
    const CRect r = plot ();
    return r.top + (kMaxDb - std::clamp (db, kMinDb, kMaxDb)) / (kMaxDb - kMinDb) * r.getHeight ();
}

double DeeprView::yOfLevel (double db) const
{
    const CRect m = meter ();
    const double top = m.top + 6.0, bottom = m.bottom - 4.0;
    return top + (kMeterMaxDb - std::clamp (db, kMeterMinDb, kMeterMaxDb)) / (kMeterMaxDb - kMeterMinDb) * (bottom - top);
}

CPoint DeeprView::handle () const
{
    return CPoint (xOfHz (host->plainValue (kDipFreq)), yOfDb (-host->plainValue (kDepth)));
}

void DeeprView::draw (CDrawContext* ctx)
{
    const CRect all = getViewSize ();
    const CRect pr = plot ();
    const CRect mr = meter ();
    ctx->setFillColor (theme::kBackground);
    ctx->drawRect (all, kDrawFilled);
    ctx->setFillColor (theme::kWaveBg);
    ctx->drawRect (CRect (pr.left, pr.top, pr.right, pr.bottom), kDrawFilled);
    ctx->drawRect (mr, kDrawFilled);
    ctx->setClipRect (all);
    ctx->setLineWidth (1.0);

    // grid
    for (double f : {30.0, 40.0, 50.0, 60.0, 80.0, 100.0, 200.0, 300.0, 400.0, 500.0, 800.0, 1000.0})
    {
        const bool major = f == 50.0 || f == 100.0 || f == 500.0 || f == 1000.0;
        ctx->setFrameColor (major ? CColor (58, 58, 64) : theme::kGrid);
        ctx->drawLine (CPoint (xOfHz (f), pr.top), CPoint (xOfHz (f), pr.bottom));
        char buf[16];
        std::snprintf (buf, sizeof (buf), f >= 1000 ? "%.0fk" : "%.0f", f >= 1000 ? f / 1000 : f);
        if (major || f == 30.0 || f == 200.0)
            text (ctx, buf, CRect (xOfHz (f) - 20, pr.bottom, xOfHz (f) + 20, pr.bottom + kAxisHeight), theme::kTextDim, 9.5);
    }
    for (double db = -12.0; db <= 0.0; db += 3.0)
    {
        ctx->setFrameColor (db == 0.0 ? CColor (70, 70, 76) : theme::kGrid);
        ctx->drawLine (CPoint (pr.left, yOfDb (db)), CPoint (pr.right, yOfDb (db)));
        char buf[16];
        std::snprintf (buf, sizeof (buf), "%.0f", db);
        text (ctx, buf, CRect (pr.left + 3, yOfDb (db) - 12, pr.left + 40, yOfDb (db)), theme::kTextDim, 9.0, kLeftText);
    }

    // the sub band
    const double split = host->plainValue (kSplit);
    const double xs = xOfHz (split);
    ctx->setFillColor (withAlpha (kSub, 34));
    ctx->drawRect (CRect (pr.left, pr.top, xs, pr.bottom), kDrawFilled);
    ctx->setLineWidth (2.0);
    ctx->setFrameColor (drag == Drag::Split ? theme::kTextBright : kSub);
    ctx->drawLine (CPoint (xs, pr.top), CPoint (xs, pr.bottom));
    ctx->setLineWidth (1.0);
    text (ctx, "sub", CRect (pr.left, pr.bottom - 18, xs, pr.bottom - 4), kSub, 10.0);

    // the dip: dashed at full Depth, filled with the dip it is making now
    const double sr = sampleRate ();
    const double dipHz = host->plainValue (kDipFreq), depth = host->plainValue (kDepth);
    const smacheratr::BiquadCoeffs bp = smacheratr::bandPass (sr, std::min (dipHz, 0.4 * sr), dipQ (host->plainValue (kDipWidth)));
    auto dipPath = [&] (double gainDb, bool closed) -> SharedPointer<CGraphicsPath> {
        auto path = owned (ctx->createGraphicsPath ());
        if (!path)
            return path;
        const double g = std::pow (10.0, gainDb / 20.0);
        const int n = std::max (2, (int)pr.getWidth () / 2);
        for (int i = 0; i <= n; ++i)
        {
            const double x = pr.left + pr.getWidth () * i / n;
            const double hz = kMinHz * std::pow (kMaxHz / kMinHz, (double)i / n);
            const double mag = std::abs (1.0 - (1.0 - g) * response (bp, sr, hz));
            const CPoint pt (x, yOfDb (20.0 * std::log10 (std::max (1e-9, mag))));
            if (i == 0)
            {
                path->beginSubpath (closed ? CPoint (x, yOfDb (0.0)) : pt);
                if (closed)
                    path->addLine (pt);
            }
            else
                path->addLine (pt);
        }
        if (closed)
        {
            path->addLine (CPoint (pr.right, yOfDb (0.0)));
            path->closeSubpath ();
        }
        return path;
    };
    if (depth > 0.01)
        if (auto full = dipPath (-depth, false))
        {
            ctx->setFrameColor (withAlpha (kDip, 150));
            ctx->setLineStyle (CLineStyle (CLineStyle::kLineCapButt, CLineStyle::kLineJoinMiter, 0.0, {3.0, 3.0}));
            ctx->drawGraphicsPath (full, CDrawContext::kPathStroked);
            ctx->setLineStyle (kLineSolid);
        }
    const bool working = shownCut < -0.05f;
    if (working)
        if (auto live = dipPath (shownCut, true))
        {
            ctx->setFillColor (withAlpha (kDip, 90));
            ctx->drawGraphicsPath (live, CDrawContext::kPathFilled);
            ctx->setLineWidth (1.6);
            ctx->setFrameColor (kDip);
            ctx->drawGraphicsPath (live, CDrawContext::kPathStroked);
            ctx->setLineWidth (1.0);
        }

    // the handle, at the full depth
    const CPoint hp = handle ();
    const bool held = drag == Drag::Handle || drag == Drag::Width;
    const CRect hr (hp.x - 6, hp.y - 6, hp.x + 6, hp.y + 6);
    ctx->setFillColor (held ? theme::kTextBright : theme::kAccent);
    ctx->drawEllipse (hr, kDrawFilled);
    ctx->setFrameColor (theme::kWaveBg);
    ctx->drawEllipse (hr, kDrawStroked);

    // readout
    char buf[96];
    std::snprintf (buf, sizeof (buf), "Dip %.1f dB  @ %.0f Hz", (double)shownCut, dipHz);
    text (ctx, buf, CRect (pr.left + 44, pr.top + 4, pr.left + 300, pr.top + 20), working ? theme::kTextBright : theme::kText, 11.0, kLeftText, true);
    std::snprintf (buf, sizeof (buf), "Depth %.1f dB   Width %.1f oct", depth, host->plainValue (kDipWidth));
    text (ctx, buf, CRect (pr.left + 44, pr.top + 20, pr.left + 300, pr.top + 34), theme::kTextDim, 9.5, kLeftText);

    // the sub's level: the key region (threshold .. +12 dB), the level, the threshold
    const double th = host->plainValue (kThreshold);
    const double bar = mr.left + 10, barR = mr.right - 10;
    for (double db = -48.0; db <= -12.0; db += 12.0)
    {
        ctx->setFrameColor (theme::kGrid);
        ctx->drawLine (CPoint (mr.left + 2, yOfLevel (db)), CPoint (mr.right - 2, yOfLevel (db)));
    }
    ctx->setFillColor (withAlpha (kDip, working ? 110 : 40));
    ctx->drawRect (CRect (mr.left + 2, yOfLevel (th + kKeyRangeDb), mr.right - 2, yOfLevel (th)), kDrawFilled);
    if (shownSub > kMeterMinDb)
    {
        ctx->setFillColor (withAlpha (kSub, working ? 255 : 190));
        ctx->drawRect (CRect (bar, yOfLevel (shownSub), barR, yOfLevel (kMeterMinDb)), kDrawFilled);
    }
    const double yt = yOfLevel (th);
    ctx->setLineWidth (2.0);
    ctx->setFrameColor (drag == Drag::Threshold ? theme::kTextBright : theme::kText);
    ctx->drawLine (CPoint (mr.left + 2, yt), CPoint (mr.right - 2, yt));
    ctx->setLineWidth (1.0);
    std::snprintf (buf, sizeof (buf), "%.0f", th);
    const bool labelBelow = yt - 14 < mr.top;
    text (ctx, buf, labelBelow ? CRect (mr.left, yt + 2, mr.right, yt + 14) : CRect (mr.left, yt - 14, mr.right, yt - 2),
          theme::kTextBright, 9.5);
    text (ctx, "sub dB", CRect (mr.left - 6, mr.bottom, mr.right, mr.bottom + kAxisHeight), theme::kTextDim, 9.0);
    ctx->resetClipRect ();
}

DeeprView::Drag DeeprView::hit (const CPoint& p) const
{
    const CPoint hp = handle ();
    if (std::hypot (p.x - hp.x, p.y - hp.y) <= 9.0)
        return Drag::Handle;
    if (meter ().pointInside (p))
        return Drag::Threshold;
    const CRect pr = plot ();
    if (p.y >= pr.top && p.y <= pr.bottom && std::fabs (p.x - xOfHz (host->plainValue (kSplit))) <= 5.0)
        return Drag::Split;
    return Drag::None;
}

void DeeprView::onMouseDownEvent (MouseDownEvent& e)
{
    const bool right = e.buttonState.isRight (); // a right click resets, like a double-click
    if (!e.buttonState.isLeft () && !right)
        return;
    const Drag h = hit (e.mousePosition);
    if (h == Drag::None)
        return;
    e.consumed = true;
    if (e.clickCount == 2 || right)
    {
        auto reset = [this] (uint32_t id) { host->setOnce (id, host->table ().defaultNormalized (id)); };
        if (h == Drag::Handle)
        {
            reset (kDipFreq);
            reset (kDepth);
            reset (kDipWidth);
        }
        else
            reset (h == Drag::Split ? kSplit : kThreshold);
        drag = Drag::None;
        invalid ();
        e.ignoreFollowUpMoveAndUpEvents (true);
        return;
    }
    drag = h == Drag::Handle && e.modifiers.has (ModifierKey::Alt) ? Drag::Width : h;
    down = e.mousePosition;
    startFreq = host->plainValue (kDipFreq);
    startDepth = host->plainValue (kDepth);
    startWidth = host->plainValue (kDipWidth);
    startSplit = host->plainValue (kSplit);
    startThreshold = host->plainValue (kThreshold);
    switch (drag)
    {
        case Drag::Handle:
            host->beginEdit (kDipFreq);
            host->beginEdit (kDepth);
            break;
        case Drag::Width: host->beginEdit (kDipWidth); break;
        case Drag::Split: host->beginEdit (kSplit); break;
        case Drag::Threshold: host->beginEdit (kThreshold); break;
        default: break;
    }
    invalid ();
}

void DeeprView::onMouseMoveEvent (MouseMoveEvent& e)
{
    if (drag == Drag::None)
    {
        Drag h = hit (e.mousePosition);
        if (h == Drag::Handle && e.modifiers.has (ModifierKey::Alt))
            h = Drag::Width;
        if (auto* f = getFrame ())
            f->setCursor (h == Drag::Handle ? kCursorSizeAll
                          : h == Drag::Threshold ? kCursorVSize
                          : h != Drag::None ? kCursorHSize
                                            : kCursorDefault);
        return;
    }
    const double fine = e.modifiers.has (ModifierKey::Shift) ? 0.2 : 1.0;
    const double dx = (e.mousePosition.x - down.x) * fine, dy = (e.mousePosition.y - down.y) * fine;
    const CRect pr = plot ();
    const double octaves = std::log2 (kMaxHz / kMinHz) * dx / pr.getWidth (); // mouse travel in octaves of the axis
    auto setPlain = [this] (uint32_t id, double v) {
        host->setNorm (id, std::clamp (host->table ().toNormalized (id, v), 0.0, 1.0));
    };
    switch (drag)
    {
        case Drag::Handle:
            setPlain (kDipFreq, std::clamp (startFreq * std::pow (2.0, octaves), 80.0, 800.0));
            setPlain (kDepth, std::clamp (startDepth + dy * (kMaxDb - kMinDb) / pr.getHeight (), 0.0, 12.0));
            break;
        case Drag::Width: setPlain (kDipWidth, std::clamp (startWidth + octaves, 0.5, 4.0)); break;
        case Drag::Split: setPlain (kSplit, std::clamp (startSplit * std::pow (2.0, octaves), 40.0, 200.0)); break;
        case Drag::Threshold:
        {
            const double span = yOfLevel (kMeterMinDb) - yOfLevel (kMeterMaxDb);
            setPlain (kThreshold, std::clamp (startThreshold - dy * (kMeterMaxDb - kMeterMinDb) / span, -60.0, 0.0));
            break;
        }
        default: break;
    }
    invalid ();
    e.consumed = true;
}

void DeeprView::onMouseUpEvent (MouseUpEvent& e)
{
    switch (drag)
    {
        case Drag::None: return;
        case Drag::Handle:
            host->endEdit (kDipFreq);
            host->endEdit (kDepth);
            break;
        case Drag::Width: host->endEdit (kDipWidth); break;
        case Drag::Split: host->endEdit (kSplit); break;
        case Drag::Threshold: host->endEdit (kThreshold); break;
    }
    drag = Drag::None;
    invalid ();
    e.consumed = true;
}

void DeeprView::onMouseWheelEvent (MouseWheelEvent& e)
{
    // the wheel while the handle is held, or with Shift over it (as on every filter handle in these
    // plug-ins), sets the dip's width: up widens it
    const bool active = drag == Drag::Handle || drag == Drag::Width ||
                        (drag == Drag::None && e.modifiers.has (ModifierKey::Shift) && hit (e.mousePosition) == Drag::Handle);
    if (!active)
        return;
    const double dn = pk::wheelStep (e, host->table (), kDipWidth);
    if (dn == 0.0)
        return;
    host->setOnce (kDipWidth, std::clamp (host->norm (kDipWidth) + dn, 0.0, 1.0));
    invalid ();
    e.consumed = true;
}

void DeeprView::onMouseExitEvent (MouseExitEvent& e)
{
    if (auto* f = getFrame ())
        f->setCursor (kCursorDefault);
    e.consumed = true;
}

void DeeprView::idle ()
{
    const Meters* m = meters ? meters () : nullptr;
    const float cut = m ? m->cutDb.load (std::memory_order_relaxed) : 0.0f;
    const float sub = m ? m->subDb.load (std::memory_order_relaxed) : -120.0f;
    const float oldCut = shownCut, oldSub = shownSub;
    shownCut += (cut - shownCut) * 0.5f;
    shownSub += (sub - shownSub) * (sub > shownSub ? 0.6f : 0.2f);
    if (std::fabs (shownCut) < 0.01f)
        shownCut = 0.0f;
    if (std::fabs (shownCut - oldCut) > 0.01f || std::fabs (shownSub - oldSub) > 0.05f)
        invalid ();
}

} // namespace deepr
