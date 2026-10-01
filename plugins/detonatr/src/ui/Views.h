// Detonatr's own displays:
//   StageStrip       the chain; click a stage to show it, its light to turn it on or off, drag to move it;
//                    the Smacheratr at the end is a fixed box of its own
//   HitView          the input and output levels of the last moments (lined up in time)
//   VocoderView      the Vocoder's bands and their levels
//   SpikeView        the Spike stage's gain change in each band
//   OrbView          the Motion stage's orbs, seen from above, round the listener
//   HistoryView      a gain over the last seconds (a Transient stage's boosts and cuts, a Limiter's reduction)
//   TtmView          a Comp stage's bands: level, target and gain
//   TapeView         the Tape stage's curves and its split
#pragma once

#include "../core/Engine.h"
#include "../core/Params.h"
#include "../core/Tape.h"

#include "pluginkit/ui/Widgets.h"

#include "vstgui/lib/cview.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace detonatr {

using MeterSource = std::function<const Meters* ()>;

class StageStrip : public VSTGUI::CView
{
public:
    static constexpr int kTailBox = kNumStages; // the Smacheratr's box, after the stages
    StageStrip (const VSTGUI::CRect& r, pk::ParamHost* host);
    void draw (VSTGUI::CDrawContext* ctx) override;
    void onMouseDownEvent (VSTGUI::MouseDownEvent& e) override;
    void onMouseMoveEvent (VSTGUI::MouseMoveEvent& e) override;
    void onMouseUpEvent (VSTGUI::MouseUpEvent& e) override;
    void onMouseCancelEvent (VSTGUI::MouseCancelEvent& e) override;

    std::function<void (int page)> onPagePicked; // a stage, or kTailBox
    int selected = kStageVocoder;                // the page shown

    Order order () const;
    VSTGUI::CRect boxRect (int position) const; // 0 .. kNumStages - 1: the stages; kTailBox: the Smacheratr
    VSTGUI::CRect lightRect (int position) const;
    // moves the stage at position `from` to position `to` (writes the Stage parameters)
    void move (int from, int to);

private:
    int positionAt (double x) const;
    pk::ParamHost* host;
    int dragFrom = -1, dragTo = -1;
    double dragX = 0.0, grabDx = 0.0;
    bool dragging = false;
    VSTGUI::CPoint down;
};

class HitView : public VSTGUI::CView
{
public:
    HitView (const VSTGUI::CRect& r, MeterSource meters);
    void draw (VSTGUI::CDrawContext* ctx) override;
    void onMouseDownEvent (VSTGUI::MouseDownEvent& e) override;
    void idle ();
    double seconds = 1.0; // how much time it shows

private:
    MeterSource meters;
    std::vector<float> in, out, colIn, colOut;
    uint32_t lastWritten = 0;
};

class VocoderView : public VSTGUI::CView
{
public:
    VocoderView (const VSTGUI::CRect& r, pk::ParamHost* host, MeterSource meters);
    void draw (VSTGUI::CDrawContext* ctx) override;
    void idle ();

private:
    pk::ParamHost* host;
    MeterSource meters;
    std::vector<float> level, freq;
};

class SpikeView : public VSTGUI::CView
{
public:
    SpikeView (const VSTGUI::CRect& r, pk::ParamHost* host, MeterSource meters);
    void draw (VSTGUI::CDrawContext* ctx) override;
    void idle ();

private:
    pk::ParamHost* host;
    MeterSource meters;
    std::vector<float> gain, held, freq; // held: the peaks, falling slowly
};

class OrbView : public VSTGUI::CView
{
public:
    OrbView (const VSTGUI::CRect& r, pk::ParamHost* host, MeterSource meters);
    void draw (VSTGUI::CDrawContext* ctx) override;
    void idle ();

private:
    pk::ParamHost* host;
    MeterSource meters;
    int orbs = 0;
    float x[Motion::kMaxOrbs] {}, y[Motion::kMaxOrbs] {};
    float distance = 3.0f, radius = 2.0f;
    // each orb's trail (the last positions)
    static constexpr int kTrail = 12;
    float tx[Motion::kMaxOrbs][kTrail] {}, ty[Motion::kMaxOrbs][kTrail] {};
    int trailPos = 0;
    uint32_t seen = 0;
};

class HistoryView : public VSTGUI::CView
{
public:
    // take: the largest boost (>= 0) and cut (<= 0) since it was last called, dB; range: the scale (+- dB)
    using Take = std::function<bool (float& boostDb, float& cutDb)>;
    HistoryView (const VSTGUI::CRect& r, std::string title, double rangeDb, Take take);
    void draw (VSTGUI::CDrawContext* ctx) override;
    void idle ();

private:
    std::string title;
    double range;
    Take take;
    static constexpr int kLen = 240; // about 8 seconds at 30 frames a second
    float boost[kLen] {}, cut[kLen] {};
    int pos = 0;
};

class TtmView : public VSTGUI::CView
{
public:
    TtmView (const VSTGUI::CRect& r, int comp, pk::ParamHost* host, MeterSource meters);
    void draw (VSTGUI::CDrawContext* ctx) override;
    void idle ();

private:
    int comp;
    pk::ParamHost* host;
    MeterSource meters;
    float level[Ttm::kBands] {}, target[Ttm::kBands] {}, gain[Ttm::kBands] {}, makeup = 0.0f;
};

class TapeView : public VSTGUI::CView
{
public:
    TapeView (const VSTGUI::CRect& r, pk::ParamHost* host);
    void draw (VSTGUI::CDrawContext* ctx) override;

private:
    pk::ParamHost* host;
    std::unique_ptr<Tape> tape; // only its curves
};

} // namespace detonatr
