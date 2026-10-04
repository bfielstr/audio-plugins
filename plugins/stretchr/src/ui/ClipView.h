// The clip editor: the stretched waveform on the output timeline with stretch markers and the
// pitch envelope on top.
#pragma once

#include "Clip.h"

#include "pluginkit/ui/CachedLayer.h"

#include "vstgui/lib/cview.h"
#include "vstgui/lib/dragging.h"

#include <functional>
#include <string>

namespace stretchr {

class Controller;
class Session;

class ClipView : public VSTGUI::CView
{
public:
    enum Mode { kStretchMode = 0, kPitchMode };

    ClipView (const VSTGUI::CRect& r, Controller* c);

    void setMode (Mode m);
    Mode mode () const { return editMode; }
    void zoomToFit ();
    void idle ();

    void draw (VSTGUI::CDrawContext* ctx) override;
    void onMouseDownEvent (VSTGUI::MouseDownEvent& e) override;
    void onMouseMoveEvent (VSTGUI::MouseMoveEvent& e) override;
    void onMouseUpEvent (VSTGUI::MouseUpEvent& e) override;
    void onMouseCancelEvent (VSTGUI::MouseCancelEvent& e) override;
    void onMouseExitEvent (VSTGUI::MouseExitEvent& e) override;
    void onMouseWheelEvent (VSTGUI::MouseWheelEvent& e) override;
    VSTGUI::SharedPointer<VSTGUI::IDropTarget> getDropTarget () override;

    std::function<void (const std::string& path)> onFileDropped;
    std::function<void (VSTGUI::CPoint framePos)> onContextMenu;

    // Geometry, exposed for tests and the editor.
    VSTGUI::CRect plotArea () const;
    double timeToX (double outSeconds) const;
    double xToTime (double x) const;
    double semisToY (double semis) const;

private:
    Session* session () const;
    TimeMap timeMap (const Clip& c) const;
    void viewRange (double outLength, double& start, double& len) const;
    int markerAt (const Clip& c, const TimeMap& m, double x) const;
    int pitchPointAt (const Clip& c, const TimeMap& m, VSTGUI::CPoint p) const;
    double yToSemis (double y, bool fine) const;
    void restoreFit ();

    enum class Drag { None, Marker, Point, Pan };

    Controller* controller;
    VSTGUI::SharedPointer<VSTGUI::IDropTarget> dropTarget;
    Mode editMode = kStretchMode;
    double viewStart = 0.0, viewLen = 0.0; // 0 = fit
    Drag drag = Drag::None;
    int dragIndex = -1, hoverIndex = -1;
    double dragStartX = 0.0, dragStartValue = 0.0, lastX = 0.0;
    bool wasPlaying = false, wasRendering = false, fitWhileDragging = false;
    int lastCapture = 0;
    double lastPlayX = -1.0;
    uint64_t lastVersion = 0;
    double lastOutLen = 1.0; // output length of the last drawn clip (for x <-> time)
    // Everything up to the pitch envelope (ruler, tints, waveform, markers, speeds, envelope): a cached
    // layer (pk::CachedLayer), rebuilt when the clip, the view's range, the mode or a hover or drag
    // changes it. The playhead, the render progress and the capture state are drawn over it, and a
    // moving playhead repaints only the strips it left and entered.
    pk::CachedLayer baseLayer;
    void paintClip (VSTGUI::CDrawContext* ctx, const Clip& c);
    VSTGUI::CRect playheadStrip (double x) const;
};

} // namespace stretchr
