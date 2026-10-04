#include "DynDisplay.h"

#include "../core/Engine.h"

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

// A block made louder is shaded cinnabar (it adds energy), one made quieter copper; deeper the stronger
// the ratio. Above: ratio > 1 is quieter (compression); Below: ratio > 1 is louder (upward compression).
// At 1:1 (no processing) a barely-there copper shade.
CColor blockColor (double ratio, bool below, bool dim)
{
    if (std::fabs (ratio - 1.0) < 1e-3)
        return theme::withAlpha (theme::kCopper, (uint8_t)(dim ? 8 : 14));
    const double amount = std::clamp (std::fabs (std::log2 (ratio)) / 3.0, 0.15, 1.0);
    const uint8_t alpha = (uint8_t)(dim ? 22 : 30 + 90 * amount);
    const bool louder = below ? ratio > 1.0 : ratio < 1.0;
    return theme::withAlpha (louder ? theme::kEnergyLive : theme::kCopper, alpha);
}

std::string gainText (double db)
{
    char buf[16];
    std::snprintf (buf, sizeof (buf), "%+.1f", db);
    return buf;
}
} // namespace

DynDisplay::DynDisplay (const CRect& r, pk::ParamHost* h, MeterSource m) : CView (r), host (h), meters (std::move (m))
{
    for (int b = 0; b <= kNumBands; ++b)
        shownIn[b] = shownOut[b] = -100.0f;
}

bool DynDisplay::subShown () const { return host->plainValue (kSubOn) >= 0.5; }

namespace {
// a band's (or the Sub band's) parameters as the display uses them; the Sub band has no Below block
struct LaneIds
{
    bool sub;
    uint32_t active, belowT, belowR, aboveT, aboveR;
};
LaneIds laneIds (int band)
{
    if (band == kSubBand)
        return {true, kSubOn, 0, 0, kSubThresh, kSubRatio};
    return {false, bandParam (band, kBandActive), bandParam (band, kBelowThresh), bandParam (band, kBelowRatio),
            bandParam (band, kAboveThresh), bandParam (band, kAboveRatio)};
}
} // namespace

int DynDisplay::bands () const { return std::clamp ((int)std::lround (host->plainValue (kBands)) + 1, 1, kMaxBands); }

CRect DynDisplay::laneRect (int band) const
{
    const CRect r = getViewSize ();
    const int n = bands (), rows = lanes ();
    const double top = r.top + kHeader;
    const double laneH = (r.getHeight () - kHeader - kScaleHeight) / rows;
    const int row = band == kSubBand ? n : n - 1 - band; // highest band on top, the Sub band at the bottom
    return CRect (r.left, top + row * laneH + 1, r.right, top + (row + 1) * laneH - 1);
}

CRect DynDisplay::graphRect (int band) const
{
    CRect g = laneRect (band);
    g.left += kLeftCol;
    g.right -= kRightCol;
    return g;
}

double DynDisplay::xOf (double db) const
{
    const CRect r = getViewSize ();
    const double left = r.left + kLeftCol, width = r.getWidth () - kLeftCol - kRightCol;
    return left + (std::clamp (db, kMinDb, kMaxDb) - kMinDb) / (kMaxDb - kMinDb) * width;
}

void DynDisplay::meterRows (const CRect& g, double& my, double& mh)
{
    const double top = g.top + 6, bottom = g.bottom - 16;
    mh = std::max (4.0, (bottom - top) * 0.5);
    my = (top + bottom) / 2 - mh / 2 + 2;
}

bool DynDisplay::textsClear (const CRect& g)
{
    // the gain texts sit in the graph's bottom 14 px (glyphs a pixel round them)
    double my, mh;
    meterRows (g, my, mh);
    return my + mh <= g.bottom - 16.0;
}

void DynDisplay::paintBase (CDrawContext* ctx)
{
    const CRect all = getViewSize ();
    ctx->setFillColor (theme::kPanel);
    ctx->drawRect (all, kDrawFilled);

    // column headers and separators
    const double gl = all.left + kLeftCol, gr = all.right - kRightCol;
    text (ctx, "Below", CRect (all.left + 4, all.top, gl - 4, all.top + kHeader), theme::kTextDim, 9.5, kLeftText, true);
    text (ctx, "Above", CRect (gr + 4, all.top, gr + 76, all.top + kHeader), theme::kTextDim, 9.5, kLeftText, true);
    text (ctx, "Att/Rel", CRect (gr + 84, all.top, all.right - 4, all.top + kHeader), theme::kTextDim, 9.5, kLeftText, true);
    ctx->setLineWidth (1.0);
    ctx->setFrameColor (theme::kLineDim);
    ctx->drawLine (CPoint (gr + 80, all.top + 2), CPoint (gr + 80, all.bottom - kScaleHeight));

    const int n = bands ();
    for (int k = 0; k < lanes (); ++k) // the bands, then the Sub band
    {
        const int b = k < n ? k : kSubBand;
        const LaneIds ids = laneIds (b);
        const CRect g = graphRect (b);
        ctx->setFillColor (theme::kWell);
        ctx->drawRect (g, kDrawFilled);
        const bool dim = host->plainValue (ids.active) < 0.5;
        const double tb = ids.sub ? kMinDb : host->plainValue (ids.belowT);
        const double ta = host->plainValue (ids.aboveT);
        const double rb = ids.sub ? 1.0 : host->plainValue (ids.belowR);
        const double ra = host->plainValue (ids.aboveR);
        const double xb = xOf (tb), xa = xOf (ta);

        // grid
        ctx->setLineWidth (1.0);
        ctx->setFrameColor (theme::kGridMinor);
        for (double db = -70.0; db <= -10.0; db += 10.0)
            ctx->drawLine (CPoint (xOf (db), g.top), CPoint (xOf (db), g.bottom));

        // blocks
        ctx->setFillColor (blockColor (rb, true, dim));
        ctx->drawRect (CRect (g.left, g.top, xb, g.bottom), kDrawFilled);
        ctx->setFillColor (blockColor (ra, false, dim));
        ctx->drawRect (CRect (xa, g.top, g.right, g.bottom), kDrawFilled);
        // the thresholds: pale copper lines (copper when the band is bypassed)
        ctx->setLineWidth (1.5);
        ctx->setFrameColor (dim ? theme::kCopper : theme::kCopperPale);
        if (!ids.sub)
            ctx->drawLine (CPoint (xb, g.top), CPoint (xb, g.bottom));
        ctx->drawLine (CPoint (xa, g.top), CPoint (xa, g.bottom));
        ctx->setLineWidth (1.0);
        if (textsClear (g))
            paintGainTexts (ctx, b);
    }

    // dB scale
    const double sy = all.bottom - kScaleHeight;
    for (double db = -80.0; db <= 0.0; db += 10.0)
    {
        char buf[16];
        std::snprintf (buf, sizeof (buf), "%.0f", std::fabs (db)); // Live labels the scale 80 ... 0
        const double cx = std::clamp (xOf (db), gl + 12.0, gr - 12.0);
        text (ctx, buf, CRect (cx - 20, sy + 1, cx + 20, all.bottom), theme::kTextDim, 9.5);
    }
}

void DynDisplay::paintGainTexts (CDrawContext* ctx, int b)
{
    // the gain each block applies at its extreme (silence / 0 dB)
    const LaneIds ids = laneIds (b);
    const CRect g = graphRect (b);
    const int n = bands ();
    const bool ottStyle = std::lround (host->plainValue (kStyle)) == kStyleOtt; // the bands (not the Sub band) run OTT's law
    const bool dim = host->plainValue (ids.active) < 0.5;
    const double tb = ids.sub ? kMinDb : host->plainValue (ids.belowT);
    const double ta = host->plainValue (ids.aboveT);
    const double rb = ids.sub ? 1.0 : host->plainValue (ids.belowR);
    const double ra = host->plainValue (ids.aboveR);
    const double xb = xOf (tb), xa = xOf (ta);
    const CColor tc = dim ? theme::kTextDim : theme::kText;
    const bool knee = host->plainValue (kSoftKnee) >= 0.5;
    const double amount = host->plainValue (kAmount);
    double belowMax = ids.sub ? 0.0 : std::min (36.0, belowGainDb (-120.0, tb, rb, knee) * amount);
    double aboveMax = std::max (-80.0, aboveGainDb (0.0, ta, ra, knee) * amount);
    if (ottStyle && !ids.sub)
    {
        // OTT style (Ott.h, as Engine.cpp sets it up): the gain over OTT's makeup at silence / 0 dB
        // of the band's mean square, the makeup (like the baked output gains) not counted
        const auto& t = paramTable ();
        auto strength = [] (double r, double r0) { return (1.0 - 1.0 / std::max (1e-3, r)) / (1.0 - 1.0 / r0); };
        const int kind = ott::bandKind (b, n);
        const double up = strength (rb, t.info (ids.belowR).def), down = strength (ra, t.info (ids.aboveR).def);
        const double upShift = tb - t.info (ids.belowT).def, downShift = ta - t.info (ids.aboveT).def;
        const double makeup = ott::makeupShape (kind, amount) * ott::kMakeup[kind];
        belowMax = std::max (0.0, ott::gainDb (kind, -120.0, amount, up, down, upShift, downShift) - makeup);
        aboveMax = std::min (0.0, ott::gainDb (kind, 0.0, amount, up, down, upShift, downShift) - makeup);
    }
    if (std::fabs (belowMax) >= 0.05 && xb - g.left > 50)
        text (ctx, gainText (belowMax), CRect (g.left + 4, g.bottom - 14, g.left + 60, g.bottom - 1), tc, 9.5, kLeftText, true);
    if (std::fabs (aboveMax) >= 0.05 && g.right - xa > 50)
        text (ctx, gainText (aboveMax), CRect (g.right - 60, g.bottom - 14, g.right - 4, g.bottom - 1), tc, 9.5, kRightText, true);
}

void DynDisplay::draw (CDrawContext* ctx)
{
    const CRect all = getViewSize ();
    baseLayer.draw (ctx, all, pk::LayerKey ().params (host), [this] (CDrawContext* c) { paintBase (c); });

    const int n = bands ();
    for (int k = 0; k < lanes (); ++k) // the bands, then the Sub band
    {
        const int b = k < n ? k : kSubBand;
        const LaneIds ids = laneIds (b);
        const CRect g = graphRect (b);
        const bool active = host->plainValue (ids.active) >= 0.5;
        const bool dim = !active;

        // meters: thick = output, thin = input
        {
            double my, mh;
            meterRows (g, my, mh);
            // the output lit (energy live; energy idle when bypassed), the input a thin text-coloured bar
            ctx->setFillColor (dim ? theme::kEnergyIdle : theme::kEnergyLive);
            ctx->drawRect (CRect (g.left, my, xOf (shownOut[b]), my + mh), kDrawFilled);
            ctx->setFillColor (theme::withAlpha (theme::kText, dim ? 90 : 210));
            ctx->drawRect (CRect (g.left, my - 4, xOf (shownIn[b]), my - 1.5), kDrawFilled);
        }

        // the gain each block applies at its extreme (in the layer unless the meters reach them), and
        // the current gain change
        if (!textsClear (g))
            paintGainTexts (ctx, b);
        const CColor tc = dim ? theme::kTextDim : theme::kText;
        std::string tag = !active ? "bypassed" : (std::fabs (shownGain[b]) > 0.05f ? gainText (shownGain[b]) + " dB" : "");
        if (!tag.empty ())
            text (ctx, tag, CRect (g.left + 4, g.top + 1, g.left + 120, g.top + 13), tc, 9.0, kLeftText);
    }
}

DynDisplay::Hit DynDisplay::hitTest (const CPoint& p, int& band) const
{
    band = -1;
    for (int b = 0; b < bands (); ++b)
        if (graphRect (b).pointInside (p))
            band = b;
    if (subShown () && graphRect (kSubBand).pointInside (p))
        band = kSubBand;
    if (band < 0)
        return Hit::None;
    const LaneIds ids = laneIds (band);
    const double xa = xOf (host->plainValue (ids.aboveT));
    constexpr double grab = 5.0;
    if (ids.sub) // only an Above block
        return std::fabs (p.x - xa) <= grab ? Hit::AboveEdge : (p.x > xa ? Hit::AboveBlock : Hit::None);
    const double xb = xOf (host->plainValue (ids.belowT));
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
    if (band == kSubBand) // its own: the Sub band's threshold or ratio
    {
        ids.push_back (edge ? kSubThresh : kSubRatio);
        return ids;
    }
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
    const bool right = e.buttonState.isRight (); // a right click resets, like a double-click
    if (!e.buttonState.isLeft () && !right)
        return;
    int band;
    const Hit hit = hitTest (e.mousePosition, band);
    if (hit == Hit::None)
        return;
    const auto ids = targetsFor (hit, band, e.modifiers);
    const bool block = hit == Hit::BelowBlock || hit == Hit::AboveBlock;
    if ((e.clickCount == 2 && block) || right)
    {
        for (auto id : ids) // blocks to 1:1 (no processing), anything else to its default
            host->setOnce (id, block ? host->table ().toNormalized (id, 1.0) : host->table ().defaultNormalized (id));
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
            const double dbPerPx = (kMaxDb - kMinDb) / (getViewSize ().getWidth () - kLeftCol - kRightCol);
            const double v = t.start + (e.mousePosition.x - downPoint.x) * dbPerPx * fine;
            host->setNorm (t.id, host->table ().toNormalized (t.id, v));
        }
        else
        {
            // "volume" of a block: dragging up makes it louder. Above: louder = lower ratio;
            // Below: louder = higher ratio (upward compression).
            const bool belowRatio = t.id >= kBandBase && t.id < kBandBase + kMaxBands * kBandBlock && (t.id - kBandBase) % kBandBlock == kBelowRatio;
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
    Meters* m = meters ? meters () : nullptr;
    bool changed = false;
    for (int b = 0; b <= kNumBands; ++b) // and the Sub band
    {
        const float in = m ? m->inputDb[(size_t)b].load (std::memory_order_relaxed) : -100.0f;
        const float out = m ? m->outputDb[(size_t)b].load (std::memory_order_relaxed) : -100.0f;
        const float g = m ? m->gainDb[(size_t)b].load (std::memory_order_relaxed) : 0.0f;
        // (floor: the levels are drawn no lower than the scale's bottom, so one falling further, as it
        // does for long after the audio stops, changes nothing on the screen and repaints nothing)
        auto ease = [&] (float& shown, float target, float floor) {
            const float next = target > shown ? target : shown + (target - shown) * 0.25f;
            if (std::fabs (std::max (next, floor) - std::max (shown, floor)) > 0.05f)
                changed = true;
            shown = next;
        };
        ease (shownIn[b], in, (float)kMinDb);
        ease (shownOut[b], out, (float)kMinDb);
        ease (shownGain[b], g, -1e9f);
    }
    if (changed)
        invalid ();
}

} // namespace multidyn
