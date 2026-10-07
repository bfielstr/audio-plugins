// Moistr's display: the multiband split against frequency (20 Hz .. 20 kHz, the bands' levels from
// -48 dB (off) at the bottom to +12 dB at the top).
//
//   base   the grid, the Low band (locked: a solid fill up to its Level, a lock and its crossover in Hz; with
//          Low Push / Low Dip no lock: its Level and how far it comes forward and dips back, dashed),
//          the upper crossovers where they are set (dim, dashed), each moving band's Level (a dim copper
//          line) and how far it falls (Level - Depth, dashed), the band names and, with the shifter on,
//          how far the upper bands are shifted (over each of them; the Low band is never shifted)
//   Liquid (on): where its resonance may go (Liquid Low .. Liquid High, a bar at the top) in the base, and
//          live where its two peaks are now (markers over the upper bands)
//   live   the Low band when it moves (filled up to its level now), each moving band (Mid, High and, with 4 Bands, Air) as a region from its crossover to the next,
//          filled up to its level now (it rises and falls with the movement), the crossovers where they
//          are now, and the Glue's gain reduction
//
// The base changes only with the settings it shows, the Low crossover and the view's size: it is a
// cached layer (pk::CachedLayer). The live regions are drawn over it and only repainted when a band's
// level or a crossover changed: with the movement at 0, or once the input has been quiet for half a
// second, the display asks for no repaints.
//
// Everything the display reads from the engine goes through one adapter, BandSnapshot / snapshot ()
// (BandView.cpp), so the engine's fields are wired in one place.
#pragma once

#include "Engine.h"
#include "Params.h"

#include "pluginkit/ui/CachedLayer.h"
#include "pluginkit/ui/Widgets.h"

#include "vstgui/lib/cview.h"

#include <functional>

namespace moistr {

// What the display shows of the split now: filled by BandView::snapshot () from the engine's meters (or,
// where the engine does not publish a value yet, estimated from the parameters).
struct BandSnapshot
{
    static constexpr int kMax = 4;  // Low, Mid, High, Air
    int bands = 3;                  // 3 or 4
    double xover[3] {};             // Hz: Low | Mid, Mid | High, High | Air (4 bands)
    double gainDb[kMax] {};         // each band's level now (dB, with the movement; kLevelOffDb: off)
    bool lowLocked = true;          // the Low band's level never moves (Low Push and Low Dip at 0; Low X never does)
    bool lowKnown = false;          // xover[0] is the engine's (Seed's) pick, not an estimate
    bool active = false;            // input heard in the last half second (the bands are moving)
    double glueDb = 0.0;            // the Glue's gain reduction (dB, 0.1 dB steps)
    double liquidHz = 0.0, liquidF2Hz = 0.0; // Liquid's peaks now (Hz, whole; 0: off or not known)
    bool operator== (const BandSnapshot& o) const;
    bool operator!= (const BandSnapshot& o) const { return !(*this == o); }
};

class BandView : public VSTGUI::CView
{
public:
    using MeterSource = std::function<const Meters* ()>;
    BandView (const VSTGUI::CRect& r, pk::ParamHost* host, MeterSource meters);
    void draw (VSTGUI::CDrawContext* ctx) override;
    void idle (); // follows the meters and the parameters (repaints only when the snapshot changed)

    // The adapter: the split now, from the engine's meters (null: the parameters alone).
    static BandSnapshot snapshot (const Meters* m, pk::ParamHost* host);
    // The Low crossover estimated from Seed while the engine does not publish its pick (Hz, in
    // kLowXoverMin .. kLowXoverMax).
    static double estimatedLowXover (int seed);
    // true for the parameters the display shows (the editor repaints it when one changes)
    static bool shows (uint32_t id);

private:
    void paintBase (VSTGUI::CDrawContext* ctx);
    VSTGUI::CRect plot () const;
    double xOf (double hz) const;
    double yOf (double db) const;

    pk::ParamHost* host;
    MeterSource meters;
    BandSnapshot snap;
    pk::CachedLayer baseLayer;
};

} // namespace moistr
