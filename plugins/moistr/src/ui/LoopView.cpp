#include "LoopView.h"

#include "GestureView.h"
#include "LoopWindow.h"

#include "pluginkit/ui/Theme.h"

#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cgraphicspath.h"
#include "vstgui/lib/events.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

namespace moistr {

using namespace VSTGUI;
namespace theme = pk::theme;

namespace {
constexpr double kTitle = 16.0, kPad = 6.0, kFoot = 13.0, kGrab = 5.0;
constexpr int kSteps = 240; // (points across the window per curve)

void text (CDrawContext* ctx, const std::string& s, const CRect& r, const CColor& c, double size, CHoriTxtAlign a = kLeftText, bool bold = false)
{
    ctx->setFont (theme::font (size, bold));
    ctx->setFontColor (c);
    ctx->drawString (s.c_str (), r, a, true);
}
} // namespace

LoopView::LoopView (const CRect& r, pk::ParamHost* h, MeterSource m, UserSource u) : CView (r), host (h), meters (std::move (m)), user (std::move (u)) {}

CRect LoopView::plot () const
{
    const CRect all = getViewSize ();
    return CRect (all.left + kPad, all.top + kTitle, all.right - kPad, all.bottom - kFoot - 2.0);
}

double LoopView::tempo () const
{
    const Meters* m = meters ? meters () : nullptr;
    return m && m->blocks.load (std::memory_order_acquire) > 0 ? std::max (1.0f, m->tempo.load (std::memory_order_relaxed)) : 120.0;
}

double LoopView::windowNow () const
{
    const Meters* m = meters ? meters () : nullptr;
    if (m && m->blocks.load (std::memory_order_acquire) > 0)
        return std::max (1e-3f, m->windowBeats.load (std::memory_order_relaxed));
    std::vector<double> p (kNumParams);
    for (uint32_t id = 0; id < kNumParams; ++id)
        p[id] = host->plainValue (id);
    const Pattern a = makePattern (std::clamp ((int)std::lround (p[kSeed]), kMinSeed, kMaxSeed), 0);
    const MotionDrift d = driftOf (p.data ());
    return windowBeats (p.data (), tempo (), GestureView::sceneOf (host, user ? user () : nullptr), &a, nullptr, &d);
}

void LoopView::idle ()
{
    const Meters* m = meters ? meters () : nullptr;
    double pos = -1.0, window = shownWindow;
    if (m && m->blocks.load (std::memory_order_acquire) > 0)
    {
        window = m->windowBeats.load (std::memory_order_relaxed);
        if (m->loopOn.load (std::memory_order_relaxed) && window > 0.0)
            pos = std::round (m->motionBeats.load (std::memory_order_relaxed) / window * 500.0) / 500.0;
    }
    if (pos != shownPos || std::fabs (window - shownWindow) > 1e-4)
        invalid ();
}

void LoopView::paintCurves (CDrawContext* ctx, const CRect& p, double window)
{
    ctx->setFillColor (theme::kWell);
    ctx->drawRect (getViewSize (), kDrawFilled);
    std::vector<double> plain (kNumParams);
    for (uint32_t id = 0; id < kNumParams; ++id)
        plain[id] = host->plainValue (id);
    const double bpm = tempo ();
    const Scene* scene = GestureView::sceneOf (host, user ? user () : nullptr);
    const Pattern a = makePattern (std::clamp ((int)std::lround (plain[kSeed]), kMinSeed, kMaxSeed), 0);
    const MotionDrift drift = driftOf (plain.data ());
    const CRect all = getViewSize ();
    // a beat grid
    ctx->setLineWidth (1.0);
    const double step = window <= 8.0 ? 1.0 : window <= 32.0 ? 4.0 : 16.0;
    for (double b = 0.0; b <= window + 1e-9; b += step)
    {
        ctx->setFrameColor (theme::kGridMinor);
        const double x = std::round (p.left + p.getWidth () * b / window) + 0.5;
        ctx->drawLine (CPoint (x, p.top), CPoint (x, p.bottom));
    }
    // every curve across the window (worked out again only when what they are made of changed)
    std::vector<std::vector<double>>& values = curveValues;
    if (curveKey != layerKey)
    {
        curveKey = layerKey;
        values.clear ();
    for (int i = 0; i <= kSteps; ++i)
    {
        const auto c = motionCurves (plain.data (), bpm, scene, &a, window * i / kSteps, &drift);
        if (i == 0)
            values.assign (c.size (), std::vector<double> (kSteps + 1, 0.0));
        for (size_t j = 0; j < c.size () && j < values.size (); ++j)
            values[j][(size_t)i] = c[j].value;
    }
    }
    for (size_t j = 0; j < values.size (); ++j)
        if (auto path = owned (ctx->createGraphicsPath ()))
        {
            for (int i = 0; i <= kSteps; ++i)
            {
                const CPoint pt (p.left + p.getWidth () * i / kSteps, p.bottom - 1.5 - (p.getHeight () - 3.0) * std::clamp (values[j][(size_t)i], 0.0, 1.0));
                if (i == 0)
                    path->beginSubpath (pt);
                else
                    path->addLine (pt);
            }
            ctx->setLineWidth (1.0);
            ctx->setFrameColor (theme::withAlpha (theme::kCopper, (uint8_t)std::clamp (230 - 12 * (int)j, 90, 230)));
            ctx->drawGraphicsPath (path, CDrawContext::kPathStroked);
        }
}

void LoopView::draw (CDrawContext* ctx)
{
    const CRect all = getViewSize (), p = plot ();
    const double window = windowNow ();
    shownWindow = window;
    pk::LayerKey key;
    key.add (window, tempo (), all.getWidth (), all.getHeight ());
    for (uint32_t id = 0; id < kChainBase; ++id) // (what the curves are made of; the LAB moves nothing by itself)
        key.add (host->plainValue (id));
    for (uint32_t id = kInput; id < kNumParams; ++id)
        key.add (host->plainValue (id));
    key.add ((uint64_t)(uintptr_t)(user ? user () : nullptr));
    layerKey = key; // (drawn directly: the curves' points are kept until the key changes)
    paintCurves (ctx, p, window);
    {
        // the title (the window and what sets it) and what moves, drawn over the layer (text stays out of the bitmap)
        std::vector<double> plain (kNumParams);
        for (uint32_t id = 0; id < kNumParams; ++id)
            plain[id] = host->plainValue (id);
        const double bpm = tempo ();
        const Scene* scene = GestureView::sceneOf (host, user ? user () : nullptr);
        const Pattern a = makePattern (std::clamp ((int)std::lround (plain[kSeed]), kMinSeed, kMaxSeed), 0);
        const MotionDrift drift = driftOf (plain.data ());
        std::string slowest = "nothing moves: a bar", names;
        double longest = 0.0;
        for (const auto& w : windowParts (plain.data (), bpm, scene, &a, nullptr, &drift))
        {
            if (w.periodBeats > longest)
            {
                longest = w.periodBeats;
                slowest = w.name;
            }
        }
        for (const auto& c : motionCurves (plain.data (), bpm, scene, &a, 0.0, &drift))
            names += (names.empty () ? "" : ", ") + c.name;
        char title[160];
        std::snprintf (title, sizeof (title), "Window %.2f beats (%.2f s): %s", window, window * 60.0 / bpm, slowest.c_str ());
        text (ctx, title, CRect (all.left + kPad, all.top + 2.0, all.right - kPad, all.top + kTitle), theme::kCopperPale, 10.0, kLeftText, true);
        text (ctx, names.empty () ? std::string ("nothing moves") : names, CRect (all.left + kPad, p.bottom + 2.0, all.right - kPad - 190.0, p.bottom + 2.0 + kFoot),
              theme::kTextDim, 9.0);
    }

    ctx->setClipRect (all);
    // the region, its handles, and the playhead
    const bool on = host->plainValue (kLoopLock) >= 0.5;
    const double s = std::clamp (host->plainValue (kLoopStart), 0.0, 1.0), e = std::clamp (host->plainValue (kLoopEnd), 0.0, 1.0);
    const double xs = p.left + p.getWidth () * s, xe = p.left + p.getWidth () * std::max (e, s);
    ctx->setFillColor (theme::withAlpha (theme::kEnergyLive, on ? 46 : 22));
    ctx->drawRect (CRect (xs, p.top, xe, p.bottom), kDrawFilled);
    ctx->setFillColor (theme::withAlpha (theme::kEnergyLive, on ? 220 : 110));
    for (double x : {xs, xe})
    {
        ctx->drawRect (CRect (x - 1.0, p.top, x + 1.0, p.bottom), kDrawFilled);
        ctx->drawRect (CRect (x - 4.0, p.top, x + 4.0, p.top + 6.0), kDrawFilled);
    }
    shownPos = -1.0;
    const Meters* m = meters ? meters () : nullptr;
    if (on && m && m->blocks.load (std::memory_order_acquire) > 0 && m->loopOn.load (std::memory_order_relaxed) && window > 0.0)
    {
        shownPos = std::round (m->motionBeats.load (std::memory_order_relaxed) / window * 500.0) / 500.0;
        const double x = std::round (p.left + p.getWidth () * std::clamp (shownPos, 0.0, 1.0)) + 0.5;
        ctx->setFrameColor (theme::withAlpha (theme::kPlayhead, 220));
        ctx->setLineWidth (1.0);
        ctx->drawLine (CPoint (x, p.top), CPoint (x, p.bottom));
    }
    char foot[96];
    std::snprintf (foot, sizeof (foot), "%s %.0f .. %.0f %%, %s", on ? "Loop" : "Loop (off)", s * 100.0, e * 100.0, host->valueText (kLoopLength).c_str ());
    text (ctx, foot, CRect (all.right - kPad - 186.0, p.bottom + 2.0, all.right - kPad, p.bottom + 2.0 + kFoot), on ? theme::kCopperPale : theme::kTextDim,
          9.0, kRightText);
}

void LoopView::onMouseDownEvent (MouseDownEvent& e)
{
    if (!e.buttonState.isLeft ())
        return;
    const CRect p = plot ();
    if (!p.pointInside (e.mousePosition))
        return;
    const double s = host->plainValue (kLoopStart), en = host->plainValue (kLoopEnd);
    if (e.clickCount == 2)
    {
        // the whole window again
        host->setOnce (kLoopStart, 0.0);
        host->setOnce (kLoopEnd, 1.0);
        invalid ();
        e.consumed = true;
        return;
    }
    const double x = e.mousePosition.x, xs = p.left + p.getWidth () * s, xe = p.left + p.getWidth () * en;
    drag = std::fabs (x - xs) <= kGrab ? Drag::Start : std::fabs (x - xe) <= kGrab ? Drag::End : x > xs && x < xe ? Drag::Region : Drag::None;
    if (drag == Drag::None)
        return;
    grabAt = (x - p.left) / p.getWidth ();
    grabStart = s;
    grabEnd = en;
    host->beginEdit (kLoopStart);
    host->beginEdit (kLoopEnd);
    e.consumed = true;
}

void LoopView::onMouseMoveEvent (MouseMoveEvent& e)
{
    if (drag == Drag::None)
        return;
    const CRect p = plot ();
    const double at = (e.mousePosition.x - p.left) / p.getWidth ();
    LoopRegion r {grabStart, grabEnd};
    if (drag == Drag::Region)
        r = slideRegion (r, at - grabAt);
    else
        r = moveHandle (r, drag == Drag::End, at);
    host->setNorm (kLoopStart, r.start);
    host->setNorm (kLoopEnd, r.end);
    invalid ();
    e.consumed = true;
}

void LoopView::finish ()
{
    if (drag == Drag::None)
        return;
    host->endEdit (kLoopStart);
    host->endEdit (kLoopEnd);
    drag = Drag::None;
}

void LoopView::onMouseUpEvent (MouseUpEvent& e)
{
    if (drag != Drag::None)
        e.consumed = true;
    finish ();
}

void LoopView::onMouseCancelEvent (MouseCancelEvent& e)
{
    finish ();
    e.consumed = true;
}

} // namespace moistr
