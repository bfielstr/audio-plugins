#include "EditorBase.h"

#include "pluginkit/vst/Clipboard.h"
#include "pluginkit/vst/PresetBar.h"
#include "pluginkit/ui/LayoutCheck.h"
#include "pluginkit/ui/Theme.h"

#include "vstgui/lib/cframe.h"
#include "vstgui/lib/controls/coptionmenu.h"
#include "vstgui/lib/cvstguitimer.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <typeinfo>

namespace pk {

using namespace VSTGUI;
using namespace Steinberg;

EditorBase::EditorBase (ControllerBase* c, double w, double h)
: VSTGUIEditor (c), controller (c), madeWidth (w), madeHeight (h), fullContentHeight (h), contentHeight (h), baseWidth (w),
  baseHeight (h + kInfoHeight)
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
    // built in the layout the controller holds, with every section open; one that folds while building
    // sets the height again (setContentHeight)
    detached = false;
    resolveLayout ();
    frame = new CFrame (CRect (0, 0, baseWidth, baseHeight), this);
    // the margins left when the window's shape differs from the UI's (layoutFrame) show the ground
    frame->setBackgroundColor (theme::kGround);
    byParam.clear ();
    buildContent ();
    // every tooltip set while building, wrapped to lines (views made later are wrapped when the mouse
    // first enters them: helpFor, from HoverWatch)
    prepareTooltips (frame);
    frame->registerMouseObserver (&hoverWatch);
    frame->enableTooltips (controller->uiShowTips, 600);
    frame->open (parent, platformType);
    frame->setZoom (scale);
    // a section built folded (setContentHeight while building) recorded a shorter content; the window
    // the host opened is still the full height, so lay out at the full height first (laid out at the
    // short one, the UI was centred in the window: everything sat lower than drawn, by half the folded
    // section, and clicks missed), then ask the host for the shorter window
    const double foldedHeight = contentHeight;
    contentHeight = fullContentHeight;
    baseHeight = contentHeight + kInfoHeight;
    // the host may have given the window another shape than the UI's (a size it kept, or one it chose)
    layoutFrame (rect.getWidth (), rect.getHeight ());
    // the window at the content's height, if the host lets it be resized (otherwise the content keeps
    // its full height and the folded section's space stays empty; arranged, the blocks are already
    // placed for the folded section: the UI is zoomed to fit the window instead)
    if (arranged && foldedHeight != fullContentHeight)
        resizeBase (baseWidth, foldedHeight, true);
    else if (foldedHeight != fullContentHeight && !setContentHeight (foldedHeight))
        placeInfoStrip (); // (the strip back at the full content's end)
    return true;
}

void EditorBase::buildContent ()
{
    building = true;
    buildUI (frame);
    building = false;
    if (arranged)
        arrangeViews ();
    addInfoStrip ();
    writeLayoutReport ();
}

tresult PLUGIN_API EditorBase::getSize (ViewRect* size)
{
    // before the window opens, the layout in the controller's state decides the size it opens at
    if (!frame && (!resolved || appliedLayout != controller->uiLayout))
    {
        resolveLayout ();
        ViewRect vr (0, 0, (int32)std::lround (baseWidth * scale), (int32)std::lround (baseHeight * scale));
        setRect (vr);
    }
    return VSTGUIEditor::getSize (size);
}

bool EditorBase::setContentHeight (double h)
{
    if (arranged)
    {
        // the last section (the one that folds) is shorter by as much as the build's content would be:
        // its block gets shorter, what is under it moves up, the window follows
        foldedBy = std::clamp (madeHeight - h, 0.0, madeHeight);
        geometry = layout::place (spec, arrangement, foldHeights ());
        if (building || boxes.empty () || !frame || !frame->isAttached ())
        {
            contentHeight = geometry.height;
            baseHeight = contentHeight + kInfoHeight;
            return true;
        }
        placeBlocks ();
        return resizeBase (geometry.width, geometry.height, true);
    }
    h = std::clamp (h, 40.0, fullContentHeight);
    if (h == contentHeight)
        return true;
    if (building || !frame || !frame->isAttached ())
    {
        // building: only recorded (addInfoStrip places the strip there, open () asks the host)
        contentHeight = h;
        baseHeight = h + kInfoHeight;
        return true;
    }
    return resizeBase (baseWidth, h, false);
}

bool EditorBase::resizeBase (double w, double contentH, bool keepIfRefused)
{
    const double oldWidth = baseWidth, oldContent = contentHeight, oldBase = baseHeight;
    baseWidth = w;
    contentHeight = contentH;
    baseHeight = contentH + kInfoHeight;
    if (detached)
    {
        // (the draw benchmark's frame: no window, the frame itself takes the size; the Classic one's is the
        // size openDetached gives it, every section open, so it draws as it did before)
        if (frame)
            frame->setSize ((arranged ? baseWidth : madeWidth) * frame->getZoom (),
                            (arranged ? baseHeight : madeHeight + kInfoHeight) * frame->getZoom ());
        placeInfoStrip ();
        return true;
    }
    // the window at the zoom it shows now (the host calls onSize, which lays the frame out with the new
    // base size)
    ViewRect vr (0, 0, (int32)std::lround (baseWidth * scale), (int32)std::lround (baseHeight * scale));
    const bool ok = plugFrame && plugFrame->resizeView (this, &vr) == kResultTrue;
    if (!ok && !keepIfRefused)
    {
        // kept as it was: the content keeps its height, the folded section's space stays empty
        baseWidth = oldWidth;
        contentHeight = oldContent;
        baseHeight = oldBase;
        return false;
    }
    // (refused but kept: the new shape is zoomed to fit the window the host keeps, nothing is cut off)
    placeInfoStrip ();
    layoutFrame (rect.getWidth (), rect.getHeight ());
    return ok;
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
    detached = true;
    resolveLayout ();
    frame = new CFrame (CRect (0, 0, baseWidth, baseHeight), this);
    frame->setBackgroundColor (theme::kGround);
    byParam.clear ();
    buildContent (); // (PK_LAYOUT_REPORT: the draw benchmark checks the layout on Linux this way)
    if (arranged)
        frame->setSize (baseWidth, baseHeight); // (a section folded while building: the frame as the content)
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
        boxes.clear ();
        framed.clear ();
        overlays.clear ();
        headerRight.clear ();
        dropMark = nullptr;
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
        auto lines = layoutReport (frame);
        for (const auto& p : problems)
            lines.push_back ("unplaced: " + p);
        std::fprintf (f, "== %s%s%s: %zu\n", typeName (typeid (*this)).c_str (), arranged ? " " : "", arranged ? appliedLayout.c_str () : "",
                      lines.size ());
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
        // another layout picked (the menu, a project loaded): built again in it, between two ticks
        if (!building && (rebuildPending || appliedLayout != controller->uiLayout))
        {
            rebuildPending = false;
            relayout ();
        }
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

// ---- layouts (pluginkit/Layout.h) --------------------------------------------------------------------
namespace pk {

namespace {
CRect toRect (const layout::Box& b) { return CRect (b.left, b.top, b.right, b.bottom); }

std::string rectText (const CRect& r)
{
    char buf[80];
    std::snprintf (buf, sizeof (buf), "[%g %g %g %g]", r.left, r.top, r.right, r.bottom);
    return buf;
}

std::string lowerText (std::string s)
{
    for (auto& c : s)
        c = (char)std::tolower ((unsigned char)c);
    return s;
}
} // namespace

void EditorBase::resolveLayout ()
{
    // The Classic layout is the editor as it builds itself: nothing is moved, so it is what it was before
    // layouts, pixel for pixel. Any other is an arrangement of the editor's panels (none: Classic).
    resolved = true;
    appliedLayout = controller->uiLayout;
    arranged = false;
    arrangement = {};
    spec = {};
    if (!layout::isClassic (appliedLayout))
    {
        spec = layoutSpec (true);
        if (spec.width <= 0)
            spec.width = madeWidth;
        if (spec.height <= 0)
            spec.height = madeHeight;
        if (spec.headerSplit <= 0)
            spec.headerSplit = spec.width / 2;
        arrangement = layout::resolve (spec, appliedLayout);
        arranged = !arrangement.empty ();
    }
    foldedBy = 0;
    geometry = arranged ? layout::place (spec, arrangement) : layout::Geometry {};
    // the regions the build will have: a filling panel's as wide as its block's content
    regions.clear ();
    for (const auto& p : spec.panels)
    {
        layout::Box r = p.region;
        if (const layout::Block* b = arranged && p.fill ? geometry.find (p.id) : nullptr)
            r.right = r.left + b->content.width ();
        regions[p.id] = r;
    }
    baseWidth = arranged ? geometry.width : madeWidth;
    fullContentHeight = arranged ? geometry.height : madeHeight;
    contentHeight = fullContentHeight;
    baseHeight = contentHeight + kInfoHeight;
}

const layout::Box& EditorBase::regionOf (const layout::Panel& p) const
{
    auto it = regions.find (p.id);
    return it != regions.end () ? it->second : p.region;
}

CRect EditorBase::layoutRegion (const std::string& id, const CRect& built) const
{
    CRect r = built;
    if (const layout::Panel* p = arranged ? spec.find (id) : nullptr)
        if (p->fill)
            r.right = r.left + regionOf (*p).width () + (built.getWidth () - p->region.width ());
    return r;
}

std::map<std::string, double> EditorBase::foldHeights () const
{
    // the section that folds is the last of the content as built (the end saturator's, at the bottom)
    if (foldedBy <= 0 || spec.empty ())
        return {};
    const layout::Panel* last = nullptr;
    for (const auto& p : spec.panels)
        if (!last || p.region.bottom > last->region.bottom)
            last = &p;
    return {{last->id, last->region.height () - foldedBy}};
}

void EditorBase::arrangeViews ()
{
    // The plug-in built its content as always, into one root view. Each of the root's views goes to the
    // block of the panel whose region holds it (its coordinates stay the build's: the block's transform
    // moves it); the header's views stay (those at the right follow the window's right edge); a view over
    // the whole content (smemplr's modulation rings) grows with it and stays on top.
    problems.clear ();
    auto* root = frame && frame->getNbViews () > 0 ? frame->getView (0)->asViewContainer () : nullptr;
    if (!root)
    {
        problems.push_back ("no root view");
        return;
    }
    std::vector<CView*> kids;
    root->forEachChild ([&] (CView* v) { kids.push_back (v); });
    for (const auto& b : geometry.blocks)
        if (const layout::Panel* p = spec.find (b.id))
            boxes.push_back (new PanelBox (toRect (b.box), p->id, p->title, this));
    auto inside = [] (const CRect& r, const layout::Box& g) {
        return r.left >= g.left - 1 && r.right <= g.right + 1 && r.top >= g.top - 1 && r.bottom <= g.bottom + 1;
    };
    for (CView* v : kids)
    {
        const CRect r = v->getViewSize ();
        if (r.getWidth () >= madeWidth * 0.9 && r.getHeight () >= madeHeight * 0.9)
        {
            overlays.push_back (v);
            continue;
        }
        if (r.bottom <= spec.headerHeight + 1)
        {
            if (r.left >= spec.headerSplit)
                headerRight.emplace_back (v, r);
            continue;
        }
        PanelBox* to = nullptr;
        for (auto* box : boxes)
            if (r.isEmpty () ? box->panelId () == spec.fallback : inside (r, regionOf (*spec.find (box->panelId ()))))
            {
                to = box;
                break;
            }
        if (!to)
        {
            problems.push_back (typeName (typeid (*v)) + " " + rectText (r));
            continue;
        }
        root->removeView (v, false); // (the reference the root adopted goes over to the block)
        to->addView (v);
    }
    // the blocks whose content is one panel (it grows to fill the block: placeBlocks)
    for (auto* box : boxes)
    {
        const layout::Box& g = regionOf (*spec.find (box->panelId ()));
        Panel* only = nullptr;
        int count = 0;
        box->forEachChild ([&] (CView* v) {
            ++count;
            const CRect r = v->getViewSize ();
            if (auto* p = dynamic_cast<Panel*> (v))
                if (std::fabs (r.left - g.left) <= 1 && std::fabs (r.top - g.top) <= 1 && std::fabs (r.right - g.right) <= 1 &&
                    std::fabs (r.bottom - g.bottom) <= 1)
                    only = p;
        });
        if (only && count == 1)
            framed.push_back ({box, only, only->getViewSize ()});
    }
    for (auto* box : boxes)
        root->addView (box);
    for (auto* o : overlays)
    {
        root->removeView (o, false);
        root->addView (o);
    }
    placeBlocks ();
}

void EditorBase::placeBlocks ()
{
    for (auto* box : boxes)
        if (const layout::Block* b = geometry.find (box->panelId ()))
            if (const layout::Panel* p = spec.find (b->id))
                box->place (toRect (b->box), toRect (regionOf (*p)), toRect (b->content));
    // a block's only panel fills the block under its strip: as much larger on each side as the block is
    // larger than the content there (in the build's coordinates), its children moved back by its transform
    for (const auto& f : framed)
        if (const layout::Block* b = geometry.find (f.box->panelId ()))
        {
            const double l = b->content.left - b->box.left, t = b->content.top - (b->box.top + layout::kStrip);
            const double r = b->box.right - b->content.right, bt = b->box.bottom - b->content.bottom;
            const CRect grown (f.built.left - l, f.built.top - t, f.built.right + r, f.built.bottom + bt);
            if (f.panel->getViewSize () != grown)
            {
                f.panel->invalid ();
                f.panel->setViewSize (grown);
                f.panel->setMouseableArea (grown);
            }
            f.panel->setTransform (CGraphicsTransform ().translate (l, t));
            f.panel->invalid ();
        }
    const double dx = geometry.width - madeWidth;
    for (auto& [v, built] : headerRight)
    {
        CRect r = built;
        r.offset (dx, 0);
        if (v->getViewSize () != r)
        {
            v->invalid ();
            v->setViewSize (r);
            v->setMouseableArea (r);
        }
    }
    for (auto* o : overlays)
    {
        const CRect r (0, 0, geometry.width, geometry.height);
        o->setViewSize (r);
        o->setMouseableArea (r);
    }
    if (frame && frame->getNbViews () > 0)
        if (auto* root = frame->getView (0)->asViewContainer ())
        {
            CRect rr = root->getViewSize ();
            rr.right = rr.left + geometry.width;
            root->setViewSize (rr);
            root->setMouseableArea (rr);
        }
    if (frame)
        frame->invalid ();
}

void EditorBase::relayout ()
{
    if (!frame)
        return;
    // everything built again (the plug-in lets go of its views first, as when the window closes), in
    // the layout the controller holds now, then the window asked for its size at the zoom it shows
    onClose ();
    hoverWatch.hovered = nullptr;
    info = nullptr;
    boxes.clear ();
    framed.clear ();
    overlays.clear ();
    headerRight.clear ();
    dropMark = nullptr;
    problems.clear ();
    frame->removeAll ();
    byParam.clear ();
    resolveLayout ();
    buildContent ();
    prepareTooltips (frame);
    frame->enableTooltips (controller->uiShowTips, 600);
    // (refused: an arranged layout is zoomed to fit the window; the Classic one keeps its full content
    // height, as open () leaves it, with a folded section's space empty)
    if (!resizeBase (baseWidth, contentHeight, arranged) && !arranged)
    {
        contentHeight = fullContentHeight;
        baseHeight = contentHeight + kInfoHeight;
        placeInfoStrip ();
        if (!detached)
            layoutFrame (rect.getWidth (), rect.getHeight ());
    }
    frame->invalid ();
}

void EditorBase::setLayout (const std::string& text, const std::string& name, bool now)
{
    if (controller->uiLayout != text || controller->uiLayoutName != name)
    {
        controller->uiLayout = text;
        controller->uiLayoutName = name;
        controller->markDirty ();
    }
    if (now && frame && !building && appliedLayout != controller->uiLayout)
        relayout ();
}

void EditorBase::commitArrangement (const layout::Arrangement& a, bool keep)
{
    if (!(a == arrangement))
    {
        arrangement = a;
        geometry = layout::place (spec, arrangement, foldHeights ());
        fullContentHeight = layout::place (spec, arrangement).height;
        placeBlocks ();
        resizeBase (geometry.width, geometry.height, true);
    }
    if (!keep)
        return;
    // into the state: Wide again when that is what it is, else a custom arrangement (no name)
    std::string text = layout::toString (arrangement), name;
    if (text == layout::toString (layout::wide (spec)))
        text = "wide", name = "Wide";
    if (layout::resolve (spec, controller->uiLayout) == arrangement)
        text = controller->uiLayout, name = controller->uiLayoutName; // (dropped where it was)
    setLayout (text, name);
    // the frame shows it already, but a filling panel built at another width than its block has now is
    // built again at the new one (everything is: the editor builds as one)
    bool rebuild = false;
    for (const auto& p : spec.panels)
        if (const layout::Block* b = p.fill ? geometry.find (p.id) : nullptr)
            rebuild = rebuild || std::fabs (b->content.width () - regionOf (p).width ()) > 0.5;
    appliedLayout = controller->uiLayout;
    rebuildPending = rebuildPending || rebuild; // (at the next tick: this runs in a block's mouse event)
}

void EditorBase::blockDragged (PanelBox* box, CPoint where, bool drop)
{
    const layout::Drop d = layout::dropAt (arrangement, geometry, where.x, where.y);
    if (!drop)
    {
        showDropMark (d.kind == layout::Drop::None ? nullptr : &d.mark);
        return;
    }
    showDropMark (nullptr);
    commitArrangement (layout::move (spec, arrangement, box->panelId (), d), true);
}

void EditorBase::blockDragCancelled (PanelBox*) { showDropMark (nullptr); }

void EditorBase::blockResized (PanelBox* box, double width, bool done)
{
    commitArrangement (layout::resize (spec, arrangement, box->panelId (), width), done);
}

void EditorBase::showDropMark (const layout::Box* mark)
{
    auto* root = frame && frame->getNbViews () > 0 ? frame->getView (0)->asViewContainer () : nullptr;
    if (!root)
        return;
    if (!mark)
    {
        if (dropMark && dropMark->isVisible ())
        {
            dropMark->invalid ();
            dropMark->setVisible (false);
        }
        return;
    }
    const CRect r = toRect (*mark);
    if (!dropMark)
    {
        dropMark = new DropMark (r);
        root->addView (dropMark);
    }
    else if (dropMark->getViewSize () != r)
    {
        dropMark->invalid ();
        dropMark->setViewSize (r);
    }
    dropMark->setVisible (true);
    dropMark->invalid ();
}

CPoint EditorBase::layoutPoint (CPoint p) const
{
    if (!arranged)
        return p;
    if (p.y <= spec.headerHeight + 1)
    {
        if (p.x >= spec.headerSplit)
            p.x += geometry.width - madeWidth;
        return p;
    }
    for (const auto& b : geometry.blocks)
        if (const layout::Panel* panel = spec.find (b.id))
        {
            const layout::Box& g = regionOf (*panel);
            if (p.x >= g.left && p.x <= g.right && p.y >= g.top && p.y <= g.bottom)
                return CPoint (p.x - g.left + b.content.left, p.y - g.top + b.content.top);
        }
    return p;
}

bool EditorBase::pickedInSubMenu (COptionMenu* menu)
{
    int32_t index = -1;
    return menu && menu->getLastItemMenu (index) != menu;
}

namespace {
// a command item that does its own work when picked
void addCommand (COptionMenu* m, const std::string& title, std::function<void ()> fn, bool checked = false, bool enabled = true)
{
    auto* item = new CCommandMenuItem (CCommandMenuItem::Desc (title.c_str ()));
    item->setActions ([fn] (CCommandMenuItem*) {
        if (fn)
            fn ();
    });
    item->setChecked (checked);
    item->setEnabled (enabled);
    m->addEntry (item);
}
} // namespace

void EditorBase::addDefaultsMenu (COptionMenu* menu)
{
    const GentlrIds ids = controller->gentlrIds ();
    if (!menu)
        return;
    // Checked: what the file says, or (not set yet) the factory default: Gentlr with the end saturator on
    // (both on by default), Advanced off (gentlr's own Advanced is on in a new gentlr). A pick sets the switch to the other state; it reads the file again and changes
    // its own switch only (another instance may have changed the other one since this menu opened).
    const GentlrDefaults d = controller->gentlrDefaults ();
    const auto factoryOn = [this] (int32_t id) { return controller->table ().defaultNormalized ((uint32_t)id) >= 0.5; };
    auto sub = makeOwned<COptionMenu> ();
    if (ids.gentlr >= 0)
    {
        const bool checked = d.gentlrOn.value_or (factoryOn (ids.gentlr) && (ids.saturator < 0 || factoryOn (ids.saturator)));
        addCommand (
            sub, "Gentlr On by Default",
            [this, checked] {
                GentlrDefaults now = controller->gentlrDefaults ();
                now.gentlrOn = !checked;
                controller->writeGentlrDefaults (now);
            },
            checked);
    }
    if (ids.advanced >= 0)
    {
        const bool checked = d.advancedOn.value_or (factoryOn (ids.advanced));
        addCommand (
            sub, "Advanced On by Default",
            [this, checked] {
                GentlrDefaults now = controller->gentlrDefaults ();
                now.advancedOn = !checked;
                controller->writeGentlrDefaults (now);
            },
            checked);
    }
    if (ids.gentlr >= 0 || ids.advanced >= 0)
    {
        sub->addSeparator ();
        if (ids.gentlr >= 0 && ids.saturator >= 0)
            addCommand (sub, "(Gentlr On also switches on smacheratr at the end)", {}, false, false);
        addCommand (sub, "(For new instances; projects and presets keep theirs)", {}, false, false);
        sub->addSeparator ();
    }
    // Glue Bands on Touch (GentlrDefaults.h): an editor preference of the whole suite, every plug-in's
    // Gentlr displays at once (smemplr's slots too, which have no Gentlr defaults of their own here)
    {
        const bool checked = glueOnTouch ();
        addCommand (sub, "Glue Bands on Touch", [checked] { writeGlueOnTouch (!checked); }, checked);
        addCommand (sub, "(Band edges dragged together snap and glue; every plug-in)", {}, false, false);
    }
    menu->addEntry (sub, "Defaults");
}

void EditorBase::addLayoutMenu (COptionMenu* menu)
{
    if (!menu)
        return;
    if (layoutSpec (true).empty ())
    {
        // (no Layout: the Defaults after a separator of their own)
        menu->addSeparator ();
        addDefaultsMenu (menu);
        return;
    }
    // Wide and Classic, the saved layouts, then the commands. Each entry does its own work when picked
    // (a command item); the layout changes at the next tick (the menu's button is built again with it).
    const layout::Saved saved = controller->savedLayouts ();
    const std::string now = controller->uiLayout, name = controller->uiLayoutName;
    auto sub = makeOwned<COptionMenu> ();
    const layout::Named* savedNow = nullptr;
    for (const auto& n : saved.layouts)
        if (n.name == name && n.layout == now)
            savedNow = &n;
    // (Classic: the fixed layout every editor had before layouts, called Default then; written as
    // "default", which 0.14 reads as its Default too)
    const bool isClassic = !savedNow && layout::isClassic (now), isWide = !savedNow && lowerText (now) == "wide";
    addCommand (sub, "Wide", [this] { setLayout ("wide", "Wide"); }, isWide);
    addCommand (sub, "Classic", [this] { setLayout (layout::kClassicText, "Classic"); }, isClassic);
    if (!saved.layouts.empty ())
        sub->addSeparator ();
    for (const auto& n : saved.layouts)
        addCommand (sub, n.name, [this, n] { setLayout (n.layout, n.name); }, savedNow == &n);
    if (!savedNow && !isClassic && !isWide)
        addCommand (sub, "Custom (not saved)", {}, true, false);
    sub->addSeparator ();
    addCommand (sub, "Save Layout As...", [this] { promptLayoutName (); });
    auto same = [] (const std::string& a, const std::string& b) {
        return a == b || (layout::isClassic (a) && layout::isClassic (b)) || (lowerText (a) == "wide" && lowerText (b) == "wide");
    };
    const bool isTheDefault = same (saved.hasDefault ? saved.defaultLayout : std::string (layout::kDefaultLayout), now);
    addCommand (sub, "Use as Default Layout", [this] {
        layout::Saved s = controller->savedLayouts ();
        s.defaultLayout = layout::isClassic (controller->uiLayout) ? std::string (layout::kClassicText) : controller->uiLayout;
        s.hasDefault = true;
        controller->writeSavedLayouts (s);
    },
         isTheDefault);
    if (!saved.layouts.empty ())
    {
        auto del = makeOwned<COptionMenu> ();
        for (const auto& n : saved.layouts)
            addCommand (del, n.name, [this, n] {
                layout::Saved s = controller->savedLayouts ();
                s.remove (n.name);
                controller->writeSavedLayouts (s);
                if (controller->uiLayoutName == n.name)
                    controller->uiLayoutName.clear (); // (the layout stays, as a custom one)
            });
        sub->addEntry (del, "Delete Layout");
    }
    menu->addSeparator ();
    menu->addEntry (sub, "Layout");
    addDefaultsMenu (menu);
}

void EditorBase::promptLayoutName ()
{
    std::string suggestion = controller->uiLayoutName;
    if (!layout::validName (suggestion))
        suggestion = "My Layout";
    showPrompt (frame, "Save Layout", {{"Name", suggestion}}, "Save", [this] (const std::vector<std::string>& v) -> std::string {
        std::string n = v.empty () ? std::string () : v[0];
        while (!n.empty () && std::isspace ((unsigned char)n.back ()))
            n.pop_back ();
        while (!n.empty () && std::isspace ((unsigned char)n.front ()))
            n.erase (n.begin ());
        if (!layout::validName (n))
            return "Give it a name of its own (not Classic, Default or Wide, no \"=\").";
        layout::Saved s = controller->savedLayouts ();
        s.put (n, layout::isClassic (controller->uiLayout) ? std::string (layout::kClassicText) : controller->uiLayout);
        if (!controller->writeSavedLayouts (s))
            return "The layout could not be saved in the presets folder.";
        setLayout (s.find (n)->layout, s.find (n)->name);
        appliedLayout = controller->uiLayout; // (the same arrangement: nothing to build again)
        return {};
    });
}

bool EditorBase::findControl (uint32_t id, CRect& out) const
{
    auto it = byParam.find (id);
    if (!frame || it == byParam.end ())
        return false;
    for (CView* v : it->second)
    {
        bool shown = v->isVisible ();
        for (CView* p = v->getParentView (); shown && p && p != frame; p = p->getParentView ())
            shown = p->isVisible ();
        if (!shown)
            continue;
        // the view's rectangle into the frame (through the blocks' transforms), then the frame's zoom and
        // margins: window pixels
        const CRect r = v->getViewSize ();
        CPoint tl = r.getTopLeft (), br = r.getBottomRight ();
        v->localToFrame (tl);
        v->localToFrame (br);
        CRect w (tl.x, tl.y, br.x, br.y);
        frame->getTransform ().transform (w);
        out = w;
        return true;
    }
    return false;
}

std::vector<std::pair<std::string, CRect>> EditorBase::layoutBlocks () const
{
    std::vector<std::pair<std::string, CRect>> out;
    for (auto* box : boxes)
        out.emplace_back (box->panelId (), box->getViewSize ());
    return out;
}

} // namespace pk
