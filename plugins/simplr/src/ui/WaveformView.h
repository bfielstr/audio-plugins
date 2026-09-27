#pragma once

#include "UiKit.h"

#include "SampleData.h"
#include "Slices.h"

#include "vstgui/lib/dragging.h"

#include <functional>
#include <memory>

namespace simplr {

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
    void resetZoom ()
    {
        viewStart = 0.0;
        viewLen = 1.0;
        invalid ();
    }

private:
    enum class Handle { None, FlagStart, FlagEnd, Start, LengthEnd, LoopStart, LoopBody, Slice, Ruler, Preview };

    SamplePtr sample () const;
    VSTGUI::CRect waveArea () const;
    VSTGUI::CRect loopBar (const SampleData& s) const; // the loop brace at the bottom (Classic mode)
    VSTGUI::CRect rulerArea () const;
    double xToPos (double x) const; // -> normalized sample position
    double posToX (double pos) const;
    void computeSlicesForDisplay (SliceList& out, const SampleData& s) const;
    void markerPositions (const SampleData& s, double& fs, double& fe, double& rs, double& re, double& ls) const;
    Handle hitTest (const VSTGUI::CPoint& p, int& sliceIndex) const;
    void commitSliceMove (double newPos);
    void addManualSlice (double pos);
    void removeSlice (int index);
    void toggleSliceKind (int index);
    void stopPreview ();

    Controller* controller;
    ParamHost* host;
    double viewStart = 0.0, viewLen = 1.0;
    Handle drag = Handle::None;
    int dragSlice = -1;
    bool dragSliceManual = false;
    double dragSliceOrigPos = 0.0, dragSlicePos = 0.0;
    VSTGUI::CPoint dragStartPoint, lastPoint;
    bool moved = false;
    double loopDragDownPos = 0.0, loopDragRe = 0.0, loopDragLen = 0.0;
    int previewNote = -1;
    int hoverSlice = -1;
    float lastHeads[16] {};
    int lastHeadCount = 0;
    uint32_t lastChange = 0;
    VSTGUI::SharedPointer<VSTGUI::IDropTarget> dropTarget;
};

} // namespace simplr
