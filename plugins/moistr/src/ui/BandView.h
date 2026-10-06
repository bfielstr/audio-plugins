// Moistr's display: the three bands' filter responses against frequency (20 Hz .. 20 kHz, -36 .. +18 dB).
//
//   base   the grid, each band's response at its set frequency, resonance and level (dim copper), and the
//          hollow between Mid and High shaded, with its width in octaves at the top left
//   live   while sound plays, each band's response where the movement has it now (the first pass solid,
//          the second dashed), a cinnabar mark at each band's frequency, and the Glue's gain reduction
//
// The base changes only with the band settings, Gap, Slope and the view's size: it is a cached layer
// (pk::CachedLayer). The live curves are drawn over it, and only repainted when a band moved: with the
// movement at 0, or once the input has been quiet for half a second, the display asks for no repaints.
#pragma once

#include "Engine.h"
#include "Params.h"

#include "pluginkit/ui/CachedLayer.h"
#include "pluginkit/ui/Widgets.h"

#include "vstgui/lib/cview.h"

#include <functional>

namespace moistr {

class BandView : public VSTGUI::CView
{
public:
    using MeterSource = std::function<const Meters* ()>;
    BandView (const VSTGUI::CRect& r, pk::ParamHost* host, MeterSource meters);
    void draw (VSTGUI::CDrawContext* ctx) override;
    void idle (); // follows the meters

    // a band's response (dB, with its level) at f for a filter at fc (the analog shape the filter follows)
    static double responseDb (int band, double f, double fc, double res, double levelDb, bool steep);

private:
    void paintBase (VSTGUI::CDrawContext* ctx);
    VSTGUI::CRect plot () const;
    double xOf (double hz) const;
    double yOf (double db) const;
    void curve (VSTGUI::CDrawContext* ctx, int band, double fc, double levelDb, bool dashed);

    pk::ParamHost* host;
    MeterSource meters;
    bool active = false;
    int passes = 1;
    float freq[kMaxPasses][kBands] {}, level[kMaxPasses][kBands] {};
    float glueDb = 0.0f;
    uint32_t seen = 0;
    pk::CachedLayer baseLayer;
};

} // namespace moistr
