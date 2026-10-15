#include "CurveView.h"

#include "Dsp.h"

#include "pluginkit/ui/Theme.h"

#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cframe.h"
#include "vstgui/lib/cgraphicspath.h"
#include "vstgui/lib/events.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdio>
#include <string>

namespace sweepr {

using namespace VSTGUI;
namespace theme = pk::theme;

namespace {
constexpr double kHandleRadius = 6.0;
constexpr int kPoints = 240; // along the frequency axis
const char* const kNames[kBands] = {"A", "B", "C"};

void text (CDrawContext* ctx, const std::string& s, const CRect& r, const CColor& c, double size, CHoriTxtAlign a = kCenterText,
           bool bold = false)
{
    ctx->setFont (theme::font (size, bold));
    ctx->setFontColor (c);
    ctx->drawString (s.c_str (), r, a, true);
}
} // namespace

CurveView::CurveView (const CRect& r, pk::ParamHost* h, MeterSource m) : CView (r), host (h), meters (std::move (m)) {}

double CurveView::xOfHz (double hz) const
{
    const CRect r = getViewSize ();
    return r.left + std::log (std::clamp (hz, kMinHz, kMaxHz) / kMinHz) / std::log (kMaxHz / kMinHz) * r.getWidth ();
}

double CurveView::yOfDb (double db) const
{
    const CRect r = getViewSize ();
    return r.top + (kMaxDb - std::clamp (db, kMinDb, kMaxDb)) / (kMaxDb - kMinDb) * (r.getHeight () - kScaleH);
}

CPoint CurveView::handle (int b) const
{
    return CPoint (xOfHz (host->plainValue (bandParam (b, kFreq))), yOfDb (host->plainValue (bandParam (b, kGain))));
}

double CurveView::liveHz (int b) const
{
    if (live () && shownHz[b] > 0.0f)
        return shownHz[b];
    return host->plainValue (bandParam (b, kFreq));
}

double CurveView::responseDb (double f, const double* centres) const
{
    std::complex<double> h = 1.0;
    for (int b = 0; b < kBands; ++b)
        h *= dsp::bellResponse (f, centres[b], host->plainValue (bandParam (b, kQ)), host->plainValue (bandParam (b, kGain)), shownSr);
    return 20.0 * std::log10 (std::max (1e-6, std::abs (h)));
}

void CurveView::paintGrid (CDrawContext* ctx)
{
    const CRect all = getViewSize ();
    ctx->setFillColor (theme::kWell);
    ctx->drawRect (all, kDrawFilled);
    const double plotBottom = all.bottom - kScaleH;
    ctx->setLineWidth (1.0);
    // the octave grid of a classic EQ screen, 31 Hz to 16 kHz
    const double freqs[] = {31.25, 62.5, 125.0, 250.0, 500.0, 1000.0, 2000.0, 4000.0, 8000.0, 16000.0};
    const char* labels[] = {"31", "63", "125", "250", "500", "1k", "2k", "4k", "8k", "16k"};
    for (int i = 0; i < 10; ++i)
    {
        const double x = xOfHz (freqs[i]);
        ctx->setFrameColor (freqs[i] == 125.0 || freqs[i] == 1000.0 || freqs[i] == 8000.0 ? theme::kGridMajor : theme::kGridMinor);
        ctx->drawLine (CPoint (x, all.top), CPoint (x, plotBottom));
        text (ctx, labels[i], CRect (x - 20, plotBottom + 1, x + 20, all.bottom - 1), theme::kTextDim, 9.5);
    }
    for (double db : {-18.0, -12.0, -6.0, 0.0, 6.0, 12.0, 18.0})
    {
        const bool major = db == 0.0 || std::fabs (db) == 18.0;
        ctx->setFrameColor (db == 0.0 ? theme::kGridZero : (major ? theme::kGridMajor : theme::kGridMinor));
        ctx->drawLine (CPoint (all.left, yOfDb (db)), CPoint (all.right, yOfDb (db)));
        if (major)
        {
            char buf[8];
            std::snprintf (buf, sizeof (buf), db > 0.0 ? "+%.0f" : "%.0f", db);
            text (ctx, buf, CRect (all.left + 3, yOfDb (db) - 12, all.left + 31, yOfDb (db)), theme::kTextDim, 9.0, kLeftText);
        }
    }
}

void CurveView::draw (CDrawContext* ctx)
{
    const CRect all = getViewSize ();
    gridLayer.draw (ctx, all, 0, [this] (CDrawContext* c) { paintGrid (c); });
    ctx->setClipRect (all);
    const double plotBottom = all.bottom - kScaleH;

    double set[kBands], now[kBands];
    bool moved = false;
    for (int b = 0; b < kBands; ++b)
    {
        set[b] = host->plainValue (bandParam (b, kFreq));
        now[b] = liveHz (b);
        moved = moved || std::fabs (std::log (now[b] / set[b])) > 1e-3;
    }
    auto curve = [&] (const double* centres, bool fill, const CColor& stroke, double width, bool dashed) {
        auto path = owned (ctx->createGraphicsPath ());
        if (!path)
            return;
        const double base = yOfDb (0.0);
        for (int i = 0; i <= kPoints; ++i)
        {
            const double f = kMinHz * std::pow (kMaxHz / kMinHz, (double)i / kPoints);
            const CPoint pt (xOfHz (f), yOfDb (responseDb (f, centres)));
            if (i == 0)
            {
                path->beginSubpath (fill ? CPoint (pt.x, base) : pt);
                if (fill)
                    path->addLine (pt);
            }
            else
                path->addLine (pt);
        }
        if (fill)
        {
            path->addLine (CPoint (all.right, base));
            path->closeSubpath ();
            ctx->setFillColor (theme::withAlpha (theme::kCopper, 40));
            ctx->drawGraphicsPath (path, CDrawContext::kPathFilled);
        }
        ctx->setLineWidth (width);
        ctx->setFrameColor (stroke);
        if (dashed)
            ctx->setLineStyle (theme::dashed ());
        ctx->drawGraphicsPath (path, CDrawContext::kPathStroked);
        ctx->setLineStyle (kLineSolid);
    };

    // how far each LFO sweeps: a bar under its handle from Freq / 2^|Depth| to Freq x 2^|Depth|
    for (int b = 0; b < kBands; ++b)
    {
        const double depth = host->plainValue (lfoOnParam (b)) >= 0.5 ? std::fabs (host->plainValue (bandParam (b, kLfoDepth))) : 0.0;
        if (depth < 0.01)
            continue;
        const CPoint h = handle (b);
        const double f = host->plainValue (bandParam (b, kFreq));
        const double x0 = xOfHz (f / std::exp2 (depth)), x1 = xOfHz (f * std::exp2 (depth));
        const double y = std::min (plotBottom - 3.0, h.y + kHandleRadius + 5.0);
        ctx->setLineWidth (2.0);
        ctx->setFrameColor (theme::withAlpha (theme::kCopperPale, 110));
        ctx->drawLine (CPoint (x0, y), CPoint (x1, y));
        ctx->setLineWidth (1.0);
        ctx->drawLine (CPoint (x0, y - 3), CPoint (x0, y + 3));
        ctx->drawLine (CPoint (x1, y - 3), CPoint (x1, y + 3));
    }

    // the curve at the settings (dashed) under the curve now
    if (moved)
        curve (set, false, theme::withAlpha (theme::kCopperPale, 140), 1.0, true);
    curve (now, true, theme::kText, 1.5, false);

    // where each band is now: a cinnabar dot on the curve
    for (int b = 0; b < kBands; ++b)
    {
        const CPoint p (xOfHz (now[b]), yOfDb (responseDb (now[b], now)));
        ctx->setFillColor (theme::kEnergyLive);
        ctx->drawEllipse (CRect (p.x - 3.0, p.y - 3.0, p.x + 3.0, p.y + 3.0), kDrawFilled);
    }

    // the handles at the settings, named
    for (int b = 0; b < kBands; ++b)
    {
        const CPoint h = handle (b);
        pk::draw::handle (ctx, h, kHandleRadius, drag == b, true);
        text (ctx, kNames[b], CRect (h.x - 20, h.y - kHandleRadius - 14, h.x + 20, h.y - kHandleRadius - 2), theme::kCopperPale, 9.5,
              kCenterText, true);
    }

    // the readout: each band's settings, and where it is now (narrow: a line per band)
    std::string line[kBands], all3;
    std::string nowText = "now";
    for (int k = 0; k < kBands; ++k)
    {
        char buf[96];
        std::snprintf (buf, sizeof (buf), "%s %s  Q %s  %s", kNames[k], host->valueText (bandParam (k, kFreq)).c_str (),
                       host->valueText (bandParam (k, kQ)).c_str (), host->valueText (bandParam (k, kGain)).c_str ());
        line[k] = buf;
        all3 += (k ? "     " : "") + line[k];
        std::snprintf (buf, sizeof (buf), "%s %s %.0f Hz", k ? " /" : "", kNames[k], now[k]);
        nowText += buf;
    }
    // (at the top right, where the curve is usually flat; the scale's labels are at the left)
    if (all.getWidth () >= 860.0)
    {
        text (ctx, all3, CRect (all.left + 200, all.top + 4, all.right - 8, all.top + 18), theme::kText, 10.5, kRightText, true);
        if (live ())
            text (ctx, nowText, CRect (all.left + 200, all.top + 18, all.right - 8, all.top + 31), theme::kTextDim, 9.5, kRightText);
    }
    else
    {
        for (int k = 0; k < kBands; ++k)
            text (ctx, line[k], CRect (all.left + 36, all.top + 4 + 14 * k, all.right - 8, all.top + 18 + 14 * k), theme::kText, 10.5, kRightText,
                  true);
        if (live ())
            text (ctx, nowText, CRect (all.left + 36, all.top + 4 + 14 * kBands, all.right - 8, all.top + 17 + 14 * kBands), theme::kTextDim, 9.5,
                  kRightText);
    }
    ctx->setLineWidth (1.0);
    ctx->resetClipRect ();
}

int CurveView::hit (const CPoint& p) const
{
    // (the last band first: drawn last, so on top where they overlap)
    for (int b = kBands - 1; b >= 0; --b)
    {
        const CPoint h = handle (b);
        if (std::hypot (p.x - h.x, p.y - h.y) <= kHandleRadius + 4.0)
            return b;
    }
    return -1;
}

void CurveView::onMouseDownEvent (MouseDownEvent& e)
{
    const bool right = e.buttonState.isRight (); // a right click resets, like a double-click
    if (!e.buttonState.isLeft () && !right)
        return;
    drag = hit (e.mousePosition);
    if (drag < 0)
        return;
    const uint32_t fId = bandParam (drag, kFreq), qId = bandParam (drag, kQ), gId = bandParam (drag, kGain);
    if (e.clickCount == 2 || right)
    {
        for (uint32_t id : {fId, qId, gId})
            host->setOnce (id, host->table ().defaultNormalized (id));
        drag = -1;
        invalid ();
        e.consumed = true;
        e.ignoreFollowUpMoveAndUpEvents (true);
        return;
    }
    down = e.mousePosition;
    startFreq = host->plainValue (fId);
    startGain = host->plainValue (gId);
    startQ = host->norm (qId);
    dragQ = false;
    for (uint32_t id : {fId, qId, gId})
        host->beginEdit (id);
    e.consumed = true;
    invalid ();
}

void CurveView::onMouseMoveEvent (MouseMoveEvent& e)
{
    if (drag < 0)
    {
        if (auto* f = getFrame ())
            f->setCursor (hit (e.mousePosition) >= 0 ? kCursorSizeAll : kCursorDefault);
        return;
    }
    const CRect r = getViewSize ();
    const double fine = e.modifiers.has (ModifierKey::Shift) ? 0.2 : 1.0;
    const double dx = (e.mousePosition.x - down.x) * fine, dy = (e.mousePosition.y - down.y) * fine;
    const uint32_t fId = bandParam (drag, kFreq), qId = bandParam (drag, kQ), gId = bandParam (drag, kGain);
    if (e.modifiers.has (ModifierKey::Alt))
    {
        // Alt: up / down is Q (150 px for the whole range), the frequency and gain stay
        host->setNorm (qId, std::clamp (startQ - dy / 150.0, 0.0, 1.0));
        dragQ = true;
    }
    else
    {
        host->setNorm (fId, host->table ().toNormalized (fId, startFreq * std::pow (kMaxHz / kMinHz, dx / r.getWidth ())));
        const double dbPerPx = (kMaxDb - kMinDb) / (r.getHeight () - kScaleH);
        host->setNorm (gId, host->table ().toNormalized (gId, std::clamp (startGain - dy * dbPerPx, -18.0, 18.0)));
    }
    invalid ();
    e.consumed = true;
}

void CurveView::onMouseUpEvent (MouseUpEvent& e)
{
    if (drag < 0)
        return;
    for (uint32_t id : {bandParam (drag, kFreq), bandParam (drag, kQ), bandParam (drag, kGain)})
        host->endEdit (id);
    drag = -1;
    invalid ();
    e.consumed = true;
}

void CurveView::onMouseWheelEvent (MouseWheelEvent& e)
{
    // Q of the handle held or under the pointer
    const int target = drag >= 0 ? drag : hit (e.mousePosition);
    if (target < 0)
        return;
    const uint32_t qId = bandParam (target, kQ);
    const double dn = pk::wheelStep (e, host->table (), qId);
    if (dn == 0.0)
        return;
    const double v = std::clamp (host->norm (qId) + dn, 0.0, 1.0);
    if (drag >= 0)
    {
        startQ = std::clamp (startQ + dn, 0.0, 1.0);
        host->setNorm (qId, v);
    }
    else
        host->setOnce (qId, v);
    invalid ();
    e.consumed = true;
}

void CurveView::onMouseExitEvent (MouseExitEvent& e)
{
    if (auto* f = getFrame ())
        f->setCursor (kCursorDefault);
    e.consumed = true;
}

void CurveView::idle ()
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
    for (int b = 0; b < kBands; ++b)
    {
        const float hz = m->freqHz[(size_t)b].load (std::memory_order_relaxed);
        if (std::fabs (hz - shownHz[b]) > 0.002f * std::max (1.0f, shownHz[b]))
        {
            shownHz[b] = hz;
            changed = changed || live ();
        }
    }
    if (changed)
        invalid ();
}

} // namespace sweepr
