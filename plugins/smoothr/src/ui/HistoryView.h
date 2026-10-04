// Smoothr's display: what the limiter did over the last few seconds, scrolling right to left, and the
// level meters beside it.
//   history   the output's peaks (a copper body) and above them what the limiter took off the input
//             (lighter), filled up from the bottom; the gain reduction hanging from the top, the
//             lows' (cinnabar, solid) and the highs' (pale copper, dashed) apart, on the same dB scale;
//             the ceiling as a dashed line
//   meters    the input into the limiter (L, R), the output (L, R) with their peak holds, and the
//             reduction now on the lows and the highs. A click on the meters clears the holds.
#pragma once

#include "../core/Engine.h"

#include "pluginkit/ui/CachedLayer.h"
#include "pluginkit/ui/Widgets.h"

#include "vstgui/lib/cview.h"

#include <functional>
#include <vector>

namespace smoothr {

class HistoryView : public VSTGUI::CView
{
public:
    static constexpr double kRangeDb = 36.0;   // the scale: 0 dB at the top, -36 at the bottom
    static constexpr double kMetersW = 124.0;  // the meters at the right
    static constexpr double kHeaderH = 20.0;
    using MeterSource = std::function<Meters* ()>;

    HistoryView (const VSTGUI::CRect& r, pk::ParamHost* host, MeterSource meters);
    void draw (VSTGUI::CDrawContext* ctx) override;
    void onMouseDownEvent (VSTGUI::MouseDownEvent& e) override;
    void idle ();

    // layout (also used by the host test)
    VSTGUI::CRect plotRect () const;   // the history
    VSTGUI::CRect metersRect () const; // the meters
    double yOfDb (double db) const;

    static VSTGUI::CColor lowColor (uint8_t alpha = 255);
    static VSTGUI::CColor highColor (uint8_t alpha = 255);

private:
    void drawHistory (VSTGUI::CDrawContext* ctx);
    void drawMeters (VSTGUI::CDrawContext* ctx);

    pk::ParamHost* host;
    MeterSource meters;
    // the history as last read (oldest first, one entry a column)
    std::vector<float> inPk, outPk, grLow, grHigh;
    int have = 0;
    uint32_t lastWritten = 0;
    // the bars (dB, falling back 24 dB a second) and their holds
    double barIn[2] = {-120.0, -120.0}, barOut[2] = {-120.0, -120.0}, holdIn = -120.0, holdOut = -120.0;
    double barLow = 0.0, barHigh = 0.0, holdGr = 0.0;
    // The well and its frame, the header's keys, the grid with its scale, the meters' beds and names:
    // a cached layer (pk::CachedLayer) the history, the ceiling and the meters are drawn over.
    pk::CachedLayer baseLayer;
    void paintBase (VSTGUI::CDrawContext* ctx);
    // the meters' geometry: the bars' top and bottom, their width and gap, and where each pair starts
    void meterLayout (double& top, double& bottom, double& w, double& gap, double x[3]) const;
    // what a repaint shows, as a key: idle repaints only when it changes (the history goes on scrolling
    // with the audio stopped, but once it is all quiet, and the bars have fallen, nothing moves)
    uint64_t shownState () const;
    uint64_t shownKey = 0;
};

} // namespace smoothr
