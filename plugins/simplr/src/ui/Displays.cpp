#include "Displays.h"

#include "Envelope.h"
#include "Filter.h"
#include "Params.h"
#include "UiKit.h"

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

uint32_t EnvelopeDisplay::idA () const { return envAdsrBase (which); }

EnvelopeDisplay::Geometry EnvelopeDisplay::geometry (ParamHost* host, int which, const CRect& a)
{
    Geometry g;
    const uint32_t b = envAdsrBase (which);
    const double at = host->plainValue (b), dt = host->plainValue (b + 1), sus = host->plainValue (b + 2),
                 rt = host->plainValue (b + 3);
    const int n = std::clamp ((int)std::lround (host->plainValue (envParam (which, kEnvPointCount))), 0, kMaxEnvPoints);
    auto w = [] (double ms) { return std::log10 (1.0 + ms / 2.0) + 0.05; };

    struct Seg
    {
        double ms, level;
        int kind, index;
        uint32_t curveId;
    };
    Seg segs[kMaxEnvPoints + 4];
    int count = 0;
    segs[count++] = {at, 1.0, kAttack, -1, envParam (which, kEnvCurveA)};
    for (int i = 0; i < n; ++i)
        segs[count++] = {host->plainValue (envPointParam (which, i, kPtTime)),
                         host->plainValue (envPointParam (which, i, kPtLevel)), kBreak, i,
                         envPointParam (which, i, kPtCurve)};
    segs[count++] = {dt, sus, kDecay, -1, envParam (which, kEnvCurveD)};
    segs[count++] = {-1.0, sus, kHold, -1, 0}; // sustain hold: fixed width
    segs[count++] = {rt, 0.0, kRelease, -1, envParam (which, kEnvCurveR)};

    double units = 0.0;
    for (int i = 0; i < count; ++i)
        units += segs[i].ms < 0.0 ? 1.2 : w (segs[i].ms);
    const double scale = a.getWidth () / units;
    auto y = [&] (double v) { return a.bottom - std::clamp (v, 0.0, 1.0) * a.getHeight (); };

    g.pts[0] = {CPoint (a.left, y (0.0)), 0.0, kStart, -1, 0};
    double x = a.left, level = 0.0;
    for (int i = 0; i < count; ++i)
    {
        x += (segs[i].ms < 0.0 ? 1.2 : w (segs[i].ms)) * scale;
        g.pts[i + 1] = {CPoint (x, y (segs[i].level)), segs[i].level, segs[i].kind, segs[i].index, segs[i].curveId};
        level = segs[i].level;
    }
    (void)level;
    g.count = count + 1;
    g.area = a;
    g.points = n;
    return g;
}

double EnvelopeDisplay::Geometry::curveOf (ParamHost* host, int seg) const
{
    return pts[seg].kind == kHold ? 0.0 : host->plainValue (pts[seg].curveId);
}

void EnvelopeDisplay::drawAdsr (CDrawContext* ctx, const CRect& a, ParamHost* host, int which, CPoint* handles,
                                bool dim)
{
    const Geometry g = geometry (host, which, a);
    ctx->setLineWidth (1.0);
    ctx->setFrameColor (theme::kGrid);
    for (double v : {0.25, 0.5, 0.75})
    {
        const double yy = a.bottom - v * a.getHeight ();
        ctx->drawLine (CPoint (a.left, yy), CPoint (a.right, yy));
    }
    auto path = owned (ctx->createGraphicsPath ());
    if (!path)
        return;
    path->beginSubpath (g.pts[0].p);
    for (int s = 1; s < g.count; ++s)
    {
        const CPoint p0 = g.pts[s - 1].p, p1 = g.pts[s].p;
        const float c = (float)g.curveOf (host, s);
        for (int i = 1; i <= 32; ++i)
        {
            const float u = i / 32.0f;
            path->addLine (CPoint (p0.x + (p1.x - p0.x) * u, p0.y + (p1.y - p0.y) * envCurve (u, c)));
        }
    }
    ctx->setLineWidth (1.5);
    ctx->setFrameColor (dim ? theme::kTextDim : theme::kCurve);
    ctx->drawGraphicsPath (path, CDrawContext::kPathStroked);

    // sustain section markers
    ctx->setFrameColor (CColor (255, 255, 255, 40));
    for (int s = 1; s < g.count; ++s)
        if (g.pts[s].kind == kDecay || g.pts[s].kind == kHold)
            ctx->drawLine (CPoint (g.pts[s].p.x, a.top), CPoint (g.pts[s].p.x, a.bottom));

    int h = 0;
    for (int s = 1; s < g.count; ++s)
    {
        const int kind = g.pts[s].kind;
        if (kind == kHold)
            continue;
        const CPoint p = g.pts[s].p;
        const double r = kind == kBreak ? 3.0 : 3.5;
        ctx->setFillColor (dim ? theme::kTextDim : (kind == kBreak ? theme::kAccent : theme::kTextBright));
        if (kind == kBreak)
            ctx->drawRect (CRect (p.x - r, p.y - r, p.x + r, p.y + r), kDrawFilled);
        else
            ctx->drawEllipse (CRect (p.x - r, p.y - r, p.x + r, p.y + r), kDrawFilled);
        if (handles && h < 3 && kind != kBreak)
            handles[h++] = p;
    }
}

void EnvelopeDisplay::draw (CDrawContext* ctx)
{
    const CRect r = getViewSize ();
    background (ctx, r);
    const CRect a = plotArea ();
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
    if (!status.empty ())
        label (ctx, status, CRect (r.left + 6, r.top + 3, r.right - 8, r.top + 16), theme::kAccent, 10.0, kRightText);
    if (dim)
        label (ctx, "One-Shot/Slicing use Fade In/Out instead", CRect (r.left, r.bottom - 18, r.right - 8, r.bottom - 4),
               theme::kTextDim, 10.0, kRightText);
    ctx->resetClipRect ();
}

CRect EnvelopeDisplay::plotArea () const
{
    CRect a = getViewSize ();
    a.inset (8, 8);
    a.top += 10;
    return a;
}

int EnvelopeDisplay::segmentAt (const Geometry& g, double x) const
{
    for (int s = 1; s < g.count; ++s)
        if (x >= g.pts[s - 1].p.x && x <= g.pts[s].p.x)
            return s;
    return x < g.pts[0].p.x ? 1 : g.count - 1;
}

void EnvelopeDisplay::insertPoint (const Geometry& g, int seg, double x, double y)
{
    const int n = g.points;
    const int kind = g.pts[seg].kind;
    if (kind != kBreak && kind != kDecay)
    {
        flash ("Double-click between the peak and the sustain point to add a breakpoint");
        return;
    }
    if (n >= kMaxEnvPoints)
    {
        flash ("Maximum of 6 breakpoints");
        return;
    }
    const CPoint p0 = g.pts[seg - 1].p, p1 = g.pts[seg].p;
    const double u = std::clamp ((x - p0.x) / std::max (1.0, p1.x - p0.x), 0.02, 0.98);
    const CRect a = g.area;
    const double level = std::clamp ((a.bottom - y) / a.getHeight (), 0.0, 1.0);
    // index of the new breakpoint = number of breakpoints before this segment's end
    const int k = kind == kDecay ? n : g.pts[seg].index;
    const uint32_t segTimeId = kind == kDecay ? envAdsrBase (which) + 1 : envPointParam (which, k, kPtTime);
    const double segMs = host->plainValue (segTimeId);
    const double segCurve = g.curveOf (host, seg);
    // shift breakpoints k..n-1 up by one (from the end)
    for (int i = n - 1; i >= k; --i)
        for (int f = 0; f < 3; ++f)
            host->setOnce (envPointParam (which, i + 1, f), host->norm (envPointParam (which, i, f)));
    auto setPlain = [this] (uint32_t id, double v) { host->setOnce (id, toNormalized (id, v)); };
    setPlain (envPointParam (which, k, kPtTime), std::max (0.1, u * segMs));
    setPlain (envPointParam (which, k, kPtLevel), level);
    setPlain (envPointParam (which, k, kPtCurve), segCurve);
    // the remainder of the split segment keeps its end point
    setPlain (kind == kDecay ? segTimeId : envPointParam (which, k + 1, kPtTime), std::max (0.1, (1.0 - u) * segMs));
    setPlain (envParam (which, kEnvPointCount), n + 1);
}

void EnvelopeDisplay::removePoint (const Geometry& g, int index)
{
    const int n = g.points;
    if (index < 0 || index >= n)
        return;
    auto setPlain = [this] (uint32_t id, double v) { host->setOnce (id, toNormalized (id, v)); };
    // the removed segment's time is added to the following one so later points don't move
    const double removedMs = host->plainValue (envPointParam (which, index, kPtTime));
    const uint32_t nextTimeId = index + 1 < n ? envPointParam (which, index + 1, kPtTime) : envAdsrBase (which) + 1;
    setPlain (nextTimeId, std::min (20000.0, host->plainValue (nextTimeId) + removedMs));
    for (int i = index; i + 1 < n; ++i)
        for (int f = 0; f < 3; ++f)
            host->setOnce (envPointParam (which, i, f), host->norm (envPointParam (which, i + 1, f)));
    setPlain (envParam (which, kEnvPointCount), n - 1);
}

void EnvelopeDisplay::flash (const std::string& msg)
{
    status = msg;
    statusTicks = 90;
    invalid ();
}

void EnvelopeDisplay::tick ()
{
    if (statusTicks > 0 && --statusTicks == 0)
    {
        status.clear ();
        invalid ();
    }
}

void EnvelopeDisplay::onMouseDownEvent (MouseDownEvent& e)
{
    if (!e.buttonState.isLeft ())
        return;
    const Geometry g = geometry (host, which, plotArea ());
    const CPoint m = e.mousePosition;

    // nearest handle (not the start point or the hold end)
    int best = -1;
    double bestD = 1e9;
    for (int s = 1; s < g.count; ++s)
    {
        if (g.pts[s].kind == kHold)
            continue;
        const double d = std::hypot (g.pts[s].p.x - m.x, g.pts[s].p.y - m.y);
        if (d < bestD)
        {
            bestD = d;
            best = s;
        }
    }
    const bool onHandle = best > 0 && bestD <= 9.0;

    if (e.clickCount == 2)
    {
        if (onHandle && g.pts[best].kind == kBreak)
            removePoint (g, g.pts[best].index);
        else if (!onHandle)
            insertPoint (g, segmentAt (g, m.x), m.x, m.y);
        invalid ();
        e.consumed = true;
        e.ignoreFollowUpMoveAndUpEvents (true);
        return;
    }

    last = m;
    dragIds.clear ();
    dragValues.clear ();
    if (e.modifiers.has (ModifierKey::Shift) && !onHandle)
    {
        // bend the curve of the segment under the mouse
        const int seg = segmentAt (g, m.x);
        if (g.pts[seg].kind == kHold)
            return;
        dragMode = DragCurve;
        curveRising = g.pts[seg].p.y < g.pts[seg - 1].p.y; // screen y grows downwards
        dragIds = {g.pts[seg].curveId};
    }
    else if (onHandle)
    {
        dragMode = DragHandle;
        const auto& pt = g.pts[best];
        const uint32_t b = envAdsrBase (which);
        switch (pt.kind)
        {
            case kAttack: dragIds = {b}; break;
            case kBreak: dragIds = {envPointParam (which, pt.index, kPtTime), envPointParam (which, pt.index, kPtLevel)}; break;
            case kDecay: dragIds = {b + 1, b + 2}; break;
            default: dragIds = {b + 3}; break;
        }
    }
    else
        return;
    for (auto id : dragIds)
    {
        dragValues.push_back (host->norm (id));
        host->beginEdit (id);
    }
    e.consumed = true;
}

void EnvelopeDisplay::onMouseMoveEvent (MouseMoveEvent& e)
{
    if (dragIds.empty ())
        return;
    const double fine = e.modifiers.has (ModifierKey::Control) ? 0.2 : 1.0;
    const double dx = (e.mousePosition.x - last.x) / 250.0 * fine;
    const double dy = -(e.mousePosition.y - last.y) / std::max (20.0, plotArea ().getHeight ()) * fine;
    last = e.mousePosition;
    if (dragMode == DragCurve)
    {
        // Dragging up bows the segment upwards. Curve params span -1..1 (normalized 0..1).
        const double delta = (curveRising ? -dy : dy) * 0.9;
        dragValues[0] = std::clamp (dragValues[0] + delta, 0.0, 1.0);
        host->setNorm (dragIds[0], dragValues[0]);
    }
    else
    {
        dragValues[0] = std::clamp (dragValues[0] + dx, 0.0, 1.0);
        host->setNorm (dragIds[0], dragValues[0]);
        if (dragIds.size () > 1)
        {
            dragValues[1] = std::clamp (dragValues[1] + dy, 0.0, 1.0);
            host->setNorm (dragIds[1], dragValues[1]);
        }
    }
    invalid ();
    e.consumed = true;
}

void EnvelopeDisplay::onMouseUpEvent (MouseUpEvent& e)
{
    if (dragIds.empty ())
        return;
    for (auto id : dragIds)
        host->endEdit (id);
    dragIds.clear ();
    e.consumed = true;
}

} // namespace simplr
