// Gently's No Overlap in the editors (NoOverlap.h has the rules): where a Gently's bands live among a
// ParamHost's parameters (Smacheratr's IDs, or Gently's own), and the edits that keep them apart.
//   BandPush                    for a display's drag: begin () when a band is grabbed (it opens a
//                               gesture on every band's Frequency and Width the drag may push, and
//                               first splits any overlap the bands already have), update () after each
//                               move of the grabbed band (its neighbours are pushed from where they were
//                               when the drag began, so dragging back lets them go back), end ()
//   pushOnce                    a band changed in one step (the wheel, a reset): its neighbours pushed
//   NoOverlapToggle             the No Overlap button: switched on, bands that overlap are split at the
//                               middle of the overlap (resolveOverlaps), written to their parameters
// With No Overlap off all of it does nothing. Used by Smacheratr's colour display (and so by every
// plug-in's end saturator and Smemplr's rack), and by Gently's display.
#pragma once

#include "../core/NoOverlap.h"
#include "../core/Params.h"

#include "pluginkit/ui/Widgets.h"

#include <cstdint>
#include <functional>
#include <vector>

namespace smacheratr {

// Where a Gently's bands are in a ParamHost, in Smacheratr's order (the two bands, Sub, High).
struct GentlyBandParams
{
    uint32_t freq[kGentlyBands];
    int64_t width[kGentlyBands]; // -1: none (Sub, High)
    uint32_t noOverlap;
    std::function<bool (pk::ParamHost*, int)> works; // band k works (only working bands push and are pushed)
};

// Smacheratr's (through a pk::MappedParamHost too: the end saturators, Smemplr's rack).
const GentlyBandParams& smacheratrBandParams ();

GentlyLayout readLayout (pk::ParamHost* host, const GentlyBandParams& bp);
// Sets the parameters where `l` differs from what the host has (setNorm inside gestures the caller
// opened, or setOnce each with `once`).
void writeLayout (pk::ParamHost* host, const GentlyBandParams& bp, const GentlyLayout& l, bool once);

class BandPush
{
public:
    // Band `band` was grabbed; `open` are the parameters the display opened gestures on itself.
    void begin (pk::ParamHost* host, const GentlyBandParams& bp, int band, const std::vector<uint32_t>& open);
    void update (pk::ParamHost* host, const GentlyBandParams& bp); // after the grabbed band's parameters were set
    void end (pk::ParamHost* host);                                // closes the gestures begin opened
    bool active () const { return on; }

private:
    bool on = false;
    int band = 0;
    GentlyLayout start;
    std::vector<uint32_t> opened;
};

// Band `band` changed in one step from `before` (read before the change): its neighbours pushed.
void pushOnce (pk::ParamHost* host, const GentlyBandParams& bp, int band, const GentlyLayout& before);

class NoOverlapToggle : public pk::Toggle
{
public:
    NoOverlapToggle (const VSTGUI::CRect& r, pk::ParamHost* h, const GentlyBandParams& bp, const char* label = "No Overlap");
    void onMouseDownEvent (VSTGUI::MouseDownEvent& e) override;

private:
    const GentlyBandParams& bands;
};

} // namespace smacheratr
