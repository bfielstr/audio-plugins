// Gentlr's two bands, its Sub band and its High band on a frequency display, the way a multiband compressor shows its
// bands (and the way Smacheratr's colour display shows its Gentlr): each band's region shaded between
// its edges, the most it can cut outlined (dashed), the cut it is making right now lit (cinnabar) from the
// 0 dB line and moving with the audio, a handle at its centre as deep as its Range (Sub's and High's at
// their Freq, where they start to taper). The whole response in the text colour (every band at its cut now, with its phase:
// the curve is what the sound gets). Behind: the input's spectrum (a line) and the output's (filled),
// so the cut shows between them. One colour for all bands: each is named in its readout at the top.
//   handle, sideways              the band's Frequency (Sub: 20 - 100 Hz, High: 2 - 16 kHz)
//   handle, up / down             its Range (the handle sits at the most it cuts: drag down for more)
//   band edge, sideways           its Width (the band stays centred; Sub and High have no width)
//   Alt (Option) + drag on a band, sideways   its Width (right: wider)
//   wheel on a handle (held, or with Shift)   its Width
//   double-click / right-click a handle       resets the band's Frequency, Width and Range
//   the band's readout at the top             switches the band (Sub, High) on or off
//   a band edge (or the Sub or High handle) dragged onto a neighbour's edge (within a few pixels it
//   snaps)                        the two are glued at that border when the drag ends: a link icon sits
//                                 on it, at the bottom of the display (with Menu > Defaults > Glue Bands
//                                 on Touch checked: smacheratr::touchSnapOctaves)
//   link icon, click              detaches the two (lit cinnabar: glued), or glues two that touch (copper)
//   a glued border dragged        both edges move: one band widens as the other narrows (for the Sub and
//                                 High bands their Freq is the border); a glued band moved drags its
//                                 neighbour's edge along
// With No Overlap on, a band dragged or widened pushes its neighbours' edges along (smacheratr::BandPush),
// and the bands are drawn where the engine has them (glued borders held, overlaps from automation split).
#pragma once

#include "../core/Engine.h"
#include "../core/Params.h"

#include "smacheratr/src/core/ClarityBand.h"
#include "smacheratr/src/ui/BandPush.h"

#include "pluginkit/ui/CachedLayer.h"
#include "pluginkit/ui/Widgets.h"

#include "vstgui/lib/cview.h"

#include <cstdint>
#include <functional>
#include <vector>

namespace gentlr {

class GentlrView : public VSTGUI::CView
{
public:
    static constexpr double kMinHz = 20.0, kMaxHz = 20000.0;
    static constexpr double kTopDb = 6.0, kBottomDb = -27.0; // the response's scale
    static constexpr double kSpecFloorDb = -90.0, kSpecTilt = 4.5; // dB/oct, pivoting at 1 kHz
    static constexpr int kFftSize = 4096;
    static constexpr double kPillW = 150.0, kPillH = 16.0, kPillTop = 4.0;
    using MeterSource = std::function<const Meters* ()>;

    GentlrView (const VSTGUI::CRect& r, pk::ParamHost* host, MeterSource meters);
    static VSTGUI::CColor bandColor (int band, uint8_t alpha = 255); // Smacheratr's Gentlr band colour (one for all bands)
    static const smacheratr::GentlrBandParams& bandParams ();        // where Gentlr's bands are among its parameters (No Overlap)
    void draw (VSTGUI::CDrawContext* ctx) override;
    void onMouseDownEvent (VSTGUI::MouseDownEvent& e) override;
    void onMouseMoveEvent (VSTGUI::MouseMoveEvent& e) override;
    void onMouseUpEvent (VSTGUI::MouseUpEvent& e) override;
    void onMouseCancelEvent (VSTGUI::MouseCancelEvent& e) override;
    void onMouseExitEvent (VSTGUI::MouseExitEvent& e) override;
    void onMouseWheelEvent (VSTGUI::MouseWheelEvent& e) override;
    void idle ();
    // the band selected in the editor (its handle lit, its region a little brighter; -1: none)
    void setSelectedBand (int band);
    int selectedBand () const { return selected; }

    // layout (also used by the host test)
    double xOfHz (double hz) const;
    double hzOfX (double x) const;
    double yOfDb (double db) const;
    double plotTop () const { return getViewSize ().top + 8.0; }
    double plotBottom () const { return getViewSize ().bottom - 16.0; }
    VSTGUI::CPoint handle (int band) const; // at its centre (Sub, High: its Freq), as deep as its Range
    double edgeX (int band, bool high) const;
    VSTGUI::CRect pill (int band) const;     // its name and cut, at the top (a row lower where they would overlap)
    double pillsBottom () const;             // under the lowest readout (the edges start there)
    // the glue link icons: where each sits (on its border, at the bottom), and the one at p (-1: none)
    int links (smacheratr::GlueBorder out[kAllBands], VSTGUI::CPoint at[kAllBands]) const;
    int linkAt (const VSTGUI::CPoint& p, smacheratr::GlueBorder* b = nullptr) const;

private:
    enum class Drag { None, Handle, Low, High, Width };
    Drag hit (const VSTGUI::CPoint& p, int* band) const;
    int bandUnder (const VSTGUI::CPoint& p) const; // the working band whose region p is in (-1: none)
    bool works (int band) const;
    double sampleRate () const;
    smacheratr::GentlrLayout layoutNow () const; // where the bands sit (with No Overlap: kept apart, as the engine has them)
    smacheratr::ClarityBand bandNow (int band) const;
    bool live () const;
    bool analyse (const std::vector<float>& in, std::vector<float>& spec); // true: it moved where it is drawn (above kSpecFloorDb)
    double specAt (const std::vector<float>& spec, double f0, double f1) const;
    void setHover (int band);

    pk::ParamHost* host;
    MeterSource meters;
    Drag drag = Drag::None;
    int dragBand = 0, hoverBand = -1, selected = -1;
    VSTGUI::CPoint down;
    double startFreq = 0.0, startRange = 0.0, startWidth = 0.0;
    smacheratr::BandPush push; // No Overlap and glue: the neighbours a drag pushes or drags along, the edge it snaps
    int hoverLink = -1;        // the link icon under the mouse, lit brighter
    float shownCut[kAllBands] = {0.0f, 0.0f, 0.0f, 0.0f}; // the bands' cuts (dB, 0 or less), eased
    // The well, the bands' regions and the grid with its scale, under the spectra: a cached layer
    // (pk::CachedLayer), rebuilt when a setting, the hover or a drag changes it. Each band's most cut
    // (its dashed curve) is kept as points until a setting, the rate or the size changes: working the
    // band's response out at every point is the costly part.
    pk::CachedLayer baseLayer;
    void paintBase (VSTGUI::CDrawContext* ctx, const bool on[], const smacheratr::ClarityBand bands[]);
    std::vector<VSTGUI::CPoint> rangePoints[kAllBands];
    uint64_t rangeKey = 0;

    // the analyser: input and output
    std::vector<float> window, bufIn, bufOut, specIn, specOut;
    uint32_t lastWritten = 0, lastBlocks = 0;
    int idleSinceBlock = 1 << 20;
    bool haveSpectrum = false;
};

} // namespace gentlr
