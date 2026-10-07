// Smeezr's display: the ten bands' levels against the pink target, and the gain each band gets.
//
//   base   the grid (the band borders, 100 / 1k / 10k, dB lines) and the GAIN lane under the levels, with
//          their labels: drawn once into a cached layer (pk::CachedLayer), rebuilt only with the view's size
//   live   while sound plays: each band's measured level (a copper bar), where its gains put it (a pale
//          mark), the pink target (a cinnabar line: every band's equal share of the power), and in the GAIN
//          lane the pink stage's gain (a bar) with the OTT stage's on top of it (a cinnabar mark at the total)
//
// The live part repaints only when a value it shows changed (levels in 0.5 dB steps, gains in 0.25 dB
// steps); once the input has been quiet for half a second the display stops asking for repaints.
#pragma once

#include "Engine.h"
#include "Params.h"

#include "pluginkit/ui/CachedLayer.h"
#include "pluginkit/ui/Widgets.h"

#include "vstgui/lib/cview.h"

#include <functional>

namespace smeezr {

class PinkView : public VSTGUI::CView
{
public:
    using MeterSource = std::function<const Meters* ()>;
    PinkView (const VSTGUI::CRect& r, MeterSource meters);
    void draw (VSTGUI::CDrawContext* ctx) override;
    void idle (); // follows the meters

private:
    void paintBase (VSTGUI::CDrawContext* ctx);
    VSTGUI::CRect levels () const; // the levels' plot
    VSTGUI::CRect lane () const;   // the GAIN lane
    double xOf (double hz) const;
    double yLevel (double db) const;
    double yGain (double db) const;

    MeterSource meters;
    bool active = false;
    float pink = 0.0f, ott = 0.0f, target = -120.0f;
    float level[kBands] {}, pinkDb[kBands] {}, ottDb[kBands] {};
    uint32_t seen = 0;
    pk::CachedLayer baseLayer;
};

} // namespace smeezr
