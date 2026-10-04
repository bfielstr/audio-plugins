#include "BandPush.h"

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

void BandPush::begin (pk::ParamHost* host, const GentlrBandParams& bp, int b, const std::vector<uint32_t>& open)
{
    end (host);
    if (host->plainValue (bp.noOverlap) < 0.5)
        return;
    on = true;
    band = b;
    start = readLayout (host, bp);
    // every working band may be pushed (the display has opened the grabbed band's own gestures)
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
    // overlaps from before (automation, a band switched on) split first, so the drag starts from what
    // the engine plays and the display shows
    resolveOverlaps (start);
    writeLayout (host, bp, start, false);
}

void BandPush::update (pk::ParamHost* host, const GentlrBandParams& bp)
{
    if (!on)
        return;
    GentlrLayout l = start;
    l.freq[band] = host->plainValue (bp.freq[band]);
    if (bp.width[band] >= 0)
        l.width[band] = host->plainValue ((uint32_t)bp.width[band]);
    pushBands (l, band, start);
    writeLayout (host, bp, l, false);
}

void BandPush::end (pk::ParamHost* host)
{
    for (uint32_t id : opened)
        host->endEdit (id);
    opened.clear ();
    on = false;
}

void pushOnce (pk::ParamHost* host, const GentlrBandParams& bp, int band, const GentlrLayout& before)
{
    if (host->plainValue (bp.noOverlap) < 0.5)
        return;
    GentlrLayout was = before;
    resolveOverlaps (was);
    GentlrLayout l = readLayout (host, bp);
    for (int k = 0; k < kGentlrBands; ++k)
        if (k != band)
        {
            l.freq[k] = was.freq[k];
            l.width[k] = was.width[k];
        }
    pushBands (l, band, was);
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
