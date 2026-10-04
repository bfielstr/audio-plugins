// The colour EQ curve (applied before the shaper and undone after it) with two handles, and, while
// Gentlr (called Clarity before) is on, its bands drawn the way a multiband compressor shows a band:
// its range shaded, the most it can cut outlined and the cut it is making right now filled in, moving
// with the audio. (Gentlr's code keeps the old name: Clarity.)
//   left handle, up / down        Amt Lo
//   right handle, up / down       Amt Hi
//   right handle, sideways        Freq
//   Gentlr handle, sideways       a Gentlr band's frequency (each band named in its readout)
//   Gentlr handle, up / down      its Range: the handle sits at the most it cuts, drag down for more
//   Sub handle (at 0 dB while its Range is 0): sideways its Freq (20 - 100 Hz), down its Range; no width
//   High handle (at 0 dB while its Range is 0): sideways its Freq (2 - 16 kHz), down its Range; no width
//   Gentlr band edge, sideways    its width
//   Alt (Option) + drag on a Gentlr band, sideways   its width, the band staying centred (right: wider)
//   wheel on a handle (held, or with Shift)   the colour peak's width / Gentlr's width
//   double-click or right-click a handle      reset its parameters
//   a band edge dragged onto a neighbour's (within a few pixels it snaps)   the two are glued at that
//                                 border when the drag ends: a link icon sits on it, near the bottom
//   link icon, click              detaches the two (lit cinnabar: glued), or glues two that touch (copper)
//   a glued border dragged        both edges move: one band widens as the other narrows (for the Sub and
//                                 High bands their Freq is the border); a glued band moved drags its
//                                 neighbour's edge along
// With No Overlap on, a band dragged or widened pushes its neighbours' edges along (BandPush), and the
// bands are drawn where the engine has them (glued borders held, overlaps from automation split, as
// applyGlue and resolveOverlaps do).
// Used by Smacheratr and, through a pk::MappedParamHost, by the Smacheratr in Smemplr's rack; the
// sample rate and the levels come from functions so it does not depend on a controller.
#pragma once

#include "../core/ClarityBand.h"
#include "../core/Engine.h"
#include "../core/Params.h"
#include "BandPush.h"

#include "pluginkit/ui/CachedLayer.h"
#include "pluginkit/ui/Widgets.h"

#include "vstgui/lib/cview.h"

#include <functional>

namespace smacheratr {

class ColorView : public VSTGUI::CView
{
public:
    static constexpr double kMinHz = 20.0, kMaxHz = 20000.0, kMaxDb = 24.0;
    using RateSource = std::function<double ()>;
    using MeterSource = std::function<const Meters* ()>;

    ColorView (const VSTGUI::CRect& r, pk::ParamHost* host, RateSource rate, MeterSource meters);
    void draw (VSTGUI::CDrawContext* ctx) override;
    void onMouseDownEvent (VSTGUI::MouseDownEvent& e) override;
    void onMouseMoveEvent (VSTGUI::MouseMoveEvent& e) override;
    void onMouseUpEvent (VSTGUI::MouseUpEvent& e) override;
    void onMouseExitEvent (VSTGUI::MouseExitEvent& e) override;
    void onMouseWheelEvent (VSTGUI::MouseWheelEvent& e) override;
    void idle (); // follows Gentlr's cut
    std::function<void (int)> onBandPicked; // a Gentlr band's handle or edge was grabbed
    // Gentlr band k's colour: pale copper for every band (they are told apart by place and name)
    static VSTGUI::CColor bandColor (int band, uint8_t alpha = 255);

    double xOfHz (double hz) const;
    double yOfDb (double db) const;
    VSTGUI::CPoint loHandle () const;
    VSTGUI::CPoint hiHandle () const;
    VSTGUI::CPoint clarityHandle (int band) const; // at a Clarity band's centre, as deep as its Range
    double clarityEdgeX (int band, bool high) const; // its edges
    // the link icons (Glue.h): where each sits (its border, near the bottom), and the one at p (-1: none)
    int links (GlueBorder out[kGentlrBands], VSTGUI::CPoint at[kGentlrBands]) const;
    int linkAt (const VSTGUI::CPoint& p, GlueBorder* b = nullptr) const;

private:
    enum class Drag { None, Lo, Hi, Clarity, ClarityLow, ClarityHigh, ClarityWidth };
    Drag hit (const VSTGUI::CPoint& p, int* band = nullptr) const; // band: which Clarity band was hit
    int bandUnder (const VSTGUI::CPoint& p) const;                 // the working band whose region p is in (-1: none)
    double sampleRate () const;
    double snapOctaves () const;        // how near (6 px, in octaves) a dragged edge snaps onto a neighbour's
    bool clarityOn (int band) const;    // the band works (Clarity on, Range above 0; Sub and High the same)
    bool clarityShown (int band) const; // its handle is there to grab (Clarity on: every band, at 0 dB while its Range is 0)
    GentlrLayout layoutNow () const;     // where the bands sit (with No Overlap: kept apart, as the engine has them)
    ClarityBand bandOf (int band) const; // its shape (Sub: subBand, High: highBand, no width)

    pk::ParamHost* host;
    RateSource rate;
    MeterSource meters;
    Drag drag = Drag::None;
    VSTGUI::CPoint down;
    int dragBand = 0; // the Clarity band being dragged
    double startLo = 0.0, startHi = 0.0, startFreq = 0.0, startClarity = 0.0, startRange = 0.0, startWidth = 0.0;
    bool movedH = false, movedV = false;
    BandPush push; // No Overlap and glue: the neighbours a band drag pushes or drags along, the edge it snaps
    int hoverLink = -1; // the link icon under the mouse (its border's index), lit brighter
    float shownCut[kGentlrBands] = {0.0f, 0.0f, 0.0f, 0.0f}; // the Clarity bands' cuts (dB, 0 or less), eased
    // The well, the grid and its scale, Gentlr's band regions and the colour response: everything drawn
    // before Gentlr's live cuts, kept in a cached layer (pk::CachedLayer) until a setting or a drag
    // changes it. The cuts, the handles and the labels are drawn over it on every repaint.
    pk::CachedLayer baseLayer;
    void paintBase (VSTGUI::CDrawContext* ctx, const bool clarity[], const ClarityBand bands[]);
    void paintLabels (VSTGUI::CDrawContext* ctx); // the title and the settings, top left
};

} // namespace smacheratr
