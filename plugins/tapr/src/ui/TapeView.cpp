#include "TapeView.h"

#include "CutShape.h"

#include "pluginkit/ui/Theme.h"
#include "sweepr/src/core/TapeShape.h"

#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cframe.h"
#include "vstgui/lib/cgraphicspath.h"
#include "vstgui/lib/events.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdio>
#include <string>

namespace tapr {

using namespace VSTGUI;
namespace theme = pk::theme;

namespace {
constexpr double kHandleRadius = 6.0;
constexpr int kPoints = 240; // along the frequency axis
constexpr double kCurveIn = 1.5; // the curve's box: -1.5 .. +1.5 in
const char* const kNames[cut::kBands] = {"LO", "MID", "HI"};

void text (CDrawContext* ctx, const std::string& s, const CRect& r, const CColor& c, double size, CHoriTxtAlign a = kCenterText,
           bool bold = false)
{
    ctx->setFont (theme::font (size, bold));
    ctx->setFontColor (c);
    ctx->drawString (s.c_str (), r, a, true);
}

// Speed's shelf at f for a filter at rate sr (the bilinear transform prewarped at its corner)
std::complex<double> shelfResponse (double speed, double f, double sr)
{
    const sweepr::tape::Shelf sh = sweepr::tape::shelfAt (speed);
    constexpr double kPi = 3.14159265358979323846;
    const double corner = std::min (sh.corner, 0.45 * sr), fr = std::clamp (f, 0.0, 0.4999 * sr);
    const std::complex<double> s (0.0, std::tan (kPi * fr / sr) / std::tan (kPi * corner / sr));
    const double g = std::pow (10.0, sh.gainDb / 20.0);
    return (g * s * s + sh.b1 * s + 1.0) / (s * s + sh.a1 * s + 1.0);
}
} // namespace

TapeView::TapeView (const CRect& r, pk::ParamHost* h, MeterSource m) : CView (r), host (h), meters (std::move (m)) {}

CRect TapeView::curveRect () const
{
    const CRect all = getViewSize ();
    const double side = all.getHeight () - 16.0;
    if (all.getWidth () < side + 300.0)
        return CRect ();
    return CRect (all.right - side - 8.0, all.top + 8.0, all.right - 8.0, all.bottom - 8.0);
}

CRect TapeView::responseRect () const
{
    const CRect all = getViewSize ();
    const CRect c = curveRect ();
    return c.isEmpty () ? all : CRect (all.left, all.top, c.left - 10.0, all.bottom);
}

double TapeView::xOfHz (double hz) const
{
    const CRect r = responseRect ();
    return r.left + std::log (std::clamp (hz, kMinHz, kMaxHz) / kMinHz) / std::log (kMaxHz / kMinHz) * r.getWidth ();
}

double TapeView::yOfDb (double db) const
{
    const CRect r = responseRect ();
    return r.top + (kMaxDb - std::clamp (db, kMinDb, kMaxDb)) / (kMaxDb - kMinDb) * (r.getHeight () - kScaleH);
}

CPoint TapeView::handle (int b) const
{
    return CPoint (xOfHz (host->plainValue (cutFreqParam (b))), yOfDb (host->plainValue (cutGainParam (b))));
}

double TapeView::responseDb (double f) const
{
    std::complex<double> h = 1.0;
    if (host->plainValue (kCutOn) >= 0.5)
        for (int b = 0; b < cut::kBands; ++b)
            h *= sweepr::bell::response (cut::prototype (host->plainValue (cutFreqParam (b)), host->plainValue (cutGainParam (b)), shownSr), f,
                                         shownSr);
    if (host->plainValue (kTapeOn) >= 0.5)
        h *= shelfResponse (host->plainValue (kSpeed), f, shownSr);
    return 20.0 * std::log10 (std::max (1e-6, std::abs (h)));
}

void TapeView::paintGrid (CDrawContext* ctx)
{
    const CRect all = getViewSize ();
    ctx->setFillColor (theme::kWell);
    ctx->drawRect (all, kDrawFilled);
    ctx->setLineWidth (1.0);
    const CRect r = responseRect ();
    const double plotBottom = r.bottom - kScaleH;
    const double freqs[] = {31.25, 62.5, 125.0, 250.0, 500.0, 1000.0, 2000.0, 4000.0, 8000.0, 16000.0};
    const char* labels[] = {"31", "63", "125", "250", "500", "1k", "2k", "4k", "8k", "16k"};
    for (int i = 0; i < 10; ++i)
    {
        const double x = xOfHz (freqs[i]);
        ctx->setFrameColor (freqs[i] == 125.0 || freqs[i] == 1000.0 || freqs[i] == 8000.0 ? theme::kGridMajor : theme::kGridMinor);
        ctx->drawLine (CPoint (x, r.top), CPoint (x, plotBottom));
        text (ctx, labels[i], CRect (x - 20, plotBottom + 1, x + 20, r.bottom - 1), theme::kTextDim, 9.5);
    }
    for (double db : {-18.0, -12.0, -6.0, 0.0, 6.0, 12.0, 18.0})
    {
        const bool major = db == 0.0 || std::fabs (db) == 18.0;
        ctx->setFrameColor (db == 0.0 ? theme::kGridZero : (major ? theme::kGridMajor : theme::kGridMinor));
        ctx->drawLine (CPoint (r.left, yOfDb (db)), CPoint (r.right, yOfDb (db)));
        if (major)
        {
            char buf[8];
            std::snprintf (buf, sizeof (buf), db > 0.0 ? "+%.0f" : "%.0f", db);
            text (ctx, buf, CRect (r.left + 3, yOfDb (db) - 12, r.left + 31, yOfDb (db)), theme::kTextDim, 9.0, kLeftText);
        }
    }
    // the curve's box: its axes and the curve itself (it never changes)
    const CRect c = curveRect ();
    if (c.isEmpty ())
        return;
    ctx->setFrameColor (theme::kGridMajor);
    ctx->drawRect (c, kDrawStroked);
    const CPoint mid = c.getCenter ();
    ctx->setFrameColor (theme::kGridZero);
    ctx->drawLine (CPoint (c.left, mid.y), CPoint (c.right, mid.y));
    ctx->drawLine (CPoint (mid.x, c.top), CPoint (mid.x, c.bottom));
    if (auto path = owned (ctx->createGraphicsPath ()))
    {
        for (int i = 0; i <= 120; ++i)
        {
            const double in = -kCurveIn + 2.0 * kCurveIn * i / 120.0;
            const CPoint pt (mid.x + in / kCurveIn * 0.5 * c.getWidth (), mid.y - sweepr::tape::curve (in) / kCurveIn * 0.5 * c.getHeight ());
            if (i == 0)
                path->beginSubpath (pt);
            else
                path->addLine (pt);
        }
        ctx->setLineWidth (1.5);
        ctx->setFrameColor (theme::kText);
        ctx->drawGraphicsPath (path, CDrawContext::kPathStroked);
        ctx->setLineWidth (1.0);
    }
    text (ctx, "CURVE", CRect (c.left + 6, c.top + 4, c.right - 6, c.top + 18), theme::kTextDim, 9.5, kLeftText, true);
}

void TapeView::draw (CDrawContext* ctx)
{
    const CRect all = getViewSize ();
    gridLayer.draw (ctx, all, 0, [this] (CDrawContext* c) { paintGrid (c); });
    ctx->setClipRect (all);
    const CRect r = responseRect ();

    // the response: a solid line over a faint fill
    if (auto path = owned (ctx->createGraphicsPath ()))
    {
        const double base = yOfDb (0.0);
        for (int i = 0; i <= kPoints; ++i)
        {
            const double f = kMinHz * std::pow (kMaxHz / kMinHz, (double)i / kPoints);
            const CPoint pt (xOfHz (f), yOfDb (responseDb (f)));
            if (i == 0)
            {
                path->beginSubpath (CPoint (pt.x, base));
                path->addLine (pt);
            }
            else
                path->addLine (pt);
        }
        path->addLine (CPoint (r.right, base));
        path->closeSubpath ();
        ctx->setFillColor (theme::withAlpha (theme::kCopper, 40));
        ctx->drawGraphicsPath (path, CDrawContext::kPathFilled);
        ctx->setLineWidth (1.5);
        ctx->setFrameColor (theme::kText);
        ctx->drawGraphicsPath (path, CDrawContext::kPathStroked);
        ctx->setLineWidth (1.0);
    }

    // the Cut bells' handles (dim while Cut is off)
    const bool cutOn = host->plainValue (kCutOn) >= 0.5;
    for (int b = 0; b < cut::kBands; ++b)
    {
        const CPoint h = handle (b);
        pk::draw::handle (ctx, h, kHandleRadius, drag == b, cutOn);
        text (ctx, kNames[b], CRect (h.x - 20, h.y - kHandleRadius - 14, h.x + 20, h.y - kHandleRadius - 2),
              cutOn ? theme::kCopperPale : theme::kTextDim, 9.5, kCenterText, true);
    }

    // the readout: where each part sits
    char buf[96];
    std::snprintf (buf, sizeof (buf), "Cut %s   Compression %s", host->plainValue (kCutPlace) >= 0.5 ? "Post" : "Pre",
                   host->plainValue (kCompPlace) >= 0.5 ? "Post" : "Pre");
    text (ctx, buf, CRect (r.left + 36, r.top + 4, r.right - 8, r.top + 18), theme::kText, 10.5, kRightText, true);

    // the curve's box: the level reaching the curve now, and the leveller's gain
    const CRect c = curveRect ();
    if (!c.isEmpty () && host->plainValue (kTapeOn) >= 0.5 && live ())
    {
        const CPoint mid = c.getCenter ();
        const double peak = std::min ((double)shownPeak, kCurveIn);
        const double x0 = mid.x - peak / kCurveIn * 0.5 * c.getWidth (), x1 = mid.x + peak / kCurveIn * 0.5 * c.getWidth ();
        ctx->setFrameColor (theme::kEnergyLive);
        ctx->setLineWidth (1.5);
        ctx->drawLine (CPoint (x0, c.top + 20), CPoint (x0, c.bottom - 4));
        ctx->drawLine (CPoint (x1, c.top + 20), CPoint (x1, c.bottom - 4));
        ctx->setLineWidth (1.0);
        std::snprintf (buf, sizeof (buf), "leveller %+.1f dB", shownLevel);
        text (ctx, buf, CRect (c.left + 6, c.bottom - 18, c.right - 6, c.bottom - 4), theme::kTextDim, 9.5, kRightText);
    }
    ctx->resetClipRect ();
}

int TapeView::hit (const CPoint& p) const
{
    for (int b = cut::kBands - 1; b >= 0; --b)
    {
        const CPoint h = handle (b);
        if (std::hypot (p.x - h.x, p.y - h.y) <= kHandleRadius + 4.0)
            return b;
    }
    return -1;
}

void TapeView::onMouseDownEvent (MouseDownEvent& e)
{
    const bool right = e.buttonState.isRight (); // a right click resets, like a double-click
    if (!e.buttonState.isLeft () && !right)
        return;
    drag = hit (e.mousePosition);
    if (drag < 0)
        return;
    const uint32_t gId = cutGainParam (drag), fId = cutFreqParam (drag);
    if (e.clickCount == 2 || right)
    {
        for (uint32_t id : {gId, fId})
            host->setOnce (id, host->table ().defaultNormalized (id));
        drag = -1;
        invalid ();
        e.consumed = true;
        e.ignoreFollowUpMoveAndUpEvents (true);
        return;
    }
    down = e.mousePosition;
    startGain = host->plainValue (gId);
    startFreq = host->plainValue (fId);
    host->beginEdit (gId);
    host->beginEdit (fId);
    e.consumed = true;
    invalid ();
}

void TapeView::onMouseMoveEvent (MouseMoveEvent& e)
{
    if (drag < 0)
    {
        if (auto* f = getFrame ())
            f->setCursor (hit (e.mousePosition) >= 0 ? kCursorSizeAll : kCursorDefault);
        return;
    }
    const double fine = e.modifiers.has (ModifierKey::Shift) ? 0.2 : 1.0;
    const double dx = (e.mousePosition.x - down.x) * fine, dy = (e.mousePosition.y - down.y) * fine;
    const double dbPerPx = (kMaxDb - kMinDb) / (responseRect ().getHeight () - kScaleH);
    const uint32_t gId = cutGainParam (drag), fId = cutFreqParam (drag);
    host->setNorm (gId, host->table ().toNormalized (gId, std::clamp (startGain - dy * dbPerPx, cut::kGainMin, cut::kGainMax)));
    // (the parameter's range keeps it in its band's span)
    host->setNorm (fId, host->table ().toNormalized (fId, startFreq * std::pow (kMaxHz / kMinHz, dx / responseRect ().getWidth ())));
    invalid ();
    e.consumed = true;
}

void TapeView::onMouseUpEvent (MouseUpEvent& e)
{
    if (drag < 0)
        return;
    host->endEdit (cutGainParam (drag));
    host->endEdit (cutFreqParam (drag));
    drag = -1;
    invalid ();
    e.consumed = true;
}

void TapeView::idle ()
{
    const Meters* m = meters ? meters () : nullptr;
    if (!m)
        return;
    const bool wasLive = live ();
    const uint32_t blocks = m->blocks.load (std::memory_order_acquire);
    if (blocks != lastBlocks)
    {
        lastBlocks = blocks;
        idleSinceBlock = 0;
    }
    else if (idleSinceBlock < (1 << 20))
        ++idleSinceBlock;
    bool changed = live () != wasLive;
    const double rate = m->sampleRate.load (std::memory_order_relaxed);
    if (rate > 1000.0 && rate != shownSr)
    {
        shownSr = rate;
        changed = true;
    }
    const float peak = m->curvePeak.load (std::memory_order_relaxed), level = m->levellerDb.load (std::memory_order_relaxed);
    if (std::fabs (peak - shownPeak) > 0.01f || std::fabs (level - shownLevel) > 0.1f)
    {
        shownPeak = peak;
        shownLevel = level;
        changed = changed || live ();
    }
    if (changed)
        invalid ();
}

} // namespace tapr
