#include "BandView.h"

#include "pluginkit/ui/Theme.h"

#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cframe.h"
#include "vstgui/lib/cgraphicspath.h"
#include "vstgui/lib/events.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace dropr {

using namespace VSTGUI;
namespace theme = pk::theme;

namespace {
constexpr double kPointRadius = 5.0, kGrab = 5.0;
// One energy colour (docs/THEME.md): the meters carry it (the level in energy idle, the level after the
// gain lit live), the gain reduction is a pale copper outline lane hanging from 0 dB; the thresholds are
// pale copper lines told apart by style (downward solid, upward dashed) and by their labels.
const CColor kLevel = theme::kEnergyIdle;     // the band's level
const CColor kLevelOut = theme::kEnergyLive;  // the band's level after the gain
const CColor kReduce = theme::kCopperPale;    // gain reduction
const CColor kDown = theme::kCopperPale;      // the downward threshold (solid)
const CColor kUp = theme::kCopperPale;        // the upward threshold (dashed)
const CColor kCurve = theme::kText;           // the gain points' curve
const CColor kXover = theme::kCopper;         // crossover handles

CColor withAlpha (CColor c, uint8_t a)
{
    c.alpha = a;
    return c;
}

void text (CDrawContext* ctx, const std::string& s, const CRect& r, const CColor& c, double size,
           CHoriTxtAlign a = kCenterText, bool bold = false)
{
    ctx->setFont (theme::font (size, bold));
    ctx->setFontColor (c);
    ctx->drawString (s.c_str (), r, a, true);
}

std::string hzText (double hz)
{
    char buf[32];
    if (hz >= 1000.0)
        std::snprintf (buf, sizeof (buf), hz >= 10000.0 ? "%.1f kHz" : "%.2f kHz", hz / 1000.0);
    else
        std::snprintf (buf, sizeof (buf), "%.0f Hz", hz);
    return buf;
}

// a Catmull-Rom curve through (x[i], y[i]), flat beyond the ends
double smoothAt (const std::vector<double>& x, const std::vector<double>& y, double at)
{
    const size_t n = x.size ();
    if (n == 0)
        return 0.0;
    if (n == 1 || at <= x[0])
        return y[0];
    if (at >= x[n - 1])
        return y[n - 1];
    size_t i = 0;
    while (i + 2 < n && at > x[i + 1])
        ++i;
    const double h = x[i + 1] - x[i];
    const double t = h > 1e-9 ? (at - x[i]) / h : 0.0;
    auto slope = [&] (size_t k) {
        if (k == 0 || k == n - 1)
            return 0.0; // flat at the ends
        return (y[k + 1] - y[k - 1]) / (x[k + 1] - x[k - 1]);
    };
    const double m0 = slope (i) * h, m1 = slope (i + 1) * h;
    const double t2 = t * t, t3 = t2 * t;
    return (2 * t3 - 3 * t2 + 1) * y[i] + (t3 - 2 * t2 + t) * m0 + (-2 * t3 + 3 * t2) * y[i + 1] + (t3 - t2) * m1;
}
} // namespace

BandView::BandView (const CRect& r, pk::ParamHost* h, MeterSource m) : CView (r), host (h), meters (std::move (m))
{
    for (int k = 0; k < kMaxBands; ++k)
    {
        shownLevel[k] = shownOut[k] = -150.0f;
        shownGain[k] = 0.0f;
    }
}

CRect BandView::plot () const
{
    const CRect r = getViewSize ();
    return CRect (r.left + kAxisLeft, r.top + kTop, r.right - kPad, r.bottom - kAxisBottom - 4.0);
}

double BandView::xOfHz (double hz) const
{
    const CRect p = plot ();
    return p.left + std::log (std::clamp (hz, kMinXoverHz, kMaxXoverHz) / kMinXoverHz) / std::log (kMaxXoverHz / kMinXoverHz) * p.getWidth ();
}

double BandView::hzAt (double x) const
{
    const CRect p = plot ();
    const double t = std::clamp ((x - p.left) / p.getWidth (), 0.0, 1.0);
    return kMinXoverHz * std::pow (kMaxXoverHz / kMinXoverHz, t);
}

double BandView::yOfDb (double db) const
{
    const CRect p = plot ();
    return p.top + (kTopDb - std::clamp (db, kBottomDb, kTopDb)) / (kTopDb - kBottomDb) * p.getHeight ();
}

double BandView::dbAt (double y) const
{
    const CRect p = plot ();
    return kTopDb - (y - p.top) / p.getHeight () * (kTopDb - kBottomDb);
}

int BandView::bands () const { return std::clamp ((int)std::lround (host->plainValue (kBands)), 1, kMaxBands); }

void BandView::xovers (double* f) const
{
    xoversFrom ([this] (uint32_t id) { return host->plainValue (id); }, f);
}

double BandView::tiltAt (int band) const
{
    double f[kNumXovers];
    xovers (f);
    return tiltDb (host->plainValue (kTilt), bandCentre (f, band, bands ()));
}

CPoint BandView::pointPos (int band) const
{
    double f[kNumXovers];
    xovers (f);
    const int n = bands ();
    return CPoint (xOfHz (bandCentre (f, band, n)), yOfDb (host->plainValue (kBandGain1 + (uint32_t)band) + tiltAt (band)));
}

double BandView::xoverX (int j) const
{
    double f[kNumXovers];
    xovers (f);
    return xOfHz (f[j]);
}

BandView::Hit BandView::hitAt (const CPoint& pt) const
{
    const CRect p = plot ();
    if (!getViewSize ().pointInside (pt))
        return {};
    const int n = bands ();
    for (int k = 0; k < n; ++k)
    {
        const CPoint q = pointPos (k);
        if (std::hypot (pt.x - q.x, pt.y - q.y) <= kPointRadius + 3.0)
            return {Target::Point, k};
    }
    for (int j = 0; j + 1 < n; ++j)
        if (std::fabs (pt.x - xoverX (j)) <= kGrab && pt.y >= p.top - 4 && pt.y <= p.bottom)
            return {Target::Xover, j};
    if (std::fabs (pt.y - yOfDb (host->plainValue (kDownThreshold))) <= kGrab)
        return {Target::DownThreshold, 0};
    if (std::fabs (pt.y - yOfDb (host->plainValue (kUpThreshold))) <= kGrab)
        return {Target::UpThreshold, 0};
    return {};
}

uint32_t BandView::paramOf (const Hit& h) const
{
    switch (h.what)
    {
        case Target::Point: return kBandGain1 + (uint32_t)h.index;
        case Target::Xover: return kXover1 + (uint32_t)h.index;
        case Target::DownThreshold: return kDownThreshold;
        case Target::UpThreshold: return kUpThreshold;
        default: return kNumParams;
    }
}

void BandView::setPlain (uint32_t id, double v) { host->setNorm (id, host->table ().toNormalized (id, v)); }

void BandView::idle ()
{
    const Meters* m = meters ? meters () : nullptr;
    if (!m)
        return;
    bool dirty = false;
    for (int k = 0; k < kMaxBands; ++k)
    {
        auto follow = [&] (float& shown, float now, float fall) {
            // up at once, down at `fall` dB per frame
            const float next = now > shown ? now : std::max (now, shown - fall);
            if (std::fabs (next - shown) > 0.05f)
            {
                shown = next;
                dirty = true;
            }
        };
        follow (shownLevel[k], m->levelDb[k].load (std::memory_order_relaxed), 3.0f);
        follow (shownOut[k], m->outDb[k].load (std::memory_order_relaxed), 3.0f);
        // the gain reduction: down at once, back up at 3 dB per frame
        const float g = m->gainDb[k].load (std::memory_order_relaxed);
        const float next = g < shownGain[k] ? g : std::min (g, shownGain[k] + 3.0f);
        if (std::fabs (next - shownGain[k]) > 0.05f)
        {
            shownGain[k] = next;
            dirty = true;
        }
    }
    if (dirty)
        invalid ();
}

void BandView::paintBase (CDrawContext* ctx)
{
    const CRect all = getViewSize ();
    const CRect pr = plot ();
    ctx->setFillColor (theme::kWell);
    ctx->drawRect (all, kDrawFilled);
    ctx->setLineWidth (1.0);
    char buf[96];

    const int n = bands ();
    double f[kNumXovers];
    xovers (f);

    // the bands: shaded columns
    for (int k = 0; k < n; ++k)
    {
        const double x0 = xOfHz (bandLow (f, k)), x1 = xOfHz (bandHigh (f, k, n));
        if (k % 2 == 1)
        {
            ctx->setFillColor (withAlpha (theme::kCopper, 12));
            ctx->drawRect (CRect (x0, pr.top, x1, pr.bottom), kDrawFilled);
        }
    }

    // grid
    for (double hz : {50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0})
    {
        const double x = xOfHz (hz);
        ctx->setFrameColor (theme::kGridMinor);
        ctx->drawLine (CPoint (x, pr.top), CPoint (x, pr.bottom));
        std::snprintf (buf, sizeof (buf), hz >= 1000.0 ? "%.0fk" : "%.0f", hz >= 1000.0 ? hz / 1000.0 : hz);
        text (ctx, buf, CRect (x - 20, pr.bottom + 3, x + 20, pr.bottom + 3 + kAxisBottom), theme::kTextDim, 9.5);
    }
    for (double db = kTopDb; db >= kBottomDb - 0.1; db -= 12.0)
    {
        const double y = yOfDb (db);
        ctx->setFrameColor (std::fabs (db) < 0.1 ? theme::kGridZero : theme::kGridMinor);
        ctx->drawLine (CPoint (pr.left, y), CPoint (pr.right, y));
        std::snprintf (buf, sizeof (buf), "%+.0f", db);
        text (ctx, std::fabs (db) < 0.1 ? "0" : buf, CRect (all.left + 1, y - 7, pr.left - 4, y + 7), theme::kTextDim, 9.0, kRightText);
    }
}

void BandView::draw (CDrawContext* ctx)
{
    const CRect all = getViewSize ();
    const CRect pr = plot ();
    baseLayer.draw (ctx, all, pk::LayerKey ().params (host), [this] (CDrawContext* c) { paintBase (c); });
    ctx->setClipRect (all);
    ctx->setLineWidth (1.0);
    char buf[96];

    const int n = bands ();
    double f[kNumXovers];
    xovers (f);

    // the meters: level (after the Input gain), level after the gain, gain reduction from 0 dB
    for (int k = 0; k < n; ++k)
    {
        const double x0 = xOfHz (bandLow (f, k)), x1 = xOfHz (bandHigh (f, k, n));
        const double w = x1 - x0, mx = 0.5 * (x0 + x1);
        // three lanes: the level (energy idle), the gain reduction (a pale copper outline, from 0 dB
        // down), the level after the gain (energy live)
        const double lane = std::max (3.0, std::min (16.0, w * 0.2));
        if (shownLevel[k] > kBottomDb)
        {
            ctx->setFillColor (kLevel);
            ctx->drawRect (CRect (mx - 1.5 * lane, yOfDb (shownLevel[k]), mx - 0.5 * lane, pr.bottom), kDrawFilled);
        }
        if (shownGain[k] < -0.05f)
        {
            const CRect gr (mx - 0.5 * lane + 1, yOfDb (0.0), mx + 0.5 * lane - 1, yOfDb (shownGain[k]));
            ctx->setFillColor (withAlpha (kReduce, 40));
            ctx->drawRect (gr, kDrawFilled);
            pk::draw::outline (ctx, gr, kReduce, 0);
        }
        if (shownOut[k] > kBottomDb)
        {
            ctx->setFillColor (kLevelOut);
            ctx->drawRect (CRect (mx + 0.5 * lane, yOfDb (shownOut[k]), mx + 1.5 * lane, pr.bottom), kDrawFilled);
        }
        std::snprintf (buf, sizeof (buf), "%.1f", (double)std::min (0.0f, shownGain[k]));
        text (ctx, shownGain[k] < -0.05f ? buf : "0.0", CRect (mx - 30, pr.top + 1, mx + 30, pr.top + 13),
              shownGain[k] < -0.05f ? kReduce : theme::kTextDim, 9.0);
    }

    // the thresholds (and Negative mode's floor)
    const double downT = host->plainValue (kDownThreshold), upT = host->plainValue (kUpThreshold);
    const bool upOn = host->plainValue (kUpRatio) > 1.001;
    const bool neg = host->plainValue (kNegative) >= 0.5;
    auto hline = [&] (double db, CColor c, double width, bool dotted) {
        const double y = yOfDb (db);
        ctx->setFrameColor (c);
        ctx->setLineWidth (width);
        if (dotted)
        {
            for (double x = pr.left; x < pr.right; x += 6.0)
                ctx->drawLine (CPoint (x, y), CPoint (std::min (pr.right, x + 3.0), y));
        }
        else
            ctx->drawLine (CPoint (pr.left, y), CPoint (pr.right, y));
        ctx->setLineWidth (1.0);
    };
    const bool downHot = drag.what == Target::DownThreshold || hover.what == Target::DownThreshold;
    const bool upHot = drag.what == Target::UpThreshold || hover.what == Target::UpThreshold;
    // held or hovered, a threshold lights cinnabar; the upward one is dashed (dim line while it is off)
    ctx->setLineStyle (theme::dashed ());
    hline (upT, upHot ? theme::kEnergyLive : (upOn ? kUp : theme::kLineDim), 1.0, false);
    ctx->setLineStyle (kLineSolid);
    hline (downT, downHot ? theme::kEnergyLive : kDown, downHot ? 1.5 : 1.0, false);
    if (neg)
        hline (downT - host->plainValue (kRange), withAlpha (theme::kCopper, 170), 1.0, true);
    auto tag = [&] (const char* s, double db, CColor c, bool right) {
        const double w = 7.0 + 5.6 * (double)std::strlen (s), y = yOfDb (db);
        const CRect box = right ? CRect (pr.right - w - 2, y - 14, pr.right - 2, y - 1) : CRect (pr.left + 2, y - 14, pr.left + 2 + w, y - 1);
        ctx->setFillColor (withAlpha (theme::kWell, 200));
        ctx->drawRect (box, kDrawFilled);
        text (ctx, s, box, c, 9.5);
    };
    std::snprintf (buf, sizeof (buf), "Down %.1f dB", downT);
    tag (buf, downT, kDown, true);
    if (neg)
    {
        std::snprintf (buf, sizeof (buf), "floor %.1f dB", downT - host->plainValue (kRange));
        tag (buf, downT - host->plainValue (kRange), theme::kTextDim, true);
    }
    std::snprintf (buf, sizeof (buf), upOn ? "Up %.1f dB" : "Up %.1f dB (off)", upT);
    tag (buf, upT, upOn ? kUp : theme::kTextDim, false);

    // crossover handles
    for (int j = 0; j + 1 < n; ++j)
    {
        const double x = xOfHz (f[j]);
        const bool hot = (drag.what == Target::Xover || hover.what == Target::Xover) &&
                         (drag.what == Target::Xover ? drag.index : hover.index) == j;
        ctx->setFrameColor (hot ? theme::kEnergyLive : withAlpha (kXover, 170));
        ctx->drawLine (CPoint (x, pr.top), CPoint (x, pr.bottom));
        // the grip at the foot: an outlined tab, lit while held or hovered
        pk::draw::outline (ctx, CRect (x - 3, pr.bottom - 14, x + 4, pr.bottom), hot ? theme::kEnergyLive : kXover, 0);
        if (hot)
            text (ctx, hzText (f[j]), CRect (x - 40, pr.bottom - 28, x + 40, pr.bottom - 16), theme::kText, 9.5);
    }

    // the gain points and the curve through them
    std::vector<double> px, py;
    for (int k = 0; k < n; ++k)
    {
        const CPoint q = pointPos (k);
        px.push_back (q.x);
        py.push_back (host->plainValue (kBandGain1 + (uint32_t)k) + tiltAt (k));
    }
    if (auto line = owned (ctx->createGraphicsPath ()))
    {
        for (double x = pr.left; x <= pr.right + 0.5; x += 2.0)
        {
            const CPoint q (x, yOfDb (smoothAt (px, py, x)));
            if (x == pr.left)
                line->beginSubpath (q);
            else
                line->addLine (q);
        }
        ctx->setLineWidth (1.0);
        ctx->setFrameColor (withAlpha (kCurve, 220));
        ctx->drawGraphicsPath (line, CDrawContext::kPathStroked);
    }
    for (int k = 0; k < n; ++k)
    {
        const CPoint q (px[(size_t)k], yOfDb (py[(size_t)k]));
        const bool hot = (drag.what == Target::Point && drag.index == k) || (hover.what == Target::Point && hover.index == k);
        pk::draw::handle (ctx, q, kPointRadius, hot);
        if (hot)
        {
            std::snprintf (buf, sizeof (buf), "Band %d  %+.1f dB", k + 1, host->plainValue (kBandGain1 + (uint32_t)k));
            text (ctx, buf, CRect (q.x - 60, q.y - 22, q.x + 60, q.y - 8), theme::kText, 9.5);
        }
    }

    // title and readouts
    text (ctx, "BANDS", CRect (all.left + 6, all.top + 4, all.left + 80, all.top + 18), theme::kCopperPale, 10.5, kLeftText, true);
    if (neg)
        std::snprintf (buf, sizeof (buf), "Ratio %s   floor %.1f dB   Input %+.1f dB   Makeup %+.1f dB", negRatioText (host->plainValue (kNegRatio)).c_str (),
                       downT - host->plainValue (kRange), host->plainValue (kInput), host->plainValue (kMakeup));
    else
        std::snprintf (buf, sizeof (buf), "Ratio %s   Input %+.1f dB   Makeup %+.1f dB", host->valueText (kDownRatio).c_str (),
                       host->plainValue (kInput), host->plainValue (kMakeup));
    text (ctx, buf, CRect (all.left + 60, all.top + 5, all.right - kPad, all.top + 18), theme::kTextDim, 9.5, kLeftText);
    ctx->resetClipRect ();
}

void BandView::onMouseDownEvent (MouseDownEvent& e)
{
    const bool right = e.buttonState.isRight ();
    if (!e.buttonState.isLeft () && !right)
        return;
    if (drag.what != Target::None) // a drag the host never ended
    {
        MouseUpEvent up;
        onMouseUpEvent (up);
    }
    const Hit h = hitAt (e.mousePosition);
    if (h.what == Target::None)
        return;
    const uint32_t id = paramOf (h);
    if (right || e.clickCount == 2)
    {
        host->setOnce (id, host->table ().defaultNormalized (id));
        invalid ();
        e.consumed = true;
        e.ignoreFollowUpMoveAndUpEvents (true);
        return;
    }
    drag = h;
    host->beginEdit (id);
    invalid ();
    e.consumed = true;
}

void BandView::onMouseMoveEvent (MouseMoveEvent& e)
{
    if (drag.what == Target::None)
    {
        const Hit h = hitAt (e.mousePosition);
        if (h.what != hover.what || h.index != hover.index)
        {
            hover = h;
            invalid ();
        }
        if (auto* fr = getFrame ())
            fr->setCursor (h.what == Target::Xover ? kCursorHSize : h.what == Target::None ? kCursorDefault : kCursorVSize);
        return;
    }
    const uint32_t id = paramOf (drag);
    switch (drag.what)
    {
        case Target::Point:
            setPlain (id, std::clamp (dbAt (e.mousePosition.y) - tiltAt (drag.index), -36.0, 36.0));
            break;
        case Target::Xover:
        {
            double f[kNumXovers];
            xovers (f);
            const double lo = drag.index > 0 ? f[drag.index - 1] * kMinXoverGap : kMinXoverHz;
            const double hi = drag.index + 1 < kNumXovers ? f[drag.index + 1] / kMinXoverGap : kMaxXoverHz;
            setPlain (id, std::clamp (hzAt (e.mousePosition.x), lo, std::max (lo, hi)));
            break;
        }
        case Target::DownThreshold:
        case Target::UpThreshold: setPlain (id, std::clamp (dbAt (e.mousePosition.y), -96.0, 0.0)); break;
        default: break;
    }
    invalid ();
    e.consumed = true;
}

void BandView::onMouseCancelEvent (MouseCancelEvent& e)
{
    MouseUpEvent up;
    onMouseUpEvent (up); // closes the edit of the drag
    e.consumed = true;
}

void BandView::onMouseUpEvent (MouseUpEvent& e)
{
    if (drag.what != Target::None)
        host->endEdit (paramOf (drag));
    drag = {};
    invalid ();
    e.consumed = true;
}

void BandView::onMouseExitEvent (MouseExitEvent& e)
{
    hover = {};
    if (auto* fr = getFrame ())
        fr->setCursor (kCursorDefault);
    invalid ();
    e.consumed = true;
}

} // namespace dropr
