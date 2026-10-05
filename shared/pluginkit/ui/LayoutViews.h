// The views of an arranged layout (pluginkit/Layout.h, issue #11): a panel's block, which holds the
// panel's content where the editor built it and shows it where the layout puts it, under a title strip to
// drag it by; and the drop indicator shown while a block is dragged.
#pragma once

#include "vstgui/lib/cview.h"
#include "vstgui/lib/cviewcontainer.h"

#include <string>

namespace pk {

// Where a container's children are, in the coordinates its own rectangle is in, given its top-left there:
// the top-left plus its transform's offset (a block moves its content by its transform). For walks over the
// view tree that add up offsets (the layout check, smemplr's modulation targets).
VSTGUI::CPoint childOrigin (const VSTGUI::CViewContainer* c, VSTGUI::CPoint topLeft);

// A panel's block in an arranged layout. Its children keep the coordinates the editor gave them (the
// build's); the block's transform moves them to its content area, under its strip. Mouse events, drawing,
// repaints and frame conversions (menus pop up under their control) all follow the transform.
//   Drag the strip: the block moves (the editor shows where it would land, and moves it there on release).
//   Drag the right edge (kEdge px): its column gets wider or narrower, within its range.
class PanelBox : public VSTGUI::CViewContainer
{
public:
    struct Handler
    {
        virtual ~Handler () = default;
        // `where`: the mouse in the editor's (the root view's) coordinates; drop: released there
        virtual void blockDragged (PanelBox* box, VSTGUI::CPoint where, bool drop) = 0;
        virtual void blockDragCancelled (PanelBox* box) = 0;
        // the block's column `width` wide (done: released)
        virtual void blockResized (PanelBox* box, double width, bool done) = 0;
    };

    PanelBox (const VSTGUI::CRect& r, std::string id, std::string title, Handler* handler);
    const std::string& panelId () const { return id; }
    const std::string& title () const { return titleText; }
    // The block at `box` (the root's coordinates) with the region of the build that it shows at `content`.
    void place (const VSTGUI::CRect& box, const VSTGUI::CRect& region, const VSTGUI::CRect& content);
    void setLifted (bool on); // being dragged: outlined in cinnabar

    void drawBackgroundRect (VSTGUI::CDrawContext* ctx, const VSTGUI::CRect& update) override;
    void onMouseDownEvent (VSTGUI::MouseDownEvent& e) override;
    void onMouseMoveEvent (VSTGUI::MouseMoveEvent& e) override;
    void onMouseUpEvent (VSTGUI::MouseUpEvent& e) override;
    void onMouseCancelEvent (VSTGUI::MouseCancelEvent& e) override;
    VSTGUI::CPoint& frameToLocal (VSTGUI::CPoint& point) const override;
    VSTGUI::CPoint& localToFrame (VSTGUI::CPoint& point) const override;

private:
    enum class Grab
    {
        None,
        Move,
        Resize,
    };
    Grab zoneAt (VSTGUI::CPoint parentPoint) const; // what a press there would grab
    void setCursorFor (Grab g);

    std::string id, titleText;
    Handler* handler;
    Grab grab = Grab::None;
    bool lifted = false, cursorSet = false, moved = false;
    VSTGUI::CPoint start;
    double startWidth = 0;
};

// The drop indicator: a 2 px cinnabar bar where a dragged block would land (docs/THEME.md, selection).
class DropMark : public VSTGUI::CView
{
public:
    explicit DropMark (const VSTGUI::CRect& r);
    void draw (VSTGUI::CDrawContext* ctx) override;
};

} // namespace pk
