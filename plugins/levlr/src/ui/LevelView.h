// The bands in use (Bands: 1 .. 4) on a frequency display, in the style of a multiband's: each band a
// column between its crossovers (numbered at the top), filled from 0 dB to its level, its drive (when on) as a tag
// at the foot; the whole response as a text-coloured line (the bands' filters added up as the engine adds them, so
// the steps between levels show as they sound); the output's spectrum behind. The last band in use
// reaches to the top of the display; the crossovers past it aren't shown.
//   a band, up / down             its Gain (Shift: fine)
//   the line between two bands    that crossover, sideways (it stops 1/6 octave from its neighbours)
//   double-click / right-click    resets a band's gain, or a crossover's frequency
//   M / S at the top of a band    mute / solo it
//   mouse wheel                   a band's gain; on a crossover, the slope
#pragma once

#include "../core/Engine.h"
#include "../core/Params.h"

#include "pluginkit/ui/Widgets.h"

#include "vstgui/lib/cview.h"

#include <cstdint>
#include <functional>
#include <vector>

namespace levlr {

class LevelView : public VSTGUI::CView
{
public:
    static constexpr double kMinHz = 20.0, kMaxHz = 20000.0, kRangeDb = 27.0;
    static constexpr double kSpecFloorDb = -90.0, kSpecTilt = 4.5; // dB/oct, pivoting at 1 kHz
    static constexpr int kFftSize = 4096;
    static constexpr double kChipTop = 4.0, kChipH = 14.0, kChipW = 17.0; // M and S, at the top of each band
    using MeterSource = std::function<const Meters* ()>;

    LevelView (const VSTGUI::CRect& r, pk::ParamHost* host, MeterSource meters);
    static VSTGUI::CColor bandColor (int band, uint8_t alpha = 255); // (also the editor's band headers)
    void draw (VSTGUI::CDrawContext* ctx) override;
    void onMouseDownEvent (VSTGUI::MouseDownEvent& e) override;
    void onMouseMoveEvent (VSTGUI::MouseMoveEvent& e) override;
    void onMouseUpEvent (VSTGUI::MouseUpEvent& e) override;
    void onMouseCancelEvent (VSTGUI::MouseCancelEvent& e) override;
    void onMouseExitEvent (VSTGUI::MouseExitEvent& e) override;
    void onMouseWheelEvent (VSTGUI::MouseWheelEvent& e) override;
    void idle ();

    // layout (also used by the host test)
    double xOfHz (double hz) const;
    double hzOfX (double x) const;
    double yOfDb (double db) const;
    double plotTop () const { return getViewSize ().top + 22.0; }
    double plotBottom () const { return getViewSize ().bottom - 16.0; }
    void crossovers (double out[kCrossovers]) const; // as the engine uses them
    int count () const;                              // the bands in use
    double edgeX (int k) const;                      // crossover k's line
    double bandLeft (int band) const;
    double bandRight (int band) const;
    VSTGUI::CRect chip (int band, bool solo) const;  // its M or S button (empty when the band is too narrow)

private:
    int hitEdge (const VSTGUI::CPoint& p) const;
    int hitBand (const VSTGUI::CPoint& p) const;
    bool hitChip (const VSTGUI::CPoint& p, int& band, bool& solo) const;
    double sampleRate () const;
    bool live () const;
    void analyse ();
    double specAt (double f0, double f1) const;
    void setHover (int edge, int band);

    pk::ParamHost* host;
    MeterSource meters;
    enum class Drag { None, Edge, Band };
    Drag drag = Drag::None;
    int dragIndex = -1;
    VSTGUI::CPoint down;
    double startValue = 0.0;
    double limitLo = kMinXoverHz, limitHi = kMaxXoverHz; // a dragged crossover's room (its neighbours, 1/6 octave off)
    int hoverEdge = -1, hoverBand = -1;

    // the analyser
    std::vector<float> window, buf, spec;
    uint32_t lastWritten = 0, lastBlocks = 0;
    int idleSinceBlock = 1 << 20;
    bool haveSpectrum = false;
};

} // namespace levlr
