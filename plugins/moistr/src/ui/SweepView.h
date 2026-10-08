// Moistr's SWEEP display: the SWEEP stage's filters against frequency (20 Hz .. 5 kHz, log; -24 .. +24 dB)
// and, at its right, the High Shelf's orbit (its corner across, Low .. High; its gain up, Min .. Max).
//
//   base   the grid, where each bell that is on sweeps (Low .. High, a bar each at the bottom, A lowest; copper
//          and pale in turn) and where the shelf's corner goes (a bar at the top); the orbit's box with its
//          ceiling (Tilt: the gain the shelf may reach at each corner, dashed)
//   live   each bell's curve and the shelf's curve now (thin; a bell switched off fades out with its gain), the
//          whole stage's response now (bold, the sum in dB, with Tone's low-pass), and the shelf's place on its
//          orbit (a dot)
//
// The base changes only with the settings it shows and the view's size: a cached layer (pk::CachedLayer).
// The live part is repainted only when a centre, the shelf's gain or the fades changed (in small steps), so
// with Sweep off, before the engine runs, or once the input has been quiet for half a second, the display asks
// for no repaints.
#pragma once

#include "Engine.h"
#include "Params.h"

#include "pluginkit/ui/CachedLayer.h"
#include "pluginkit/ui/Widgets.h"

#include "vstgui/lib/cview.h"

#include <functional>

namespace moistr {

// What the display shows of the SWEEP stage now (from the engine's meters, else the parameters).
struct SweepSnapshot
{
    double hz[kNumBells + 1] {};   // bells A .. H, the shelf's corner (Hz, 0.1 Hz steps)
    double bellDb[kNumBells] {};   // the bells' gains now (dB, 0.05 dB steps; 0 while off)
    double shelfDb = 0.0;     // the shelf's gain now (dB, 0.05 dB steps)
    double amount = 0.0;      // Sweep faded in (0 .. 1, 0.01 steps)
    double shelfAmount = 0.0; // High Shelf faded in
    bool operator== (const SweepSnapshot& o) const;
    bool operator!= (const SweepSnapshot& o) const { return !(*this == o); }
};

class SweepView : public VSTGUI::CView
{
public:
    using MeterSource = std::function<const Meters* ()>;
    SweepView (const VSTGUI::CRect& r, pk::ParamHost* host, MeterSource meters);
    void draw (VSTGUI::CDrawContext* ctx) override;
    void idle (); // follows the meters and the parameters (repaints only when the snapshot changed)

    // (while the input is quiet (no sound for half a second) the curves hold where they were: `held`, so the
    // display asks for no repaints)
    static SweepSnapshot snapshot (const Meters* m, pk::ParamHost* host, const SweepSnapshot* held = nullptr);
    // the analog responses the display draws (dB at hz): a bell (peaking EQ) and the high shelf with a Q
    static double bellDb (double hz, double centre, double gainDb, double q);
    static double shelfDb (double hz, double corner, double gainDb, double q);
    // true for the parameters the display shows (the editor repaints it when one changes)
    static bool shows (uint32_t id);

private:
    void paintBase (VSTGUI::CDrawContext* ctx);
    VSTGUI::CRect plot () const;  // the curves
    VSTGUI::CRect orbit () const; // the orbit's box
    double xOf (double hz) const;
    double yOf (double db) const;

    pk::ParamHost* host;
    MeterSource meters;
    SweepSnapshot snap;
    pk::CachedLayer baseLayer;
};

} // namespace moistr
