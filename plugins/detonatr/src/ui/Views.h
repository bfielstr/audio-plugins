// Detonatr's own displays:
//   StageStrip      the chain; click a stage to show it, its light to turn it on or off, drag to move it
//   HitView         the input and output levels of the last moments (lined up in time)
//   RecordingSlot   a Tone recording: drop a file on it or click to pick one; shows its waveform
//   TransientCurve  the Transient stage's level after a hit (Spike, Fall, Drop)
#pragma once

#include "../core/Engine.h"
#include "../core/Params.h"

#include "pluginkit/ui/Widgets.h"

#include "vstgui/lib/cview.h"
#include "vstgui/lib/dragging.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace detonatr {

uint32_t stageOnParam (int stage);

class StageStrip : public VSTGUI::CView
{
public:
    StageStrip (const VSTGUI::CRect& r, pk::ParamHost* host);
    void draw (VSTGUI::CDrawContext* ctx) override;
    void onMouseDownEvent (VSTGUI::MouseDownEvent& e) override;
    void onMouseMoveEvent (VSTGUI::MouseMoveEvent& e) override;
    void onMouseUpEvent (VSTGUI::MouseUpEvent& e) override;
    void onMouseCancelEvent (VSTGUI::MouseCancelEvent& e) override;

    std::function<void (int stage)> onStagePicked;
    int selected = kStageTone; // the stage whose controls are shown

    Order order () const;
    VSTGUI::CRect boxRect (int position) const;
    VSTGUI::CRect lightRect (int position) const;
    // moves the stage at position `from` to position `to` (writes the five Stage parameters)
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
    using MeterSource = std::function<const Meters* ()>;
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

class RecordingSlot : public VSTGUI::CView
{
public:
    RecordingSlot (const VSTGUI::CRect& r, int slot);
    void draw (VSTGUI::CDrawContext* ctx) override;
    void onMouseDownEvent (VSTGUI::MouseDownEvent& e) override;
    VSTGUI::SharedPointer<VSTGUI::IDropTarget> getDropTarget () override;
    // what the slot holds (null: empty) and its name, or an error to show
    void setRecording (std::shared_ptr<const Carrier> c, const std::string& name);
    void setError (const std::string& e);

    std::function<void (const std::string& path)> onFileDropped;
    std::function<void ()> onClick;
    bool dropHover = false;

private:
    int slot;
    std::shared_ptr<const Carrier> audio;
    std::string name, error;
    std::vector<float> peaks; // the waveform's outline
    VSTGUI::SharedPointer<VSTGUI::IDropTarget> dropTarget;
};

class TransientCurve : public VSTGUI::CView
{
public:
    TransientCurve (const VSTGUI::CRect& r, pk::ParamHost* host);
    void draw (VSTGUI::CDrawContext* ctx) override;

private:
    pk::ParamHost* host;
};

} // namespace detonatr
