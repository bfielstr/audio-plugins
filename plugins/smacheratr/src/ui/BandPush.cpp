#include "BandPush.h"

#include "pluginkit/GentlrDefaults.h"
#include "pluginkit/ui/Theme.h"

#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cgraphicspath.h"
#include "vstgui/lib/events.h"

#include <algorithm>
#include <cmath>

namespace smacheratr {

const GentlrBandParams& smacheratrBandParams ()
{
    static const GentlrBandParams bp {
        {kClarityFreq, kClarity2Freq, kClaritySubFreq, kClarityHighFreq},
        {kClarityWidth, kClarity2Width, -1, -1},
        kClarityNoOverlap,
        {kClarityGlue12, kClarityGlueSub1, kClarityGlueSub2, kClarityGlue1High, kClarityGlue2High},
        // (every band the same way: Sub and High have no button of their own)
        [] (pk::ParamHost* h, int k) { return clarityBandOn (h->plainValue (kClarity), h->plainValue (kGentlrRangeIds[k])); }};
    return bp;
}

GentlrLayout readLayout (pk::ParamHost* host, const GentlrBandParams& bp)
{
    GentlrLayout l;
    for (int k = 0; k < kGentlrBands; ++k)
    {
        l.on[k] = bp.works (host, k);
        l.freq[k] = host->plainValue (bp.freq[k]);
        l.width[k] = bp.width[k] >= 0 ? host->plainValue ((uint32_t)bp.width[k]) : 0.0;
    }
    return l;
}

void writeLayout (pk::ParamHost* host, const GentlrBandParams& bp, const GentlrLayout& l, bool once)
{
    auto set = [&] (uint32_t id, double plain) {
        if (std::fabs (plain - host->plainValue (id)) <= 1e-9 * std::max (1.0, std::fabs (plain)))
            return; // (as it is: a pushed band's round trip through its parameters leaves less)
        const double n = host->table ().toNormalized (id, plain);
        if (once)
            host->setOnce (id, n);
        else
            host->setNorm (id, n);
    };
    for (int k = 0; k < kGentlrBands; ++k)
    {
        if (!l.on[k])
            continue;
        set (bp.freq[k], l.freq[k]);
        if (bp.width[k] >= 0)
            set ((uint32_t)bp.width[k], l.width[k]);
    }
}

void readGlue (pk::ParamHost* host, const GentlrBandParams& bp, bool glued[kGluePairs])
{
    for (int g = 0; g < kGluePairs; ++g)
        glued[g] = host->plainValue (bp.glue[g]) >= 0.5;
}

bool anyGlue (pk::ParamHost* host, const GentlrBandParams& bp)
{
    bool glued[kGluePairs];
    readGlue (host, bp, glued);
    return std::any_of (glued, glued + kGluePairs, [] (bool b) { return b; });
}

GentlrLayout shownLayout (pk::ParamHost* host, const GentlrBandParams& bp)
{
    GentlrLayout l = readLayout (host, bp);
    bool glued[kGluePairs];
    readGlue (host, bp, glued);
    applyGlue (l, glued);
    if (host->plainValue (bp.noOverlap) >= 0.5)
        resolveOverlaps (l);
    return l;
}

int linkBorders (pk::ParamHost* host, const GentlrBandParams& bp, GlueBorder out[kGentlrBands])
{
    bool glued[kGluePairs];
    readGlue (host, bp, glued);
    return glueBorders (shownLayout (host, bp), glued, out);
}

void toggleGlue (pk::ParamHost* host, const GentlrBandParams& bp, const GlueBorder& b)
{
    if (b.glued)
    {
        host->setOnce (bp.glue[b.pair], 0.0); // (detached: both keep where they are)
        return;
    }
    host->setOnce (bp.glue[b.pair], 1.0);
    // the two only touched: the follower's edge exactly on the border, as the engine holds it
    GentlrLayout l = readLayout (host, bp);
    bool glued[kGluePairs];
    readGlue (host, bp, glued);
    applyGlue (l, glued);
    if (host->plainValue (bp.noOverlap) >= 0.5)
        resolveOverlaps (l);
    writeLayout (host, bp, l, true);
}

void drawLink (VSTGUI::CDrawContext* ctx, const VSTGUI::CPoint& c, bool glued, bool hot)
{
    using namespace VSTGUI;
    namespace theme = pk::theme;
    // a well behind it, so it reads over the bands' regions and curves
    ctx->setFillColor (theme::withAlpha (theme::kWell, 230));
    ctx->drawEllipse (CRect (c.x - kLinkRadius, c.y - kLinkRadius + 1, c.x + kLinkRadius, c.y + kLinkRadius - 1), kDrawFilled);
    // two links of a chain across the border, overlapping at it
    const CColor col = glued ? theme::kEnergyLive : theme::withAlpha (theme::kCopper, hot ? 255 : 190);
    ctx->setFrameColor (col);
    ctx->setLineWidth (hot ? 1.5 : 1.0);
    ctx->setLineStyle (kLineSolid);
    for (double dx : {-2.5, 2.5})
    {
        const CRect r (c.x + dx - 4.0, c.y - 2.5, c.x + dx + 4.0, c.y + 2.5);
        if (auto path = owned (ctx->createRoundRectGraphicsPath (r, 2.5)))
            ctx->drawGraphicsPath (path, CDrawContext::kPathStroked);
    }
    ctx->setLineWidth (1.0);
}

void BandPush::begin (pk::ParamHost* host, const GentlrBandParams& bp, int b, const std::vector<uint32_t>& open, Grab g, double snap)
{
    for (uint32_t id : opened)
        host->endEdit (id);
    opened.clear ();
    on = true;
    band = b;
    grab = g;
    snapOct = snap;
    snapPair = -1;
    readGlue (host, bp, glued);
    start = readLayout (host, bp);
    const bool noOverlap = host->plainValue (bp.noOverlap) >= 0.5;
    moves = noOverlap || std::any_of (glued, glued + kGluePairs, [] (bool x) { return x; });
    if (!moves)
        return; // (only the grabbed band's own parameters change: a snap)
    // every working band may be pushed or follow (the display has opened the grabbed band's own gestures)
    for (int k = 0; k < kGentlrBands; ++k)
    {
        if (!start.on[k])
            continue;
        for (int64_t id : {(int64_t)bp.freq[k], bp.width[k]})
            if (id >= 0 && std::find (open.begin (), open.end (), (uint32_t)id) == open.end ())
            {
                host->beginEdit ((uint32_t)id);
                opened.push_back ((uint32_t)id);
            }
    }
    // glued borders held and overlaps from before (automation, a band switched on) split first, so the
    // drag starts from what the engine plays and the display shows
    applyGlue (start, glued);
    if (noOverlap)
        resolveOverlaps (start);
    writeLayout (host, bp, start, false);
}

void BandPush::update (pk::ParamHost* host, const GentlrBandParams& bp)
{
    if (!on)
        return;
    using noOverlap::Span;
    GentlrLayout l = start;
    l.freq[band] = host->plainValue (bp.freq[band]);
    if (bp.width[band] >= 0)
        l.width[band] = host->plainValue ((uint32_t)bp.width[band]);
    const bool edge = grab == Grab::LowEdge || grab == Grab::HighEdge, high = grab == Grab::HighEdge;
    // a glued edge dragged moves the border: the band's other edge stays where it was (the display set
    // the edge with the band centred: its new edge is where the mouse is)
    if (edge && hasWidth (band) && start.on[band] && gluedAt (start, band, high, glued))
    {
        const Span s = noOverlap::spanOf (l, band), s0 = noOverlap::spanOf (start, band);
        Span t = s0;
        if (high)
            t.hi = std::clamp (s.hi, s0.lo + kMinWidthOct, s0.lo + kMaxWidthOct);
        else
            t.lo = std::clamp (s.lo, s0.hi - kMaxWidthOct, s0.hi - kMinWidthOct);
        noOverlap::setSpan (l, band, t);
    }
    // an edge near a neighbour's facing edge snaps onto it (the pair glues when the drag ends)
    snapPair = -1;
    if (snapOct > 0.0 && grab != Grab::Width && start.on[band])
    {
        double best = snapOct, target = 0.0;
        int to = -1;
        bool toHigh = false;
        const Span s = noOverlap::spanOf (l, band);
        for (bool side : {false, true})
        {
            if ((edge && side != high) || (band == kSubBand && !side) || (band == kHighBand && side) || gluedAt (start, band, side, glued))
                continue;
            double t = 0.0;
            const double e = side ? s.hi : s.lo;
            const int j = snapTarget (start, band, side, e, best, glued, &t);
            if (j >= 0)
            {
                best = std::fabs (t - e);
                to = j;
                target = t;
                toHigh = side;
            }
        }
        bool snapped = to >= 0;
        if (snapped && !hasWidth (band))
            l.freq[band] = std::exp2 (target); // (the Sub and High bands' Freq is their edge)
        else if (snapped && grab == Grab::Body)
        {
            const double shift = target - (toHigh ? s.hi : s.lo);
            noOverlap::setSpan (l, band, {s.lo + shift, s.hi + shift});
        }
        else if (snapped)
        {
            // an edge dragged with the band centred: the width that puts the edge there
            const double w = 2.0 * std::fabs (target - 0.5 * (s.lo + s.hi));
            snapped = w >= kMinWidthOct && w <= kMaxWidthOct;
            if (snapped)
                l.width[band] = w;
        }
        if (snapped)
        {
            snapPair = glue::pairOf (band, to);
            snapAt = target;
        }
    }
    if (host->plainValue (bp.noOverlap) >= 0.5)
        pushBands (l, band, start);
    followGlue (l, band, start, glued);
    writeLayout (host, bp, l, false);
}

void BandPush::end (pk::ParamHost* host, const GentlrBandParams& bp)
{
    for (uint32_t id : opened)
        host->endEdit (id);
    opened.clear ();
    if (on && snapPair >= 0)
        host->setOnce (bp.glue[snapPair], 1.0); // (glue on touch)
    snapPair = -1;
    on = false;
}

double touchSnapOctaves (double snapOct) { return pk::glueOnTouch () ? snapOct : 0.0; }

void pushOnce (pk::ParamHost* host, const GentlrBandParams& bp, int band, const GentlrLayout& before)
{
    const bool noOverlap = host->plainValue (bp.noOverlap) >= 0.5;
    bool glued[kGluePairs];
    readGlue (host, bp, glued);
    if (!noOverlap && !std::any_of (glued, glued + kGluePairs, [] (bool x) { return x; }))
        return;
    GentlrLayout was = before;
    applyGlue (was, glued);
    if (noOverlap)
        resolveOverlaps (was);
    GentlrLayout l = readLayout (host, bp);
    for (int k = 0; k < kGentlrBands; ++k)
        if (k != band)
        {
            l.freq[k] = was.freq[k];
            l.width[k] = was.width[k];
        }
    if (noOverlap)
        pushBands (l, band, was);
    followGlue (l, band, was, glued);
    writeLayout (host, bp, l, true);
}

NoOverlapToggle::NoOverlapToggle (const VSTGUI::CRect& r, pk::ParamHost* h, const GentlrBandParams& bp, const char* label)
    : pk::Toggle (r, h, bp.noOverlap, label), bands (bp)
{
}

void NoOverlapToggle::onMouseDownEvent (VSTGUI::MouseDownEvent& e)
{
    const bool was = host->plainValue (param) >= 0.5;
    pk::Toggle::onMouseDownEvent (e);
    if (was || host->plainValue (param) < 0.5)
        return;
    // switched on: bands that overlap are split at the middle of the overlap
    GentlrLayout l = readLayout (host, bands);
    resolveOverlaps (l);
    writeLayout (host, bands, l, true);
}

} // namespace smacheratr
