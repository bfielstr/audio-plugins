// Disperse's bands as bars: one bar per band from the lowest (left) to the highest (right), each as tall
// as its gain at the dial now (in dB: silent bands have no bar, full bands reach the top), the next band
// to rise outlined. Drawn from the parameters alone (On, the dial, Bands and Seed): the editor repaints
// it only when one of them changes, never while the audio plays, and a repaint is a few dozen rectangles
// (about 0.1 ms). It is drawn directly, not through a pk::CachedLayer: inside a panel that an arranged
// layout centres by a fraction of a pixel, the cached bitmap came out a few pixels different from the
// direct drawing at 150 % (the draw bench's check), and with no per-tick repaints it would gain nothing.
#pragma once

#include "Params.h"

#include "pluginkit/ui/Widgets.h"

#include "vstgui/lib/cview.h"

namespace ciphr {

class DisperseView : public VSTGUI::CView
{
public:
    DisperseView (const VSTGUI::CRect& r, pk::ParamHost* host);
    void draw (VSTGUI::CDrawContext* ctx) override;
    static constexpr double kFloorDb = -48.0; // a bar's foot

private:
    void paint (VSTGUI::CDrawContext* ctx);
    pk::ParamHost* host;
};

} // namespace ciphr
