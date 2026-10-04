#pragma once

#include "UiKit.h"

#include "Params.h"
#include "SampleData.h"
#include "Slices.h"

#include "pluginkit/ui/CachedLayer.h"

#include "vstgui/lib/dragging.h"

#include <functional>
#include <memory>

namespace smemplr {

class Controller;

class WaveformView : public VSTGUI::CView
{
public:
    WaveformView (const VSTGUI::CRect& r, Controller* c, ParamHost* h);
    ~WaveformView () override;

    void draw (VSTGUI::CDrawContext* ctx) override;
    void onMouseDownEvent (VSTGUI::MouseDownEvent& e) override;
    void onMouseMoveEvent (VSTGUI::MouseMoveEvent& e) override;
    void onMouseUpEvent (VSTGUI::MouseUpEvent& e) override;
    void onMouseCancelEvent (VSTGUI::MouseCancelEvent& e) override;
    void onMouseWheelEvent (VSTGUI::MouseWheelEvent& e) override;
    VSTGUI::SharedPointer<VSTGUI::IDropTarget> getDropTarget () override;

    std::function<void (VSTGUI::CPoint where)> onContextMenu;
    std::function<void (const std::string& path)> onFileDropped;

    // Called from the editor's idle timer; repaints if playheads moved.
    void idle ();
    // The ruler's right end, this wide, is the editor's (its Grid controls sit over it): the ruler's
    // marks and the sample's name stay left of it.
    static constexpr double kToolsWidth = 270.0;
    // The Grid's step in the sample's frames (0: Grid off): its note length (Grid Size) at the sample's
    // tempo while Warp is on, at the host's otherwise (120 BPM without one), counted from the start flag.
    double gridFrames (const SampleData& s) const;
    void resetZoom ()
    {
        viewStart = 0.0;
        viewLen = 1.0;
        setZoomedToSelection (false);
        invalid ();
    }
    // Zoom to selection: the view zooms to the loop (Classic with Loop on) or to the flagged region
    // (start flag .. end flag), with a little room each side; again, back to the whole sample. Any other
    // zoom or scroll leaves it. A double-click on the ruler does the same.
    void toggleZoomToSelection ();
    bool zoomedToSelection () const { return zoomedSel; }
    // The waveform's height: a vertical zoom of the drawing only (1 .. kMaxHeight; Alt + scroll, or the
    // editor's Height slider). View state, not saved.
    static constexpr double kMaxHeight = 16.0;
    double waveHeight () const { return heightZoom; }
    void setWaveHeight (double h);
    // told when the zoom to selection or the height changes (the editor's buttons show them)
    std::function<void ()> onViewChanged;

private:
    // LoopRegion: inside the shaded loop (Loop on), above its bar: a drag moves the loop like the bar, a click
    // auditions as anywhere else in the waveform. HeadStart / HeadEnd / HeadBody: an extra playhead's
    // region (dragHead: which), its start, its end, or the whole region by its bar
    enum class Handle { None, FlagStart, FlagEnd, Start, LoopEnd, LoopBody, Slice, Ruler, Preview, LoopRegion, HeadStart, HeadEnd, HeadBody };

    SamplePtr sample () const;
    VSTGUI::CRect waveArea () const;
    VSTGUI::CRect loopBar (const SampleData& s) const; // the loop brace at the bottom (Classic mode)
    // The extra playheads (Classic, Playheads 2 .. 4): how many there are (1: none shown), playhead k's
    // (1 .. 3) region (start, loop end in frames) and its bar, stacked above the loop's
    int extraHeads () const;
    void headPositions (const SampleData& s, int k, double& hs, double& he) const;
    VSTGUI::CRect headBar (const SampleData& s, int k) const;
    VSTGUI::CRect rulerArea () const;
    double xToPos (double x) const; // -> normalized sample position
    double posToX (double pos) const;
    void computeSlicesForDisplay (SliceList& out, const SampleData& s) const;
    void markerPositions (const SampleData& s, double& fs, double& fe, double& rs, double& re, double& le) const;
    Handle hitTest (const VSTGUI::CPoint& p, int& sliceIndex) const;
    void commitSliceMove (double newPos);
    void addManualSlice (double pos);
    void removeSlice (int index);
    void toggleSliceKind (int index);
    void stopPreview ();
    // The loop's drags with the Grid on (step: gridFrames, > 0): the loop starts on a grid line and is a
    // whole number of steps long (at least one; held inside the flags fs .. fe). keepStart: only the
    // length snaps (the loop's end dragged), the start stays where it is
    void setLoopOnGrid (double rs, double len, double fs, double fe, double step, bool keepStart = false,
                        uint32_t startId = kStart, uint32_t lengthId = kLength);

    void setZoomedToSelection (bool z);

    Controller* controller;
    ParamHost* host;
    double viewStart = 0.0, viewLen = 1.0;
    bool zoomedSel = false;
    double heightZoom = 1.0;
    Handle drag = Handle::None;
    int dragSlice = -1;
    int dragHead = 0; // the extra playhead dragged (1 .. 3)
    bool dragSliceManual = false;
    double dragSliceOrigPos = 0.0, dragSlicePos = 0.0;
    VSTGUI::CPoint dragStartPoint, lastPoint;
    bool moved = false;
    double loopDragDownPos = 0.0, loopDragRs = 0.0, loopDragLen = 0.0;
    int previewNote = -1;
    int hoverSlice = -1;
    float lastHeads[16] {};
    int lastHeadCount = 0;
    uint32_t lastChange = 0;
    VSTGUI::SharedPointer<VSTGUI::IDropTarget> dropTarget;
    // Everything but the playheads (ruler, waveform, markers, loop, slices, overview, name): a cached
    // layer (pk::CachedLayer) rebuilt when the sample, a setting, the zoom or a drag changes it. The
    // playheads are drawn over it, and while only they move, only the strips they left and entered
    // are repainted.
    pk::CachedLayer layer;
    void paint (VSTGUI::CDrawContext* ctx, bool playheads);
    VSTGUI::CRect playheadStrip (float pos) const;
};

// The waveform's Height (WaveformView::setWaveHeight): a small bar slider over the ruler, x1 .. x16 on a
// log scale. Drag sideways (Shift: fine), the mouse wheel steps it, a double-click (or right-click) puts
// it back to x1.
class WaveHeightSlider : public VSTGUI::CView
{
public:
    WaveHeightSlider (const VSTGUI::CRect& r, WaveformView* w) : CView (r), wave (w) {}
    void draw (VSTGUI::CDrawContext* ctx) override;
    void onMouseDownEvent (VSTGUI::MouseDownEvent& e) override;
    void onMouseMoveEvent (VSTGUI::MouseMoveEvent& e) override;
    void onMouseUpEvent (VSTGUI::MouseUpEvent& e) override;
    void onMouseWheelEvent (VSTGUI::MouseWheelEvent& e) override;

private:
    WaveformView* wave;
    bool dragging = false;
    double startX = 0.0, startValue = 0.0; // (startValue: log2 of the height)
};

} // namespace smemplr
