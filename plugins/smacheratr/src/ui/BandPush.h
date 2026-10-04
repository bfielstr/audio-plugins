// Gentlr's No Overlap and glue in the editors (NoOverlap.h and Glue.h have the rules): where a Gentlr's
// bands live among a ParamHost's parameters (Smacheratr's IDs, or Gentlr's own), and the edits that keep
// them apart and hold their glued borders.
//   BandPush                    for a display's drag: begin () when a band is grabbed (it opens a
//                               gesture on every band's Frequency and Width the drag may move, and
//                               first splits any overlap the bands already have and holds the glued
//                               borders), update () after each move of the grabbed band (its neighbours
//                               are pushed, and its glued neighbours follow, from where they were when
//                               the drag began, so dragging back lets them go back), end (). A band edge
//                               dragged to within a few pixels of a neighbour's facing edge snaps onto
//                               it, and the two are glued when the drag ends ("glue on touch"); a glued
//                               edge dragged moves the border (the band's other edge stays, so one band
//                               widens as the other narrows)
//   pushOnce                    a band changed in one step (the wheel, a reset): its neighbours pushed,
//                               its glued neighbours following
//   NoOverlapToggle             the No Overlap button: switched on, bands that overlap are split at the
//                               middle of the overlap (resolveOverlaps), written to their parameters
//   shownLayout, linkBorders, toggleGlue, drawLink   the displays: where the bands sit as the engine
//                               has them, the borders that get a link icon, a click on one (glue or
//                               detach) and the icon itself (a thin copper chain link, lit cinnabar
//                               while glued: docs/THEME.md)
// With No Overlap off and nothing glued, a drag only snaps. Used by Smacheratr's colour display (and so
// by every plug-in's end saturator and Smemplr's rack), and by Gentlr's display.
#pragma once

#include "../core/Glue.h"
#include "../core/NoOverlap.h"
#include "../core/Params.h"

#include "pluginkit/ui/Widgets.h"

#include <cstdint>
#include <functional>
#include <vector>

namespace VSTGUI {
class CDrawContext;
}

namespace smacheratr {

// Where a Gentlr's bands are in a ParamHost, in Smacheratr's order (the two bands, Sub, High).
struct GentlrBandParams
{
    uint32_t freq[kGentlrBands];
    int64_t width[kGentlrBands]; // -1: none (Sub, High)
    uint32_t noOverlap;
    uint32_t glue[kGluePairs]; // the glue switches (GluePair's order)
    std::function<bool (pk::ParamHost*, int)> works; // band k works (only working bands push and are pushed)
};

// Smacheratr's (through a pk::MappedParamHost too: the end saturators, Smemplr's rack).
const GentlrBandParams& smacheratrBandParams ();

GentlrLayout readLayout (pk::ParamHost* host, const GentlrBandParams& bp);
// Sets the parameters where `l` differs from what the host has (setNorm inside gestures the caller
// opened, or setOnce each with `once`).
void writeLayout (pk::ParamHost* host, const GentlrBandParams& bp, const GentlrLayout& l, bool once);
// the glue switches as the host has them
void readGlue (pk::ParamHost* host, const GentlrBandParams& bp, bool glued[kGluePairs]);
bool anyGlue (pk::ParamHost* host, const GentlrBandParams& bp);

// Where the bands sit as the engine has them (glued borders held, with No Overlap kept apart).
GentlrLayout shownLayout (pk::ParamHost* host, const GentlrBandParams& bp);
// The borders that get a link icon (glueBorders on shownLayout); returns how many.
int linkBorders (pk::ParamHost* host, const GentlrBandParams& bp, GlueBorder out[kGentlrBands]);
// A click on a link icon: a glued border is detached (the bands keep where they are), two touching
// bands are glued (the follower's edge set exactly on the border, as the engine would).
void toggleGlue (pk::ParamHost* host, const GentlrBandParams& bp, const GlueBorder& b);
// The link icon at `c`: two thin interlocking copper links across the border, cinnabar while glued
// (brighter when `hot`, the mouse over it). kLinkRadius: how near a click must be.
constexpr double kLinkRadius = 7.0;
void drawLink (VSTGUI::CDrawContext* ctx, const VSTGUI::CPoint& c, bool glued, bool hot);

class BandPush
{
public:
    // what of the band the display grabbed: its body (handle: it moves), an edge, or its width (Alt)
    enum class Grab { Body, LowEdge, HighEdge, Width };
    // Band `band` was grabbed; `open` are the parameters the display opened gestures on itself;
    // `snapOct`: how near (octaves, a few pixels) an edge snaps onto a neighbour's (0: no snapping).
    void begin (pk::ParamHost* host, const GentlrBandParams& bp, int band, const std::vector<uint32_t>& open, Grab grab = Grab::Body,
                double snapOct = 0.0);
    void update (pk::ParamHost* host, const GentlrBandParams& bp); // after the grabbed band's parameters were set
    void end (pk::ParamHost* host, const GentlrBandParams& bp);    // closes the gestures begin opened; glues a snapped pair
    bool active () const { return on; }
    int snapped () const { return snapPair; } // the pair the grabbed edge sits snapped on (GluePair), or -1
    double snappedAt () const { return snapAt; } // where (log2 Hz)

private:
    bool on = false, moves = false; // moves: No Overlap or glue may move the other bands
    int band = 0, snapPair = -1;
    double snapOct = 0.0, snapAt = 0.0;
    Grab grab = Grab::Body;
    GentlrLayout start;
    bool glued[kGluePairs] {};
    std::vector<uint32_t> opened;
};

// Band `band` changed in one step from `before` (read before the change): its neighbours pushed, its
// glued neighbours following.
void pushOnce (pk::ParamHost* host, const GentlrBandParams& bp, int band, const GentlrLayout& before);

class NoOverlapToggle : public pk::Toggle
{
public:
    NoOverlapToggle (const VSTGUI::CRect& r, pk::ParamHost* h, const GentlrBandParams& bp, const char* label = "No Overlap");
    void onMouseDownEvent (VSTGUI::MouseDownEvent& e) override;

private:
    const GentlrBandParams& bands;
};

} // namespace smacheratr
