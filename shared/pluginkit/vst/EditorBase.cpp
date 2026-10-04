#include "EditorBase.h"

#include "pluginkit/vst/Clipboard.h"
#include "pluginkit/ui/LayoutCheck.h"
#include "pluginkit/ui/Theme.h"

#include "vstgui/lib/cframe.h"
#include "vstgui/lib/cvstguitimer.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace pk {

using namespace VSTGUI;
using namespace Steinberg;

EditorBase::EditorBase (ControllerBase* c, double w, double h)
: VSTGUIEditor (c), controller (c), fullContentHeight (h), contentHeight (h), baseWidth (w), baseHeight (h + kInfoHeight)
{
    scale = std::clamp (c->uiScale, kMinZoom, kMaxZoom);
    ViewRect vr (0, 0, (int32)std::lround (baseWidth * scale), (int32)std::lround (baseHeight * scale));
    setRect (vr);
    setIdleRate (33);
    hoverWatch.editor = this;
}

bool PLUGIN_API EditorBase::open (void* parent, const PlatformType& platformType)
{
    if (frame)
        return false;
    // built with every section open; one that folds while building sets the height again (setContentHeight)
    contentHeight = fullContentHeight;
    baseHeight = contentHeight + kInfoHeight;
    frame = new CFrame (CRect (0, 0, baseWidth, baseHeight), this);
    // the margins left when the window's shape differs from the UI's (layoutFrame) show the ground
    frame->setBackgroundColor (theme::kGround);
    byParam.clear ();
    buildUI (frame);
    addInfoStrip ();
    writeLayoutReport ();
    // every tooltip set while building, wrapped to lines (views made later are wrapped when the mouse
    // first enters them: helpFor, from HoverWatch)
    prepareTooltips (frame);
    frame->registerMouseObserver (&hoverWatch);
    frame->enableTooltips (controller->uiShowTips, 600);
    frame->open (parent, platformType);
    frame->setZoom (scale);
    // the host may have given the window another shape than the UI's (a size it kept, or one it chose)
    layoutFrame (rect.getWidth (), rect.getHeight ());
    // a section built folded (setContentHeight while building): the window at the content's height, if
    // the host lets it be resized (otherwise the content keeps its full height)
    if (contentHeight != fullContentHeight)
    {
        const double h = contentHeight;
        contentHeight = fullContentHeight;
        baseHeight = contentHeight + kInfoHeight;
        if (!setContentHeight (h))
            placeInfoStrip (); // (the strip back at the full content's end)
    }
    return true;
}

bool EditorBase::setContentHeight (double h)
{
    h = std::clamp (h, 40.0, fullContentHeight);
    if (h == contentHeight)
        return true;
    if (!frame || !frame->isAttached ())
    {
        // building: only recorded (addInfoStrip places the strip there, open () asks the host)
        contentHeight = h;
        baseHeight = h + kInfoHeight;
        return true;
    }
    const double oldContent = contentHeight, oldBase = baseHeight;
    contentHeight = h;
    baseHeight = h + kInfoHeight;
    // the window at the zoom it shows now, only its height changed (the host calls onSize, which lays
    // the frame out with the new base height)
    ViewRect vr (0, 0, (int32)std::lround (baseWidth * scale), (int32)std::lround (baseHeight * scale));
    const bool ok = plugFrame && plugFrame->resizeView (this, &vr) == kResultTrue;
    if (!ok)
    {
        // kept as it was: the content keeps its height, the folded section's space stays empty
        contentHeight = oldContent;
        baseHeight = oldBase;
        return false;
    }
    placeInfoStrip ();
    layoutFrame (rect.getWidth (), rect.getHeight ());
    return true;
}

void EditorBase::placeInfoStrip ()
{
    if (!frame)
        return;
    CViewContainer* root = frame->getNbViews () > 0 ? frame->getView (0)->asViewContainer () : nullptr;
    if (root)
    {
        CRect rr = root->getViewSize ();
        rr.bottom = rr.top + baseHeight;
        root->setViewSize (rr);
        root->setMouseableArea (rr);
    }
    if (info)
    {
        const CRect ir (8, contentHeight, baseWidth - 8, baseHeight - 8);
        info->setViewSize (ir);
        info->setMouseableArea (ir);
    }
    frame->invalid ();
}

VSTGUI::CFrame* EditorBase::openDetached (double zoom, const RootWrapper& wrap)
{
    if (frame)
        return nullptr;
    // as open () builds it, without the window, the tooltips and the hover help
    frame = new CFrame (CRect (0, 0, baseWidth, baseHeight), this);
    frame->setBackgroundColor (theme::kGround);
    byParam.clear ();
    buildUI (frame);
    addInfoStrip ();
    writeLayoutReport (); // (PK_LAYOUT_REPORT: the draw benchmark checks the layout on Linux this way)
    if (wrap && frame->getNbViews () > 0)
    {
        CView* root = frame->getView (0);
        frame->removeView (root, false); // (the reference the frame adopted goes over to the holder)
        CViewContainer* holder = wrap (frame->getViewSize ());
        holder->addView (root);
        frame->addView (holder);
    }
    frame->setZoom (std::clamp (zoom, kMinZoom, kMaxZoom));
    frame->attached (frame); // (the views find their frame and parents, as in a window)
    return frame;
}

void PLUGIN_API EditorBase::close ()
{
    onClose ();
    byParam.clear ();
    if (frame)
    {
        frame->unregisterMouseObserver (&hoverWatch);
        hoverWatch.hovered = nullptr;
        info = nullptr;
        frame->forget ();
        frame = nullptr;
    }
}

void EditorBase::addInfoStrip ()
{
    // The plug-in built its content into one root container of baseWidth x contentHeight (its
    // Background, which draws the window). The root grows by the strip, so the window's frame and
    // ground run around the info box too, and nothing the plug-in placed moves.
    CViewContainer* root = frame;
    if (frame->getNbViews () > 0)
        if (auto* c = frame->getView (0)->asViewContainer ())
            root = c;
    if (root != frame)
    {
        CRect rr = root->getViewSize ();
        rr.bottom = rr.top + baseHeight;
        root->setViewSize (rr);
        root->setMouseableArea (rr);
    }
    // the box: 8 px in from the window's sides and bottom, starting where the content ends (the
    // content's last panels end 8 px above that)
    info = new InfoBox (CRect (8, contentHeight, baseWidth - 8, baseHeight - 8));
    root->addView (info);
}

void EditorBase::writeLayoutReport ()
{
    // A debug aid: with PK_LAYOUT_REPORT set to a file, every editor opened appends the controls that
    // overlap or touch and the texts that spill out of their boxes (pk::layoutReport), under a line
    // naming the editor and the count. The macOS host tests set it; nothing is written otherwise.
    const char* path = std::getenv ("PK_LAYOUT_REPORT");
    if (!path || !*path)
        return;
    if (FILE* f = std::fopen (path, "a"))
    {
        const auto lines = layoutReport (frame);
        std::fprintf (f, "== %s: %zu\n", typeName (typeid (*this)).c_str (), lines.size ());
        for (const auto& l : lines)
            std::fprintf (f, "%s\n", l.c_str ());
        std::fclose (f);
    }
}

void EditorBase::layoutFrame (double w, double h)
{
    if (!frame || w <= 0 || h <= 0)
        return;
    // Keep shape, pad: the UI is zoomed as large as fits the window both ways and centred in it, the
    // rest of the window is the frame's ground. setZoom gives the frame its scale (and tells the
    // platform layer, which renders text and paths for it); then the frame takes the window's size,
    // and its transform both scales and offsets the content to the centre. VSTGUI maps mouse
    // positions, invalid rectangles, drawing and pop-up menus through the frame's whole transform, so
    // the offset is followed everywhere, as the zoom always was.
    const double z = zoomFor (w, h, baseWidth, baseHeight);
    frame->setZoom (z);
    frame->setSize (w, h);
    const double dx = std::round ((w - baseWidth * z) / 2), dy = std::round ((h - baseHeight * z) / 2);
    frame->setTransform (CGraphicsTransform (z, 0, 0, z, std::max (0.0, dx), std::max (0.0, dy)));
    frame->invalid ();
}

tresult PLUGIN_API EditorBase::checkSizeConstraint (ViewRect* rect)
{
    if (!rect)
        return kInvalidArgument;
    // Any shape: the window may be resized freely, the UI keeps its own shape inside it (layoutFrame).
    // Only the size is bounded, each side between the smallest and the largest zoom's.
    const int32 w = std::clamp (rect->getWidth (), (int32)std::lround (baseWidth * kMinZoom), (int32)std::lround (baseWidth * kMaxZoom));
    const int32 h = std::clamp (rect->getHeight (), (int32)std::lround (baseHeight * kMinZoom), (int32)std::lround (baseHeight * kMaxZoom));
    rect->right = rect->left + w;
    rect->bottom = rect->top + h;
    return kResultTrue;
}

tresult PLUGIN_API EditorBase::onSize (ViewRect* newSize)
{
    if (!newSize)
        return kInvalidArgument;
    scale = zoomFor (newSize->getWidth (), newSize->getHeight (), baseWidth, baseHeight);
    controller->uiScale = scale; // (what is kept: the next window opens at this zoom, in the UI's shape)
    if (frame)
        layoutFrame (newSize->getWidth (), newSize->getHeight ());
    return VSTGUIEditor::onSize (newSize);
}

void EditorBase::resizeTo (double s)
{
    s = std::clamp (s, kMinZoom, kMaxZoom);
    ViewRect vr (0, 0, (int32)std::lround (baseWidth * s), (int32)std::lround (baseHeight * s));
    if (plugFrame)
        plugFrame->resizeView (this, &vr);
}

void EditorBase::HoverWatch::onMouseEntered (CView* view, CFrame*)
{
    // the frame enters views from the outside in: the last one entered is the innermost
    hovered = view;
    if (editor->info)
        editor->info->showFor (view);
}

void EditorBase::HoverWatch::onMouseExited (CView* view, CFrame* f)
{
    // left (or removed): its parent is under the mouse again, until the next view is entered
    if (hovered != view)
        return;
    CView* parent = view->getParentView ();
    hovered = parent == f ? nullptr : parent;
    if (editor->info)
        editor->info->showFor (hovered);
}

CMessageResult EditorBase::notify (CBaseObject* sender, const char* message)
{
    if (message == CVSTGUITimer::kMsgTimer && frame)
    {
        idle ();
        controller->checkLatency ();
    }
    return VSTGUIEditor::notify (sender, message);
}

void EditorBase::paramChanged (uint32_t id)
{
    auto it = byParam.find (id);
    if (it != byParam.end ())
        for (auto* v : it->second)
            v->invalid ();
}

void EditorBase::setTooltipsEnabled (bool on)
{
    controller->uiShowTips = on;
    if (frame)
    {
        frame->enableTooltips (on, 600);
        frame->invalid ();
    }
}

void EditorBase::applyParamTooltips (const char* (*helpFor) (uint32_t))
{
    // (wrapped for the floating tooltip, the text kept whole for the info box: prepareTooltip)
    for (auto& [id, views] : byParam)
        if (const char* t = helpFor (id))
            for (auto* v : views)
            {
                v->setTooltipText (t);
                prepareTooltip (v);
            }
}

} // namespace pk

namespace pk {

void EditorBase::copySettings () { putClipboardText (frame, controller->settingsText ()); }

bool EditorBase::pasteSettings ()
{
    const bool ok = controller->applySettingsText (clipboardText (frame));
    if (ok)
        refresh ();
    return ok;
}

bool EditorBase::settingsMenuPicked (int index, int first)
{
    if (index == first)
        copySettings ();
    else if (index == first + 1)
        pasteSettings ();
    else
        return false;
    return true;
}

} // namespace pk
