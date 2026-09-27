#include "Displays.h"

#include "Filter.h"
#include "Params.h"
#include "Theme.h"

#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cgraphicspath.h"
#include "vstgui/lib/events.h"

#include <algorithm>
#include <cmath>

namespace simplr {

using namespace VSTGUI;

namespace {

void label (CDrawContext* ctx, const std::string& s, const CRect& r, const CColor& c, double size,
            CHoriTxtAlign a = kLeftText, bool bold = false)
{
    ctx->setFont (theme::font (size, bold));
    ctx->setFontColor (c);
    ctx->drawString (s.c_str (), r, a, true);
}

void background (CDrawContext* ctx, const CRect& r)
{
    ctx->setFillColor (theme::kWaveBg);
    ctx->drawRect (r, kDrawFilled);
    ctx->setFrameColor (theme::kPanelEdge);
    ctx->setLineWidth (1.0);
    ctx->drawRect (r, kDrawStroked);
}

double expCurve (double u)
{
    const double k = 4.0, endY = std::exp (-k);
    return 1.0 - (std::exp (-k * u) - endY) / (1.0 - endY); // 0 -> 1
}

} // namespace

//==============================================================================
FilterDisplay::FilterDisplay (const CRect& r, ParamHost* h) : CView (r), host (h) {}

CRect FilterDisplay::toggleRect (int i) const
{
    const CRect r = getViewSize ();
    return CRect (r.right - 84 + i * 40, r.top + 4, r.right - 46 + i * 40, r.top + 18);
}

void FilterDisplay::draw (CDrawContext* ctx)
{
    const CRect r = getViewSize ();
    background (ctx, r);
    const bool on = host->plainValue (kFilterOn) >= 0.5;
    CRect a = r;
    a.inset (6, 6);
    a.top += 12;

    if (showEnv)
    {
        CPoint handles[3];
        EnvelopeDisplay::drawAdsr (ctx, a, host, 1, handles, !on);
        label (ctx, "Env Amount " + host->valueText (kFilterEnvAmt), CRect (r.left + 6, r.top + 3, r.right - 90, r.top + 17),
               theme::kTextDim, 10.0);
    }
    else
    {
        ctx->setClipRect (r);
        auto xOf = [&] (double f) { return a.left + std::log (f / 20.0) / std::log (1000.0) * a.getWidth (); };
        auto yOf = [&] (double db) { return a.top + (24.0 - std::clamp (db, -60.0, 24.0)) / 72.0 * a.getHeight (); };
        ctx->setLineWidth (1.0);
        for (double f : {50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0})
        {
            ctx->setFrameColor (f == 100.0 || f == 1000.0 || f == 10000.0 ? CColor (60, 60, 64) : theme::kGrid);
            ctx->drawLine (CPoint (xOf (f), a.top), CPoint (xOf (f), a.bottom));
        }
        for (double db : {12.0, 0.0, -12.0, -24.0, -36.0})
        {
            ctx->setFrameColor (db == 0.0 ? CColor (70, 70, 76) : theme::kGrid);
            ctx->drawLine (CPoint (a.left, yOf (db)), CPoint (a.right, yOf (db)));
        }
        FilterSettings fs;
        fs.type = (int)std::lround (host->plainValue (kFilterType));
        fs.circuit = (int)std::lround (host->plainValue (kFilterCircuit));
        fs.slope24 = std::lround (host->plainValue (kFilterSlope)) == 1;
        fs.cutoff = (float)host->plainValue (kFilterFreq);
        fs.res = (float)host->plainValue (kFilterRes);
        fs.morph = (float)host->plainValue (kFilterMorph);
        auto path = owned (ctx->createGraphicsPath ());
        if (path)
        {
            const int n = (int)a.getWidth ();
            path->beginSubpath (CPoint (a.left, a.bottom));
            for (int i = 0; i <= n; ++i)
            {
                const double f = 20.0 * std::pow (1000.0, (double)i / n);
                path->addLine (CPoint (a.left + i, yOf (filterResponseDb (fs, (float)f))));
            }
            path->addLine (CPoint (a.right, a.bottom));
            path->closeSubpath ();
            ctx->setFillColor (on ? CColor (255, 164, 40, 45) : CColor (120, 120, 120, 30));
            ctx->drawGraphicsPath (path, CDrawContext::kPathFilled);
            auto line = owned (ctx->createGraphicsPath ());
            for (int i = 0; i <= n; ++i)
            {
                const double f = 20.0 * std::pow (1000.0, (double)i / n);
                const CPoint pt (a.left + i, yOf (filterResponseDb (fs, (float)f)));
                if (i == 0)
                    line->beginSubpath (pt);
                else
                    line->addLine (pt);
            }
            ctx->setLineWidth (1.5);
            ctx->setFrameColor (on ? theme::kCurve : theme::kTextDim);
            ctx->drawGraphicsPath (line, CDrawContext::kPathStroked);
        }
        const double cx = xOf (std::clamp ((double)fs.cutoff, 20.0, 20000.0));
        const double cy = yOf (filterResponseDb (fs, fs.cutoff));
        ctx->setFillColor (on ? theme::kTextBright : theme::kTextDim);
        ctx->drawEllipse (CRect (cx - 3.5, cy - 3.5, cx + 3.5, cy + 3.5), kDrawFilled);
        std::string info = host->valueText (kFilterFreq) + "   Res " + host->valueText (kFilterRes);
        if (!circuitSupported (fs.type, fs.circuit))
            info += "   (circuit uses Clean for this type)";
        label (ctx, info, CRect (r.left + 6, r.top + 3, r.right - 90, r.top + 17), theme::kTextDim, 10.0);
        ctx->resetClipRect ();
    }
    if (!on)
        label (ctx, "Filter Off", r, theme::kTextDim, 12.0, kCenterText, true);

    for (int i = 0; i < 2; ++i)
    {
        const CRect t = toggleRect (i);
        const bool sel = (i == 1) == showEnv;
        ctx->setFillColor (sel ? theme::kKnobTrack : theme::kControlBg);
        ctx->drawRect (t, kDrawFilled);
        label (ctx, i == 0 ? "Freq" : "Env", t, sel ? theme::kTextBright : theme::kTextDim, 9.5, kCenterText);
    }
}

void FilterDisplay::onMouseDownEvent (MouseDownEvent& e)
{
    if (!e.buttonState.isLeft ())
        return;
    for (int i = 0; i < 2; ++i)
        if (toggleRect (i).pointInside (e.mousePosition))
        {
            showEnv = i == 1;
            invalid ();
            e.consumed = true;
            e.ignoreFollowUpMoveAndUpEvents (true);
            return;
        }
    if (showEnv)
        return;
    dragging = true;
    last = e.mousePosition;
    freqN = host->norm (kFilterFreq);
    resN = host->norm (kFilterRes);
    host->beginEdit (kFilterFreq);
    host->beginEdit (kFilterRes);
    e.consumed = true;
}

void FilterDisplay::onMouseMoveEvent (MouseMoveEvent& e)
{
    if (!dragging)
        return;
    const CRect r = getViewSize ();
    const double fine = e.modifiers.has (ModifierKey::Shift) ? 0.2 : 1.0;
    freqN = std::clamp (freqN + (e.mousePosition.x - last.x) / r.getWidth () * fine, 0.0, 1.0);
    resN = std::clamp (resN - (e.mousePosition.y - last.y) / r.getHeight () * fine, 0.0, 1.0);
    last = e.mousePosition;
    host->setNorm (kFilterFreq, freqN);
    host->setNorm (kFilterRes, resN);
    invalid ();
    e.consumed = true;
}

void FilterDisplay::onMouseUpEvent (MouseUpEvent& e)
{
    if (!dragging)
        return;
    dragging = false;
    host->endEdit (kFilterFreq);
    host->endEdit (kFilterRes);
    e.consumed = true;
}

//==============================================================================
EnvelopeDisplay::EnvelopeDisplay (const CRect& r, ParamHost* h, int w) : CView (r), host (h), which (w) {}

uint32_t EnvelopeDisplay::idA () const { return which == 0 ? kAmpA : (which == 1 ? kFiltA : kPitchA); }

void EnvelopeDisplay::drawAdsr (CDrawContext* ctx, const CRect& a, ParamHost* host, int which, CPoint* handles,
                                bool dim)
{
    const uint32_t base = which == 0 ? kAmpA : (which == 1 ? kFiltA : kPitchA);
    const double at = host->plainValue (base), dt = host->plainValue (base + 1), s = host->plainValue (base + 2),
                 rt = host->plainValue (base + 3);
    auto w = [] (double ms) { return std::log10 (1.0 + ms / 2.0) + 0.05; };
    const double units = w (at) + w (dt) + 1.2 + w (rt);
    const double scale = a.getWidth () / units;
    const double x0 = a.left, x1 = x0 + w (at) * scale, x2 = x1 + w (dt) * scale, x3 = x2 + 1.2 * scale,
                 x4 = x3 + w (rt) * scale;
    auto y = [&] (double v) { return a.bottom - v * a.getHeight (); };

    ctx->setLineWidth (1.0);
    ctx->setFrameColor (theme::kGrid);
    for (double v : {0.25, 0.5, 0.75})
        ctx->drawLine (CPoint (a.left, y (v)), CPoint (a.right, y (v)));

    auto path = owned (ctx->createGraphicsPath ());
    if (!path)
        return;
    path->beginSubpath (CPoint (x0, y (0)));
    path->addLine (CPoint (x1, y (1)));
    for (int i = 1; i <= 24; ++i)
    {
        const double u = i / 24.0;
        path->addLine (CPoint (x1 + (x2 - x1) * u, y (1.0 + (s - 1.0) * expCurve (u))));
    }
    path->addLine (CPoint (x3, y (s)));
    for (int i = 1; i <= 24; ++i)
    {
        const double u = i / 24.0;
        path->addLine (CPoint (x3 + (x4 - x3) * u, y (s * (1.0 - expCurve (u)))));
    }
    auto fill = owned (ctx->createGraphicsPath ());
    ctx->setLineWidth (1.5);
    ctx->setFrameColor (dim ? theme::kTextDim : theme::kCurve);
    ctx->drawGraphicsPath (path, CDrawContext::kPathStroked);
    // sustain segment marker
    ctx->setFrameColor (CColor (255, 255, 255, 40));
    ctx->drawLine (CPoint (x2, a.top), CPoint (x2, a.bottom));
    ctx->drawLine (CPoint (x3, a.top), CPoint (x3, a.bottom));

    const CPoint h[3] = {CPoint (x1, y (1)), CPoint (x2, y (s)), CPoint (x4, y (0))};
    for (int i = 0; i < 3; ++i)
    {
        if (handles)
            handles[i] = h[i];
        ctx->setFillColor (dim ? theme::kTextDim : theme::kTextBright);
        ctx->drawEllipse (CRect (h[i].x - 3.5, h[i].y - 3.5, h[i].x + 3.5, h[i].y + 3.5), kDrawFilled);
    }
}

void EnvelopeDisplay::draw (CDrawContext* ctx)
{
    const CRect r = getViewSize ();
    background (ctx, r);
    CRect a = r;
    a.inset (8, 8);
    a.top += 10;
    const int mode = (int)std::lround (host->plainValue (kMode));
    const bool dim = which == 0 && mode != kModeClassic;
    ctx->setClipRect (r);
    drawAdsr (ctx, a, host, which, nullptr, dim);
    std::string title = which == 0 ? "Amplitude" : (which == 1 ? "Filter Envelope" : "Pitch Envelope");
    if (which == 0 && std::lround (host->plainValue (kAmpLoopMode)) != 0)
        title += "  (Loop: " + host->valueText (kAmpLoopMode) + ")";
    if (which == 1)
        title += "  " + host->valueText (kFilterEnvAmt);
    if (which == 2)
        title += "  " + host->valueText (kPitchEnvAmt);
    label (ctx, title, CRect (r.left + 6, r.top + 3, r.right - 6, r.top + 16), theme::kTextDim, 10.0);
    if (dim)
        label (ctx, "One-Shot/Slicing use Fade In/Out instead", CRect (r.left, r.bottom - 18, r.right - 8, r.bottom - 4),
               theme::kTextDim, 10.0, kRightText);
    ctx->resetClipRect ();
}

void EnvelopeDisplay::onMouseDownEvent (MouseDownEvent& e)
{
    if (!e.buttonState.isLeft ())
        return;
    const CRect r = getViewSize ();
    CRect a = r;
    a.inset (8, 8);
    a.top += 10;
    // compute handle positions without drawing
    const uint32_t base = idA ();
    const double at = host->plainValue (base), dt = host->plainValue (base + 1), s = host->plainValue (base + 2),
                 rt = host->plainValue (base + 3);
    auto w = [] (double ms) { return std::log10 (1.0 + ms / 2.0) + 0.05; };
    const double scale = a.getWidth () / (w (at) + w (dt) + 1.2 + w (rt));
    const double x1 = a.left + w (at) * scale, x2 = x1 + w (dt) * scale, x4 = x2 + 1.2 * scale + w (rt) * scale;
    const CPoint h[3] = {CPoint (x1, a.top), CPoint (x2, a.bottom - s * a.getHeight ()), CPoint (x4, a.bottom)};
    int best = -1;
    double bestD = 1e9;
    for (int i = 0; i < 3; ++i)
    {
        const double d = std::hypot (h[i].x - e.mousePosition.x, h[i].y - e.mousePosition.y);
        if (d < bestD)
        {
            bestD = d;
            best = i;
        }
    }
    if (bestD > 40)
        return;
    dragHandle = best;
    last = e.mousePosition;
    va = host->norm (base);
    vd = host->norm (base + 1);
    vs = host->norm (base + 2);
    vr = host->norm (base + 3);
    for (uint32_t i = 0; i < 4; ++i)
        host->beginEdit (base + i);
    e.consumed = true;
}

void EnvelopeDisplay::onMouseMoveEvent (MouseMoveEvent& e)
{
    if (dragHandle < 0)
        return;
    const uint32_t base = idA ();
    const double fine = e.modifiers.has (ModifierKey::Shift) ? 0.2 : 1.0;
    const double dx = (e.mousePosition.x - last.x) / 250.0 * fine;
    const double dy = -(e.mousePosition.y - last.y) / std::max (20.0, getViewSize ().getHeight () - 26.0) * fine;
    last = e.mousePosition;
    switch (dragHandle)
    {
        case 0:
            va = std::clamp (va + dx, 0.0, 1.0);
            host->setNorm (base, va);
            break;
        case 1:
            vd = std::clamp (vd + dx, 0.0, 1.0);
            vs = std::clamp (vs + dy, 0.0, 1.0);
            host->setNorm (base + 1, vd);
            host->setNorm (base + 2, vs);
            break;
        default:
            vr = std::clamp (vr + dx, 0.0, 1.0);
            host->setNorm (base + 3, vr);
            break;
    }
    invalid ();
    e.consumed = true;
}

void EnvelopeDisplay::onMouseUpEvent (MouseUpEvent& e)
{
    if (dragHandle < 0)
        return;
    const uint32_t base = idA ();
    for (uint32_t i = 0; i < 4; ++i)
        host->endEdit (base + i);
    dragHandle = -1;
    e.consumed = true;
}

} // namespace simplr
