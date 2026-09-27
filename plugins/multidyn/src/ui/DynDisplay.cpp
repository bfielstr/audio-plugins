#include "DynDisplay.h"

#include "Engine.h"
#include "plugin/Controller.h"

#include "pluginkit/ui/Theme.h"

#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cframe.h"
#include "vstgui/lib/events.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace multidyn {

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

// Orange when the block is made louder, blue when quieter. Above: ratio > 1 is quieter
// (compression); Below: ratio > 1 is louder (upward compression).
CColor blockColor (double ratio, bool below, bool dim)
{
    if (std::fabs (ratio - 1.0) < 1e-3)
        return CColor (255, 255, 255, (uint8_t)(dim ? 8 : 16));
    const double amount = std::clamp (std::fabs (std::log2 (ratio)) / 3.0, 0.15, 1.0);
    const uint8_t alpha = (uint8_t)(dim ? 25 : 40 + 110 * amount);
    const bool louder = below ? ratio > 1.0 : ratio < 1.0;
    return louder ? CColor (255, 150, 40, alpha) : CColor (70, 130, 235, alpha);
}

std::string gainText (double db)
{
    char buf[16];
    std::snprintf (buf, sizeof (buf), "%+.1f", db);
    return buf;
}
} // namespace

DynDisplay::DynDisplay (const CRect& r, pk::ParamHost* h, Controller* c) : CView (r), host (h), controller (c)
{
    for (int b = 0; b < kNumBands; ++b)
        shownIn[b] = shownOut[b] = -100.0f;
}

int DynDisplay::bands () const { return std::clamp ((int)std::lround (host->plainValue (kBands)) + 1, 1, kMaxBands); }

CRect DynDisplay::laneRect (int band) const
{
    const CRect r = getViewSize ();
    const int n = bands ();
    const double laneH = (r.getHeight () - kScaleHeight) / n;
    const int row = n - 1 - band; // highest band on top
    return CRect (r.left, r.top + row * laneH + 1, r.right, r.top + (row + 1) * laneH - 1);
}

double DynDisplay::xOf (double db) const
{
    const CRect r = getViewSize ();
    return r.left + (std::clamp (db, kMinDb, kMaxDb) - kMinDb) / (kMaxDb - kMinDb) * r.getWidth ();
}

void DynDisplay::draw (CDrawContext* ctx)
{
    const CRect all = getViewSize ();
    ctx->setFillColor (theme::kPanel);
    ctx->drawRect (all, kDrawFilled);
    const int n = bands ();
    for (int b = 0; b < n; ++b)
    {
        const CRect lane = laneRect (b);
        ctx->setFillColor (theme::kWaveBg);
        ctx->drawRect (lane, kDrawFilled);
        const bool active = host->plainValue (bandParam (b, kBandActive)) >= 0.5;
        const bool dim = !active;
        const double tb = host->plainValue (bandParam (b, kBelowThresh));
        const double ta = host->plainValue (bandParam (b, kAboveThresh));
        const double rb = host->plainValue (bandParam (b, kBelowRatio));
        const double ra = host->plainValue (bandParam (b, kAboveRatio));
        const double xb = xOf (tb), xa = xOf (ta);

        // grid
        ctx->setLineWidth (1.0);
        ctx->setFrameColor (theme::kGrid);
        for (double db = -70.0; db <= -10.0; db += 10.0)
            ctx->drawLine (CPoint (xOf (db), lane.top), CPoint (xOf (db), lane.bottom));

        // blocks
        ctx->setFillColor (blockColor (rb, true, dim));
        ctx->drawRect (CRect (lane.left, lane.top, xb, lane.bottom), kDrawFilled);
        ctx->setFillColor (blockColor (ra, false, dim));
        ctx->drawRect (CRect (xa, lane.top, lane.right, lane.bottom), kDrawFilled);
        ctx->setLineWidth (2.0);
        ctx->setFrameColor (dim ? theme::kTextDim : theme::kTextBright);
        ctx->drawLine (CPoint (xb, lane.top), CPoint (xb, lane.bottom));
        ctx->drawLine (CPoint (xa, lane.top), CPoint (xa, lane.bottom));
        ctx->setLineWidth (1.0);

        // meters: thick = output, thin = input
        {
            // keep clear of the labels at the top and the ratio text at the bottom of the lane
            const double top = lane.top + 18, bottom = lane.bottom - 18;
            const double mh = std::max (4.0, (bottom - top) * 0.55);
            const double my = (top + bottom) / 2 - mh / 2 + 2;
            ctx->setFillColor (dim ? theme::kKnobTrack : theme::kAccent);
            ctx->drawRect (CRect (lane.left, my, xOf (shownOut[b]), my + mh), kDrawFilled);
            ctx->setFillColor (CColor (240, 240, 240, dim ? 90 : 220));
            ctx->drawRect (CRect (lane.left, my - 4, xOf (shownIn[b]), my - 1.5), kDrawFilled);
        }

        // labels
        const CColor tc = dim ? theme::kTextDim : theme::kText;
        // ratio, and the gain the block applies at its extreme (silence / 0 dB), like the original
        const bool knee = host->plainValue (kSoftKnee) >= 0.5;
        const double amount = host->plainValue (kAmount);
        const double belowMax = std::min (36.0, belowGainDb (-120.0, tb, rb, knee) * amount);
        const double aboveMax = std::max (-80.0, aboveGainDb (0.0, ta, ra, knee) * amount);
        text (ctx, host->valueText (bandParam (b, kBelowRatio)), CRect (lane.left + 4, lane.bottom - 16, xb - 4, lane.bottom - 2), tc, 10.0);
        text (ctx, host->valueText (bandParam (b, kAboveRatio)), CRect (xa + 4, lane.bottom - 16, lane.right - 4, lane.bottom - 2), tc, 10.0);
        if (std::fabs (belowMax) >= 0.05 && xb - lane.left > 120)
            text (ctx, gainText (belowMax), CRect (lane.left + 4, lane.bottom - 16, lane.left + 60, lane.bottom - 2), tc, 10.0, kLeftText, true);
        if (std::fabs (aboveMax) >= 0.05 && lane.right - xa > 120)
            text (ctx, gainText (aboveMax), CRect (lane.right - 60, lane.bottom - 16, lane.right - 4, lane.bottom - 2), tc, 10.0, kRightText, true);
        text (ctx, host->valueText (bandParam (b, kBelowThresh)), CRect (xb - 60, lane.top + 2, xb - 4, lane.top + 14), tc, 9.5, kRightText);
        text (ctx, host->valueText (bandParam (b, kAboveThresh)), CRect (xa + 4, lane.top + 2, xa + 60, lane.top + 14), tc, 9.5, kLeftText);
        std::string tag = "Band " + std::to_string (b + 1);
        if (!active)
            tag += "  (bypassed)";
        else if (std::fabs (shownGain[b]) > 0.05f)
        {
            char buf[32];
            std::snprintf (buf, sizeof (buf), "  %+.1f dB", shownGain[b]);
            tag += buf;
        }
        text (ctx, tag, CRect (lane.left + 4, lane.top + 2, lane.left + 160, lane.top + 14), tc, 9.5, kLeftText, true);
    }

    // dB scale
    const double sy = all.bottom - kScaleHeight;
    for (double db = -80.0; db <= 0.0; db += 10.0)
    {
        char buf[16];
        std::snprintf (buf, sizeof (buf), "%.0f", std::fabs (db)); // Live labels the scale 80 ... 0
        const double cx = std::clamp (xOf (db), all.left + 12.0, all.right - 12.0);
        text (ctx, buf, CRect (cx - 20, sy + 1, cx + 20, all.bottom), theme::kTextDim, 9.5);
    }
}

DynDisplay::Hit DynDisplay::hitTest (const CPoint& p, int& band) const
{
    band = -1;
    for (int b = 0; b < bands (); ++b)
        if (laneRect (b).pointInside (p))
            band = b;
    if (band < 0)
        return Hit::None;
    const double xb = xOf (host->plainValue (bandParam (band, kBelowThresh)));
    const double xa = xOf (host->plainValue (bandParam (band, kAboveThresh)));
    constexpr double grab = 5.0;
    if (std::fabs (p.x - xb) <= grab)
        return Hit::BelowEdge;
    if (std::fabs (p.x - xa) <= grab)
        return Hit::AboveEdge;
    if (p.x < xb)
        return Hit::BelowBlock;
    if (p.x > xa)
        return Hit::AboveBlock;
    return Hit::None;
}

std::vector<uint32_t> DynDisplay::targetsFor (Hit hit, int band, const Modifiers& mods) const
{
    const bool edge = hit == Hit::BelowEdge || hit == Hit::AboveEdge;
    const bool below = hit == Hit::BelowEdge || hit == Hit::BelowBlock;
    const int fieldThis = edge ? (below ? kBelowThresh : kAboveThresh) : (below ? kBelowRatio : kAboveRatio);
    const int fieldOther = edge ? (below ? kAboveThresh : kBelowThresh) : (below ? kAboveRatio : kBelowRatio);
    std::vector<uint32_t> ids;
    const bool allBands = mods.has (ModifierKey::Control);
    const bool both = mods.has (ModifierKey::Alt);
    for (int b = 0; b < bands (); ++b)
    {
        if (b != band && !allBands)
            continue;
        ids.push_back (bandParam (b, fieldThis));
        if (both)
            ids.push_back (bandParam (b, fieldOther));
    }
    return ids;
}

void DynDisplay::onMouseDownEvent (MouseDownEvent& e)
{
    if (!e.buttonState.isLeft ())
        return;
    int band;
    const Hit hit = hitTest (e.mousePosition, band);
    if (hit == Hit::None)
        return;
    const auto ids = targetsFor (hit, band, e.modifiers);
    if (e.clickCount == 2 && (hit == Hit::BelowBlock || hit == Hit::AboveBlock))
    {
        for (auto id : ids)
            host->setOnce (id, host->table ().toNormalized (id, 1.0)); // 1:1 = no processing
        invalid ();
        e.consumed = true;
        e.ignoreFollowUpMoveAndUpEvents (true);
        return;
    }
    dragHit = hit;
    downPoint = e.mousePosition;
    targets.clear ();
    for (auto id : ids)
    {
        targets.push_back ({id, host->table ().info (id).curve == pk::Curve::Ratio ? host->norm (id) : host->plainValue (id)});
        host->beginEdit (id);
    }
    e.consumed = true;
}

void DynDisplay::onMouseMoveEvent (MouseMoveEvent& e)
{
    if (dragHit == Hit::None)
    {
        int band;
        const Hit h = hitTest (e.mousePosition, band);
        if (auto* f = getFrame ())
            f->setCursor (h == Hit::BelowEdge || h == Hit::AboveEdge ? kCursorHSize
                          : h == Hit::None                          ? kCursorDefault
                                                                    : kCursorVSize);
        return;
    }
    const double fine = e.modifiers.has (ModifierKey::Shift) ? 0.2 : 1.0;
    const bool edge = dragHit == Hit::BelowEdge || dragHit == Hit::AboveEdge;
    for (const auto& t : targets)
    {
        if (edge)
        {
            // thresholds: follow the mouse horizontally in dB
            const double dbPerPx = (kMaxDb - kMinDb) / getViewSize ().getWidth ();
            const double v = t.start + (e.mousePosition.x - downPoint.x) * dbPerPx * fine;
            host->setNorm (t.id, host->table ().toNormalized (t.id, v));
        }
        else
        {
            // "volume" of a block: dragging up makes it louder. Above: louder = lower ratio;
            // Below: louder = higher ratio (upward compression).
            const bool belowRatio = (t.id - kBandBase) % kBandBlock == kBelowRatio;
            const double dir = belowRatio ? -1.0 : 1.0;
            const double v = t.start + dir * (e.mousePosition.y - downPoint.y) / 200.0 * fine;
            host->setNorm (t.id, std::clamp (v, 0.0, 1.0));
        }
    }
    invalid ();
    e.consumed = true;
}

void DynDisplay::onMouseUpEvent (MouseUpEvent& e)
{
    if (dragHit == Hit::None)
        return;
    for (const auto& t : targets)
        host->endEdit (t.id);
    targets.clear ();
    dragHit = Hit::None;
    e.consumed = true;
}

void DynDisplay::onMouseExitEvent (MouseExitEvent& e)
{
    if (auto* f = getFrame ())
        f->setCursor (kCursorDefault);
    e.consumed = true;
}

void DynDisplay::idle ()
{
    Meters* m = controller->getMeters ();
    bool changed = false;
    for (int b = 0; b < kNumBands; ++b)
    {
        const float in = m ? m->inputDb[(size_t)b].load (std::memory_order_relaxed) : -100.0f;
        const float out = m ? m->outputDb[(size_t)b].load (std::memory_order_relaxed) : -100.0f;
        const float g = m ? m->gainDb[(size_t)b].load (std::memory_order_relaxed) : 0.0f;
        auto ease = [&] (float& shown, float target) {
            const float next = target > shown ? target : shown + (target - shown) * 0.25f;
            if (std::fabs (next - shown) > 0.05f)
                changed = true;
            shown = next;
        };
        ease (shownIn[b], in);
        ease (shownOut[b], out);
        ease (shownGain[b], g);
    }
    if (changed)
        invalid ();
}

} // namespace multidyn
