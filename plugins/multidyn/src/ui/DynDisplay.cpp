#include "DynDisplay.h"

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
const char* kBandNames[] = {"Low", "Mid", "High"};

void text (CDrawContext* ctx, const std::string& s, const CRect& r, const CColor& c, double size,
           CHoriTxtAlign a = kCenterText, bool bold = false)
{
    ctx->setFont (theme::font (size, bold));
    ctx->setFontColor (c);
    ctx->drawString (s.c_str (), r, a, true);
}

// Blue when the block is made quieter (ratio > 1), orange when louder (ratio < 1).
CColor blockColor (double ratio, bool dim)
{
    const uint8_t a = dim ? 18 : 0;
    if (std::fabs (ratio - 1.0) < 1e-3)
        return CColor (255, 255, 255, (uint8_t)(dim ? 8 : 16));
    const double amount = std::clamp (std::fabs (std::log2 (ratio)) / 3.0, 0.15, 1.0);
    const uint8_t alpha = (uint8_t)(dim ? 25 : 40 + 110 * amount);
    (void)a;
    return ratio > 1.0 ? CColor (70, 130, 235, alpha) : CColor (255, 150, 40, alpha);
}
} // namespace

DynDisplay::DynDisplay (const CRect& r, pk::ParamHost* h, Controller* c) : CView (r), host (h), controller (c)
{
    for (int b = 0; b < kNumBands; ++b)
        shownIn[b] = shownOut[b] = -100.0f;
}

CRect DynDisplay::laneRect (int band) const
{
    const CRect r = getViewSize ();
    const double laneH = (r.getHeight () - kScaleHeight) / kNumBands;
    const int row = kNumBands - 1 - band; // High on top
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
    const bool lowOn = host->plainValue (kLowOn) >= 0.5, highOn = host->plainValue (kHighOn) >= 0.5;

    for (int b = 0; b < kNumBands; ++b)
    {
        const CRect lane = laneRect (b);
        ctx->setFillColor (theme::kWaveBg);
        ctx->drawRect (lane, kDrawFilled);
        const bool used = b == kMid || (b == kLow ? lowOn : highOn);
        const bool active = host->plainValue (bandParam (b, kBandActive)) >= 0.5;
        const bool dim = !used || !active;
        const double tb = host->plainValue (bandParam (b, kBelowThresh));
        const double ta = host->plainValue (bandParam (b, kAboveThresh));
        const double rb = host->plainValue (bandParam (b, kBelowRatio));
        const double ra = host->plainValue (bandParam (b, kAboveRatio));
        const double xb = xOf (tb), xa = xOf (ta);

        // grid
        ctx->setLineWidth (1.0);
        ctx->setFrameColor (theme::kGrid);
        for (double db = -60.0; db <= 0.0; db += 12.0)
            ctx->drawLine (CPoint (xOf (db), lane.top), CPoint (xOf (db), lane.bottom));

        // blocks
        ctx->setFillColor (blockColor (rb, dim));
        ctx->drawRect (CRect (lane.left, lane.top, xb, lane.bottom), kDrawFilled);
        ctx->setFillColor (blockColor (ra, dim));
        ctx->drawRect (CRect (xa, lane.top, lane.right, lane.bottom), kDrawFilled);
        ctx->setLineWidth (2.0);
        ctx->setFrameColor (dim ? theme::kTextDim : theme::kTextBright);
        ctx->drawLine (CPoint (xb, lane.top), CPoint (xb, lane.bottom));
        ctx->drawLine (CPoint (xa, lane.top), CPoint (xa, lane.bottom));
        ctx->setLineWidth (1.0);

        // meters: thick = output, thin = input
        if (used)
        {
            const double mh = lane.getHeight () * 0.34;
            const double my = lane.getCenter ().y - mh / 2;
            ctx->setFillColor (dim ? theme::kKnobTrack : theme::kAccent);
            ctx->drawRect (CRect (lane.left, my, xOf (shownOut[b]), my + mh), kDrawFilled);
            ctx->setFillColor (CColor (240, 240, 240, dim ? 90 : 220));
            ctx->drawRect (CRect (lane.left, my - 5, xOf (shownIn[b]), my - 2), kDrawFilled);
        }

        // labels
        const CColor tc = dim ? theme::kTextDim : theme::kText;
        text (ctx, host->valueText (bandParam (b, kBelowRatio)), CRect (lane.left + 4, lane.bottom - 16, xb - 4, lane.bottom - 2), tc, 10.0);
        text (ctx, host->valueText (bandParam (b, kAboveRatio)), CRect (xa + 4, lane.bottom - 16, lane.right - 4, lane.bottom - 2), tc, 10.0);
        text (ctx, host->valueText (bandParam (b, kBelowThresh)), CRect (xb - 60, lane.top + 2, xb - 4, lane.top + 14), tc, 9.5, kRightText);
        text (ctx, host->valueText (bandParam (b, kAboveThresh)), CRect (xa + 4, lane.top + 2, xa + 60, lane.top + 14), tc, 9.5, kLeftText);
        std::string tag = kBandNames[b];
        if (!used)
            tag += "  (off)";
        else if (!active)
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
    for (double db = -60.0; db <= 0.0; db += 12.0)
    {
        char buf[16];
        std::snprintf (buf, sizeof (buf), "%.0f", db);
        text (ctx, buf, CRect (xOf (db) - 20, sy + 1, xOf (db) + 20, all.bottom), theme::kTextDim, 9.5);
    }
}

DynDisplay::Hit DynDisplay::hitTest (const CPoint& p, int& band) const
{
    band = -1;
    for (int b = 0; b < kNumBands; ++b)
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
    for (int b = 0; b < kNumBands; ++b)
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
            host->setOnce (id, host->table ().defaultNormalized (id));
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
            // "volume" of a block: dragging up makes it louder = lower ratio
            const double v = t.start + (e.mousePosition.y - downPoint.y) / 200.0 * fine;
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
