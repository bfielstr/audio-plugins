// Gentlr's No Overlap (kClarityNoOverlap): its working bands never cover the same frequencies. A band
// covers the octaves between its edges (centre / 2^(Width / 2) to centre * 2^(Width / 2)), the Sub band
// everything below its Freq, the High band everything above its Freq. Along the spectrum they sit in
// one order, Sub first, the bands by their centres, High last, and each one's high edge stays at or
// below the next one's low edge.
//
// Two ways to get there, both pure functions on a GentlrLayout:
//   pushBands        a band was dragged or widened (in an editor): its neighbours' edges are pushed
//                    along (a neighbour gets narrower, and once it is as narrow as a band can be, 0.5
//                    octaves, it moves as a whole), and where they cannot move further (the Sub band
//                    at 20 Hz, the High band at 16 kHz, a band's centre at an end of its range) the
//                    dragged band stops at them.
//   resolveOverlaps  overlapping bands with nobody to blame (No Overlap switched on, a band switched
//                    on, automation): each overlap is split at its middle (on a log axis), each band
//                    giving up half (and pushing on where it has no room).
// The editors push (and write the neighbours' parameters); the engines resolve what they are given
// before designing the bands, so automation that makes bands overlap never gets past them. Bands that
// do not overlap come out exactly as they went in (to the bit), so with the editors' pushing the
// engines change nothing.
#pragma once

#include "Params.h"

#include <algorithm>
#include <cmath>

namespace smacheratr {

// Gentlr's bands as No Overlap sees them, in Smacheratr's order (the two bands, Sub, High).
struct GentlrLayout
{
    bool on[kGentlrBands] {};      // the band works (only working bands push and are pushed)
    double freq[kGentlrBands] {};  // Hz: a band's centre, the Sub and High bands' Freq (where they taper)
    double width[kGentlrBands] {}; // octaves between a band's edges (the Sub and High bands have none)
};

namespace noOverlap {

constexpr double kFar = 1.0e6;   // octaves: past either end of the spectrum (the Sub band's low edge, the High band's high one)
constexpr double kTouch = 1e-6; // octaves: edges this close touch (a parameter's round trip leaves less)

struct Span
{
    double lo, hi; // log2 Hz
};

inline Span spanOf (const GentlrLayout& l, int k)
{
    const double c = std::log2 (std::max (1.0, l.freq[k]));
    if (k == kSubBand)
        return {-kFar, c};
    if (k == kHighBand)
        return {c, kFar};
    return {c - 0.5 * l.width[k], c + 0.5 * l.width[k]};
}

inline void setSpan (GentlrLayout& l, int k, const Span& s)
{
    if (k == kSubBand)
        l.freq[k] = std::exp2 (s.hi);
    else if (k == kHighBand)
        l.freq[k] = std::exp2 (s.lo);
    else
    {
        l.freq[k] = std::exp2 (0.5 * (s.lo + s.hi));
        l.width[k] = s.hi - s.lo;
    }
}

// a band's centre range and narrowest width, log2 Hz and octaves
inline const double kCenterMin = std::log2 (20.0), kCenterMax = std::log2 (20000.0);
constexpr double kNarrowest = kMinWidthOct;

// The working bands in their order along the spectrum: Sub, the bands by their centres (`centres`, the
// layout whose centres decide, e.g. before a drag), High. Returns how many there are.
inline int orderOf (const GentlrLayout& l, int order[kGentlrBands], const GentlrLayout& centres)
{
    int n = 0;
    if (l.on[kSubBand])
        order[n++] = kSubBand;
    const int first = n;
    for (int k = 0; k < kClarityBands; ++k)
        if (l.on[k])
            order[n++] = k;
    std::stable_sort (order + first, order + n, [&] (int a, int b) { return centres.freq[a] < centres.freq[b]; });
    if (l.on[kHighBand])
        order[n++] = kHighBand;
    return n;
}

// floorHi[p]: the lowest the high edge of the band at position p can go, everything below it pressed
// down as far as it goes too; ceilLo[p]: the highest its low edge can go, everything above pressed up.
inline void limits (const int* order, int n, double* floorHi, double* ceilLo)
{
    for (int p = 0; p < n; ++p)
    {
        const int k = order[p];
        const double below = p > 0 ? floorHi[p - 1] : -kFar;
        if (k == kSubBand)
            floorHi[p] = std::log2 (kSubMinHz);
        else if (k == kHighBand)
            floorHi[p] = kFar; // (last: no high edge)
        else
            floorHi[p] = std::max (below, kCenterMin - 0.5 * kNarrowest) + kNarrowest;
    }
    for (int p = n - 1; p >= 0; --p)
    {
        const int k = order[p];
        const double above = p + 1 < n ? ceilLo[p + 1] : kFar;
        if (k == kHighBand)
            ceilLo[p] = std::log2 (kHighMaxHz);
        else if (k == kSubBand)
            ceilLo[p] = -kFar; // (first: no low edge)
        else
            ceilLo[p] = std::min (above, kCenterMax + 0.5 * kNarrowest) - kNarrowest;
    }
}

// The band whose high edge was just set to `hi` (lowered): narrower, its centre kept in range, and as
// a whole lower once it is as narrow as it goes.
inline void lowerHigh (int k, Span& s, double hi)
{
    s.hi = hi;
    if (k == kSubBand || k == kHighBand)
        return;
    s.lo = std::max (s.lo, 2.0 * kCenterMin - s.hi);
    if (s.hi - s.lo < kNarrowest)
        s.lo = s.hi - kNarrowest;
}

// The band whose low edge was just set to `lo` (raised), the same way up.
inline void raiseLow (int k, Span& s, double lo)
{
    s.lo = lo;
    if (k == kSubBand || k == kHighBand)
        return;
    s.hi = std::min (s.hi, 2.0 * kCenterMax - s.lo);
    if (s.hi - s.lo < kNarrowest)
        s.hi = s.lo + kNarrowest;
}

// Pushes the bands after position p up, and those before it down, off the band at p: each push runs
// on until a band that is not touched (an overlap further on is not this push's to settle).
inline void pushFrom (const int* order, int n, int p, Span* span, bool* moved, bool up = true)
{
    for (int q = p + 1; up && q < n && span[q].lo < span[q - 1].hi - kTouch; ++q)
    {
        raiseLow (order[q], span[q], span[q - 1].hi);
        moved[q] = true;
    }
    for (int q = p - 1; q >= 0 && span[q].hi > span[q + 1].lo + kTouch; --q)
    {
        lowerHigh (order[q], span[q], span[q + 1].lo);
        moved[q] = true;
    }
}

} // namespace noOverlap

// Whether two working bands of `l` cover the same frequencies.
inline bool bandsOverlap (const GentlrLayout& l)
{
    using namespace noOverlap;
    int order[kGentlrBands];
    const int n = orderOf (l, order, l);
    for (int p = 0; p + 1 < n; ++p)
        if (spanOf (l, order[p]).hi > spanOf (l, order[p + 1]).lo + kTouch)
            return true;
    return false;
}

// Overlapping working bands, with nobody to blame: each overlap split at its middle (a band with no
// room left pushes on, as far as the others can go). Bands that do not overlap are left exactly as
// they are.
inline void resolveOverlaps (GentlrLayout& l)
{
    using namespace noOverlap;
    int order[kGentlrBands];
    const int n = orderOf (l, order, l);
    Span span[kGentlrBands];
    bool moved[kGentlrBands] {};
    double floorHi[kGentlrBands], ceilLo[kGentlrBands];
    for (int p = 0; p < n; ++p)
        span[p] = spanOf (l, order[p]);
    limits (order, n, floorHi, ceilLo);
    for (int p = 0; p + 1 < n; ++p)
    {
        if (span[p].hi <= span[p + 1].lo + kTouch || floorHi[p] > ceilLo[p + 1])
            continue;
        const double mid = std::clamp (0.5 * (span[p].hi + span[p + 1].lo), floorHi[p], ceilLo[p + 1]);
        lowerHigh (order[p], span[p], std::min (mid, span[p].hi));
        raiseLow (order[p + 1], span[p + 1], std::max (mid, span[p + 1].lo));
        moved[p] = moved[p + 1] = true;
        // the lower band, narrowed past what a band can be, moved down: what is below it is pushed
        // down (as far as it goes: mid is above floorHi). The upper one may now reach into the band
        // after it: that is the next pair's overlap, split the same way (pushing on from here could
        // push that band past its range).
        pushFrom (order, n, p, span, moved, false);
    }
    for (int p = 0; p < n; ++p)
        if (moved[p])
            setSpan (l, order[p], span[p]);
}

// Band `moved` of `l` was dragged or widened from where it was in `before` (the other bands as they
// were): it stops where its neighbours cannot make room, and pushes them along as far as they can. A
// band whose Width changed keeps its centre (it narrows where it meets a neighbour that cannot move);
// one that moved keeps its Width (squeezed only where the room between the neighbours is narrower).
// The order along the spectrum is `before`'s, so a band pushes its neighbour rather than jumping it.
inline void pushBands (GentlrLayout& l, int moved, const GentlrLayout& before)
{
    using namespace noOverlap;
    int order[kGentlrBands];
    const int n = orderOf (l, order, before);
    int p = -1;
    for (int q = 0; q < n; ++q)
        if (order[q] == moved)
            p = q;
    if (p < 0)
        return; // (it does not work: it pushes nothing)
    Span span[kGentlrBands];
    bool movedAt[kGentlrBands] {};
    double floorHi[kGentlrBands], ceilLo[kGentlrBands];
    for (int q = 0; q < n; ++q)
        span[q] = spanOf (l, order[q]);
    limits (order, n, floorHi, ceilLo);
    // the room the neighbours can make, pressed as far as they go
    const double lo = p > 0 ? floorHi[p - 1] : -kFar, hi = p + 1 < n ? ceilLo[p + 1] : kFar;
    Span& s = span[p];
    const Span was = s;
    if (moved == kSubBand)
        s.hi = std::min (s.hi, hi);
    else if (moved == kHighBand)
        s.lo = std::max (s.lo, lo);
    else
    {
        if (hi - lo < kNarrowest)
            return; // (no room at all: cannot happen with the parameters' ranges)
        double c = 0.5 * (s.lo + s.hi), w = s.hi - s.lo;
        if (std::fabs (l.width[moved] - before.width[moved]) > 1e-12)
        {
            // widened or narrowed about its centre: as wide as the room on its tighter side allows
            c = std::clamp (c, lo + 0.5 * kNarrowest, hi - 0.5 * kNarrowest);
            w = std::max (kNarrowest, std::min (w, 2.0 * std::min (c - lo, hi - c)));
        }
        else
        {
            w = std::min (w, hi - lo);
            c = std::clamp (c, lo + 0.5 * w, hi - 0.5 * w);
        }
        s = {c - 0.5 * w, c + 0.5 * w};
    }
    movedAt[p] = std::fabs (s.lo - was.lo) > 1e-12 || std::fabs (s.hi - was.hi) > 1e-12; // (left as it is where it fits)
    pushFrom (order, n, p, span, movedAt);
    for (int q = 0; q < n; ++q)
        if (movedAt[q])
            setSpan (l, order[q], span[q]);
}

} // namespace smacheratr
