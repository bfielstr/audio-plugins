// Gentlr's glue (kClarityGlue12 .. kClarityGlue2High): two neighbouring bands held at a shared border.
// A band's edges are as No Overlap sees them (NoOverlap.h: a band from centre / 2^(Width / 2) to centre
// * 2^(Width / 2), the Sub band up to its Freq, the High band from its Freq), so a glued border is one
// band's high edge sitting exactly on the next one's low edge; for the Sub and High bands their Freq is
// the border. The pairs that can meet, each with its own switch (GluePair): band 1 and band 2, the Sub
// band and either band, either band and the High band (the Sub band ends by 100 Hz and the High band
// starts at 2 kHz, so those two never meet).
//
// A glue switch holds while its two bands both work and are neighbours along the spectrum (Sub first,
// the bands by their centres, High last, as No Overlap orders them); otherwise it waits, and holds
// again once they are neighbours. Pure functions on a GentlrLayout, as No Overlap's:
//   applyGlue     the engines (and the displays, to draw what the engines play): the border is kept
//                 equal under automation. The lower band leads (a band's low edge follows the high
//                 edge of the band or Sub band under it), except that the High band leads the band
//                 under it (the shelf's Freq is the border). The follower keeps its far edge (one band
//                 gets wider as the other narrows); a band that would be narrower than 0.5 octaves or
//                 wider than 4 keeps that width and moves. Borders already equal (to kGlueTol, far
//                 less than a parameter's round trip) are left exactly as they are, so what the
//                 editors write comes out bit for bit and states without glue sound as they did.
//   followGlue    the editors: a band was dragged (moved, widened, or one edge dragged) and the
//                 bands glued to it follow, each along to the next glued one; a Sub or High band
//                 glued to it holds the dragged band at the end of its own range.
//   snapTarget    the editors: a band edge dragged to within a few pixels of a neighbour's facing
//                 edge lands exactly on it, and the two glue when the drag ends ("glue on touch").
//   glueBorders   the borders between neighbours that touch, glued or not: where the displays draw
//                 the link icons (a click glues or detaches).
//   neighbourOf, gluedAt   which band is next to a band along the spectrum, and whether they are glued.
// The engines run applyGlue before No Overlap's resolveOverlaps; where the two disagree (automation
// moving a third band onto a glued pair) No Overlap wins, it is the one that promises something.
#pragma once

#include "NoOverlap.h"
#include "Params.h"

#include <algorithm>
#include <cmath>

namespace smacheratr {

// The glue switches, in the order of kClarityGlueIds (and of Gentlr's own glue IDs).
enum GluePair : int
{
    kGlue12 = 0, // band 1 and band 2
    kGlueSub1,   // the Sub band and band 1
    kGlueSub2,   // the Sub band and band 2
    kGlue1High,  // band 1 and the High band
    kGlue2High,  // band 2 and the High band
};
static_assert (kGlue2High + 1 == kGluePairs, "one switch per pair");

namespace glue {

constexpr double kGlueTol = 1e-4;  // octaves: borders this close are already glued (left exactly as they are)
constexpr double kTouchOct = 0.02; // octaves: edges this close touch, for the link icon (about a pixel)
inline const double kSubLo = std::log2 (kSubMinHz), kSubHi = std::log2 (kSubMaxHz);   // the Sub band's border range
inline const double kHighLo = std::log2 (kHighMinHz), kHighHi = std::log2 (kHighMaxHz); // the High band's

// the switch for bands a and b (either order), or -1 when they cannot glue
constexpr int pairOf (int a, int b)
{
    if (a > b)
    {
        const int t = a;
        a = b;
        b = t;
    }
    return a == 0 && b == 1               ? kGlue12
           : a == 0 && b == kSubBand      ? kGlueSub1
           : a == 1 && b == kSubBand      ? kGlueSub2
           : a == 0 && b == kHighBand     ? kGlue1High
           : a == 1 && b == kHighBand     ? kGlue2High
                                          : -1;
}
// the two bands of switch g
constexpr int firstOf (int g) { return g == kGlue12 || g == kGlue1High ? 0 : g == kGlueSub1 ? kSubBand : g == kGlueSub2 ? kSubBand : 1; }
constexpr int secondOf (int g) { return g == kGlue12 ? 1 : g == kGlueSub1 ? 0 : g == kGlueSub2 ? 1 : kHighBand; }

// The band (with a width) whose low edge was set to `lo`, its high edge kept: as wide as that leaves
// it (0.5 to 4 octaves; past that it keeps the width and moves), its centre kept in range.
inline void followLow (noOverlap::Span& s, double lo)
{
    const double w = std::clamp (s.hi - lo, kMinWidthOct, kMaxWidthOct);
    s = {lo, lo + w};
}
// The same with its high edge set to `hi`, its low edge kept.
inline void followHigh (noOverlap::Span& s, double hi)
{
    const double w = std::clamp (hi - s.lo, kMinWidthOct, kMaxWidthOct);
    s = {hi - w, hi};
}

// switch g is on, from a layout's glue array
inline bool on (const bool* glued, int a, int b)
{
    const int g = pairOf (a, b);
    return g >= 0 && glued[g];
}

} // namespace glue

// The working band next to band k along the spectrum of `l` (above it, or below), or -1.
inline int neighbourOf (const GentlrLayout& l, int k, bool above)
{
    int order[kGentlrBands];
    const int n = noOverlap::orderOf (l, order, l);
    for (int p = 0; p < n; ++p)
        if (order[p] == k)
            return above ? (p + 1 < n ? order[p + 1] : -1) : (p > 0 ? order[p - 1] : -1);
    return -1;
}

// Band k's edge above (its high edge, or below: its low edge) is glued to its neighbour there.
inline bool gluedAt (const GentlrLayout& l, int k, bool above, const bool glued[kGluePairs])
{
    const int j = neighbourOf (l, k, above);
    return j >= 0 && glue::on (glued, k, j);
}

// The engines (and the displays): the working bands glued to a neighbour keep their shared border
// equal (glued[g]: switch g, GluePair's order). Bands that are not glued, or whose border is already
// equal, are left exactly as they are.
inline void applyGlue (GentlrLayout& l, const bool glued[kGluePairs])
{
    using namespace noOverlap;
    int order[kGentlrBands];
    const int n = orderOf (l, order, l);
    Span span[kGentlrBands];
    bool moved[kGentlrBands] {};
    for (int p = 0; p < n; ++p)
        span[p] = spanOf (l, order[p]);
    // from the bottom up: a band's low edge follows the band (or the Sub band's Freq) under it
    for (int p = 0; p + 1 < n; ++p)
    {
        const int a = order[p], b = order[p + 1];
        if (b == kHighBand || !glue::on (glued, a, b) || std::fabs (span[p + 1].lo - span[p].hi) <= glue::kGlueTol)
            continue;
        glue::followLow (span[p + 1], span[p].hi);
        moved[p + 1] = true;
    }
    // the High band's Freq leads the band under it (its high edge follows; its low edge may then leave
    // a glued band under it, where both cannot hold: the High band is the one with a fixed range)
    if (n >= 2 && order[n - 1] == kHighBand)
    {
        const int a = order[n - 2];
        if (glue::on (glued, a, kHighBand) && std::fabs (span[n - 2].hi - span[n - 1].lo) > glue::kGlueTol)
        {
            glue::followHigh (span[n - 2], span[n - 1].lo);
            moved[n - 2] = true;
        }
    }
    for (int p = 0; p < n; ++p)
        if (moved[p])
            setSpan (l, order[p], span[p]);
}

// The editors: band `moved` of `l` was dragged from where it was in `before` (the other bands as they
// were, or as No Overlap pushed them). The bands glued to it follow (a follower keeps its far edge; at
// 0.5 or 4 octaves it keeps that width and moves, and the band glued to it beyond follows in turn). A
// Sub or High band glued to it (whose border has a range: 20 - 100 Hz, 2 - 16 kHz) holds it: a band
// that moved stops there, one whose edge or width was dragged has that edge stop there. The order
// along the spectrum is `before`'s.
inline void followGlue (GentlrLayout& l, int moved, const GentlrLayout& before, const bool glued[kGluePairs])
{
    using namespace noOverlap;
    int order[kGentlrBands];
    const int n = orderOf (l, order, before);
    int p = -1;
    for (int q = 0; q < n; ++q)
        if (order[q] == moved)
            p = q;
    if (p < 0)
        return; // (it does not work: nothing is glued to it)
    Span span[kGentlrBands], was[kGentlrBands];
    bool changed[kGentlrBands] {};
    for (int q = 0; q < n; ++q)
    {
        span[q] = spanOf (l, order[q]);
        was[q] = span[q];
    }
    auto glueAt = [&] (int q) { return q >= 0 && q + 1 < n && glue::on (glued, order[q], order[q + 1]); }; // (border q, q + 1)
    // held by a glued Sub or High band next to it
    if (hasWidth (moved))
    {
        Span& s = span[p];
        const bool subBelow = p > 0 && order[p - 1] == kSubBand && glueAt (p - 1);
        const bool highAbove = p + 1 < n && order[p + 1] == kHighBand && glueAt (p);
        if (std::fabs (l.width[moved] - before.width[moved]) <= 1e-12)
        {
            // moved as a whole: shifted back into the room the shelves allow
            double lo = -kFar, hi = kFar; // how far it may shift
            if (subBelow)
            {
                lo = std::max (lo, glue::kSubLo - s.lo);
                hi = std::min (hi, glue::kSubHi - s.lo);
            }
            if (highAbove)
            {
                lo = std::max (lo, glue::kHighLo - s.hi);
                hi = std::min (hi, glue::kHighHi - s.hi);
            }
            const double shift = lo <= hi ? std::clamp (0.0, lo, hi) : 0.0;
            s = {s.lo + shift, s.hi + shift};
        }
        else
        {
            if (subBelow)
                s.lo = std::clamp (s.lo, glue::kSubLo, glue::kSubHi);
            if (highAbove)
                s.hi = std::clamp (s.hi, glue::kHighLo, glue::kHighHi);
            s.lo = std::min (s.lo, s.hi - kNarrowest);
        }
        changed[p] = std::fabs (s.lo - was[p].lo) > 1e-12 || std::fabs (s.hi - was[p].hi) > 1e-12;
    }
    // up: each glued band's low edge follows the one under it
    for (int q = p + 1; q < n && glueAt (q - 1); ++q)
    {
        const double edge = span[q - 1].hi;
        if (std::fabs (span[q].lo - edge) <= glue::kGlueTol)
            break;
        if (order[q] == kHighBand)
            span[q].lo = std::clamp (edge, glue::kHighLo, glue::kHighHi);
        else
            glue::followLow (span[q], edge);
        changed[q] = true;
        if (std::fabs (span[q].hi - was[q].hi) <= 1e-12)
            break; // (its far edge stayed: the next border is as it was)
    }
    // down: each glued band's high edge follows the one over it
    for (int q = p - 1; q >= 0 && glueAt (q); --q)
    {
        const double edge = span[q + 1].lo;
        if (std::fabs (span[q].hi - edge) <= glue::kGlueTol)
            break;
        if (order[q] == kSubBand)
            span[q].hi = std::clamp (edge, glue::kSubLo, glue::kSubHi);
        else
            glue::followHigh (span[q], edge);
        changed[q] = true;
        if (std::fabs (span[q].lo - was[q].lo) <= 1e-12)
            break;
    }
    for (int q = 0; q < n; ++q)
        if (changed[q])
            setSpan (l, order[q], span[q]);
}

// The editors' snap: the working neighbour of band `moved` whose facing edge in `at` (a band above it:
// its low edge, or the High band's Freq, when `high`; one below: its high edge, or the Sub band's
// Freq) is within `tol` octaves of `edge` (moved's edge, log2 Hz), a pair that can glue and is not
// glued yet. Returns that band (its edge in *target), or -1.
inline int snapTarget (const GentlrLayout& at, int moved, bool high, double edge, double tol, const bool glued[kGluePairs],
                       double* target)
{
    using namespace noOverlap;
    int best = -1;
    double bestD = tol;
    const double c = std::log2 (std::max (1.0, at.freq[moved]));
    for (int j = 0; j < kGentlrBands; ++j)
    {
        const int g = glue::pairOf (moved, j);
        if (j == moved || g < 0 || glued[g] || !at.on[j])
            continue;
        const double cj = std::log2 (std::max (1.0, at.freq[j]));
        // j on the side the edge faces (the Sub band is always below, the High band always above)
        const bool above = j == kHighBand || (j != kSubBand && moved != kHighBand && (moved == kSubBand || cj > c));
        if (above != high)
            continue;
        const Span s = spanOf (at, j);
        const double e = high ? s.lo : s.hi;
        const double d = std::fabs (e - edge);
        if (d <= bestD)
        {
            bestD = d;
            best = j;
            *target = e;
        }
    }
    return best;
}

// A border between neighbours along the spectrum where a link icon goes.
struct GlueBorder
{
    int pair;     // its switch (GluePair)
    int lower, upper;
    double at;    // log2 Hz (the lower band's high edge)
    bool glued;   // its switch is on (otherwise the two only touch: a click glues them)
};

// The borders of `l` (as the engine has it: after applyGlue and resolveOverlaps) where two working
// neighbours that can glue touch (to glue::kTouchOct), or are glued. Returns how many (at most
// kGentlrBands - 1), lowest first.
inline int glueBorders (const GentlrLayout& l, const bool glued[kGluePairs], GlueBorder* out)
{
    using namespace noOverlap;
    int order[kGentlrBands];
    const int n = orderOf (l, order, l);
    int count = 0;
    for (int p = 0; p + 1 < n; ++p)
    {
        const int a = order[p], b = order[p + 1], g = glue::pairOf (a, b);
        if (g < 0)
            continue;
        const double hi = spanOf (l, a).hi, lo = spanOf (l, b).lo;
        if (glued[g] || std::fabs (hi - lo) <= glue::kTouchOct)
            out[count++] = {g, a, b, hi, glued[g]};
    }
    return count;
}

} // namespace smacheratr
