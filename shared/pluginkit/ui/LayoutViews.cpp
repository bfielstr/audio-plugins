#include "pluginkit/ui/LayoutViews.h"

#include "pluginkit/Layout.h"
#include "pluginkit/ui/Theme.h"
#include "pluginkit/ui/Widgets.h"

#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cframe.h"
#include "vstgui/lib/events.h"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace pk {

using namespace VSTGUI;

CPoint childOrigin (const CViewContainer* c, CPoint topLeft)
{
    const CGraphicsTransform& t = c->getTransform ();
    return CPoint (topLeft.x + t.dx, topLeft.y + t.dy);
}

PanelBox::PanelBox (const CRect& r, std::string i, std::string t, Handler* h)
    : CViewContainer (r), id (std::move (i)), titleText (std::move (t)), handler (h)
{
    setTransparency (true); // (the root's ground shows; the block draws only its strip)
    for (auto& ch : titleText)
        ch = (char)std::toupper ((unsigned char)ch);
}

void PanelBox::place (const CRect& box, const CRect& region, const CRect& content)
{
    if (getViewSize () != box)
    {
        invalid ();
        setViewSize (box);
        setMouseableArea (box);
    }
    // the build's region at the content area: children at (x, y) of the build draw at
    // (content.left - box.left + x - region.left, ...) in the block
    setTransform (CGraphicsTransform ().translate (content.left - box.left - region.left, content.top - box.top - region.top));
    invalid ();
}

void PanelBox::setLifted (bool on)
{
    if (lifted == on)
        return;
    lifted = on;
    invalid ();
}

void PanelBox::drawBackgroundRect (CDrawContext* ctx, const CRect& update)
{
    const CRect r (0, 0, getViewSize ().getWidth (), getViewSize ().getHeight ());
    // the title strip, as a panel's title: a grip (two columns of dots, copper) and the name in small
    // uppercase pale copper on plain space; held, the block is outlined in cinnabar (THEME.md: selection)
    if (update.rectOverlap (CRect (0, 0, r.right, layout::kStrip)))
    {
        ctx->setFillColor (lifted ? theme::kEnergyLive : theme::kCopper);
        for (int col = 0; col < 2; ++col)
            for (int row = 0; row < 3; ++row)
                ctx->drawRect (CRect (2 + col * 3, 4 + row * 3, 4 + col * 3, 6 + row * 3), kDrawFilled);
        if (!titleText.empty ())
        {
            ctx->setFont (theme::font (9.5, true));
            ctx->setFontColor (theme::kCopperPale);
            ctx->drawString (titleText.c_str (), CRect (12, 0, r.right - layout::kEdge, layout::kStrip - 1), kLeftText, true);
        }
    }
    if (lifted)
        draw::outline (ctx, r, theme::kEnergyLive, 0);
}

PanelBox::Grab PanelBox::zoneAt (CPoint p) const
{
    const CRect r = getViewSize ();
    const double x = p.x - r.left, y = p.y - r.top;
    if (x < 0 || y < 0 || x >= r.getWidth () || y >= r.getHeight ())
        return Grab::None;
    if (x >= r.getWidth () - layout::kEdge)
        return Grab::Resize;
    if (y < layout::kStrip)
        return Grab::Move;
    return Grab::None;
}

void PanelBox::setCursorFor (Grab g)
{
    auto* f = getFrame ();
    if (!f)
        return;
    if (g == Grab::Resize)
        f->setCursor (kCursorHSize);
    else if (g == Grab::Move)
        f->setCursor (kCursorSizeAll);
    else if (cursorSet)
        f->setCursor (kCursorDefault);
    cursorSet = g != Grab::None;
}

void PanelBox::onMouseDownEvent (MouseDownEvent& e)
{
    // (a press on the content goes to it; the strip and the edge are the block's own, but a control
    // placed over the edge keeps it)
    const Grab g = e.buttonState.isLeft () ? zoneAt (e.mousePosition) : Grab::None;
    if (g == Grab::None)
    {
        CViewContainer::onMouseDownEvent (e);
        return;
    }
    if (g == Grab::Resize)
    {
        // a control there (inside the content) keeps its clicks
        CViewContainer::onMouseDownEvent (e);
        if (e.consumed)
            return;
    }
    grab = g;
    moved = false;
    start = e.mousePosition;
    startWidth = getViewSize ().getWidth ();
    e.consumed = true;
}

void PanelBox::onMouseMoveEvent (MouseMoveEvent& e)
{
    if (grab == Grab::None)
    {
        setCursorFor (zoneAt (e.mousePosition));
        CViewContainer::onMouseMoveEvent (e);
        return;
    }
    e.consumed = true;
    moved = moved || std::fabs (e.mousePosition.x - start.x) + std::fabs (e.mousePosition.y - start.y) > 3;
    if (!moved)
        return;
    if (grab == Grab::Move)
    {
        setLifted (true);
        handler->blockDragged (this, e.mousePosition, false);
    }
    else
        handler->blockResized (this, std::round (startWidth + e.mousePosition.x - start.x), false);
}

void PanelBox::onMouseUpEvent (MouseUpEvent& e)
{
    if (grab == Grab::None)
    {
        CViewContainer::onMouseUpEvent (e);
        return;
    }
    e.consumed = true;
    const Grab g = grab;
    grab = Grab::None;
    setLifted (false);
    if (!moved)
    {
        if (g == Grab::Move)
            handler->blockDragCancelled (this);
        return;
    }
    if (g == Grab::Move)
        handler->blockDragged (this, e.mousePosition, true);
    else
        handler->blockResized (this, std::round (startWidth + e.mousePosition.x - start.x), true);
}

void PanelBox::onMouseCancelEvent (MouseCancelEvent& e)
{
    if (grab == Grab::None)
    {
        CViewContainer::onMouseCancelEvent (e);
        return;
    }
    e.consumed = true;
    const Grab g = grab;
    grab = Grab::None;
    setLifted (false);
    if (g == Grab::Move)
        handler->blockDragCancelled (this);
    else if (moved)
        handler->blockResized (this, getViewSize ().getWidth (), true);
}

// CViewContainer's conversions add up offsets only; a block's children are moved by its transform too
CPoint& PanelBox::frameToLocal (CPoint& point) const
{
    if (auto* parent = getParentView ())
        parent->frameToLocal (point);
    point.offset (-getViewSize ().left, -getViewSize ().top);
    getTransform ().inverse ().transform (point);
    return point;
}

CPoint& PanelBox::localToFrame (CPoint& point) const
{
    getTransform ().transform (point);
    point.offset (getViewSize ().left, getViewSize ().top);
    if (auto* parent = getParentView ())
        return parent->localToFrame (point);
    return point;
}

DropMark::DropMark (const CRect& r) : CView (r) { setMouseEnabled (false); }

void DropMark::draw (CDrawContext* ctx)
{
    ctx->setFillColor (theme::kEnergyLive);
    ctx->drawRect (getViewSize (), kDrawFilled);
}

} // namespace pk
