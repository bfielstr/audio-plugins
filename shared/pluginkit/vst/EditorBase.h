// VSTGUI editor base: a freely resizable window that shows the UI uniformly zoomed and centred (the
// margins in the ground colour), parameter binding for pluginkit widgets, a periodic idle(), the info
// box under every editor's content, switchable, wrapped hover tooltips, and the layouts (Menu > Layout:
// the editor as built, the Wide template, the user's own arrangements; pluginkit/Layout.h).
#pragma once

#include "pluginkit/Layout.h"
#include "pluginkit/ui/InfoBox.h"
#include "pluginkit/ui/LayoutViews.h"
#include "pluginkit/ui/Widgets.h"
#include "pluginkit/vst/ControllerBase.h"

#include "public.sdk/source/vst/vstguieditor.h"
#include "vstgui/lib/cframe.h"

#include <algorithm>
#include <functional>
#include <map>
#include <type_traits>
#include <vector>

namespace VSTGUI {
class COptionMenu;
}

namespace pk {

class EditorBase : public Steinberg::Vst::VSTGUIEditor, public ParamHost, private PanelBox::Handler
{
public:
    // The strip under every editor's content that holds the info box: the window is this much taller
    // than the `height` a plug-in gives (its content keeps its coordinates; the strip is added below).
    static constexpr double kInfoHeight = InfoBox::kStripHeight;
    // The zoom range (Menu > Interface Size and the host's resizing).
    static constexpr double kMinZoom = 0.5, kMaxZoom = 2.0;

    // width x height: the plug-in's content at 100 % (the window adds kInfoHeight below it).
    EditorBase (ControllerBase* c, double width, double height);

    bool PLUGIN_API open (void* parent, const VSTGUI::PlatformType& platformType) override;
    void PLUGIN_API close () override;
    Steinberg::tresult PLUGIN_API canResize () override { return Steinberg::kResultTrue; }
    // (the size of the layout the window opens with: the Default's, or an arrangement's)
    Steinberg::tresult PLUGIN_API getSize (Steinberg::ViewRect* size) override;
    Steinberg::tresult PLUGIN_API checkSizeConstraint (Steinberg::ViewRect* rect) override;
    Steinberg::tresult PLUGIN_API onSize (Steinberg::ViewRect* newSize) override;
    VSTGUI::CMessageResult notify (VSTGUI::CBaseObject* sender, const char* message) override;

    // Builds the view hierarchy into an (unopened) frame.
    virtual void buildUI (VSTGUI::CFrame* f) = 0;
    // Called about 30 times a second while open.
    virtual void idle () {}
    // Host or UI changed a parameter: repaints the views bound to it. Override to do more.
    virtual void paramChanged (uint32_t id);

    // Repaints everything (after a preset load or a reset).
    void refresh ()
    {
        if (frame)
            frame->invalid ();
    }

    // Copy / Paste Settings (the plug-in's menu): every parameter as text on the clipboard, for this
    // plug-in or the same effect in Smemplr's rack (ControllerBase::settingsText).
    void copySettings ();
    bool pasteSettings ();
    // A menu's callback: index is the entry picked, first what addSettingsMenuEntries returned; true
    // when it was one of the two.
    bool settingsMenuPicked (int index, int first);

    // The floating tooltips ("?" in each header); the info box shows the same help either way.
    void setTooltipsEnabled (bool on);
    bool tooltipsEnabled () const { return controller->uiShowTips; }
    // Asks the host for the window at `scale` times the base size (Menu > Interface Size).
    void resizeTo (double scale);
    double currentScale () const { return scale; }
    // The zoom a window of w x h shows the UI at: as large as fits both ways (never stretched), within
    // kMinZoom .. kMaxZoom.
    static double zoomFor (double w, double h, double baseW, double baseH)
    {
        if (baseW <= 0 || baseH <= 0)
            return 1.0;
        return std::clamp (std::min (w / baseW, h / baseH), kMinZoom, kMaxZoom);
    }
    // ---- layouts (pluginkit/Layout.h, issue #11)
    // The editor's panels: the regions of its content and the Wide template's rows. `arranged`: for a
    // layout other than the Default (an editor may build some content differently for those, and declare
    // its regions as built that way: smemplr splits its modulation column in two). None (the default):
    // the editor has only the Default layout and no Layout menu.
    virtual layout::Spec layoutSpec (bool /*arranged*/) const { return {}; }
    // While building (and after): whether this build is arranged (not the Default layout).
    bool arrangedLayout () const { return arranged; }
    // A point of the build (a menu's place under its button, in the editor's coordinates as built) where
    // it is shown now: moved with its panel's block, or with the window's right edge in the header.
    VSTGUI::CPoint layoutPoint (VSTGUI::CPoint built) const;
    // Picks a layout (its text, see Layout.h, and the name the menu shows it by): kept in the controller's
    // state; the editor is built again in it at the next tick (now: at once).
    void setLayout (const std::string& text, const std::string& name, bool now = false);
    // The Layout sub-menu at the end of a menu (after a separator): Default, Wide, the saved layouts, Save
    // Layout As..., Use as Default Layout, Delete Layout. Its entries act by themselves: a menu's callback
    // must ignore picks from sub-menus (pickedInSubMenu).
    void addLayoutMenu (VSTGUI::COptionMenu* menu);
    static bool pickedInSubMenu (VSTGUI::COptionMenu* menu);
    // Where the control bound to a parameter is shown, in window pixels (the zoom and the margins
    // applied): false when none is visible. (Host tests find controls this way, whatever the layout.)
    bool findControl (uint32_t id, VSTGUI::CRect& r) const;
    // What an arranged build could not place (views of the root in no panel's region, named with their
    // rectangles): empty when every view went to a block. The draw benchmark's layout check fails on it.
    const std::vector<std::string>& layoutProblems () const { return problems; }
    // The blocks now (panel id and rectangle in the root's coordinates; none in the Default layout).
    std::vector<std::pair<std::string, VSTGUI::CRect>> layoutBlocks () const;

    // The window size including the info strip, at 100 %.
    double fullWidth () const { return baseWidth; }
    double fullHeight () const { return baseHeight; }
    // A section of the content folded or opened (the end saturator's, smacheratr::TailPanel): the content
    // is now `height` tall (at most the height the editor was made with). The window asks the host for
    // the new size at the zoom it has (so nothing changes size), the info strip moves up or down with the
    // content's end. False when the host keeps the window as it is: the content then keeps its full
    // height (the space under a folded section stays empty). While building (before the window opens) it
    // only records the height; open () asks the host for it. `height` is in the build's coordinates (the
    // section's end as built); in an arranged layout the section's block gets shorter by as much (what is
    // under it moves up) and the window keeps the new shape even when the host keeps its size (zoomed).
    bool setContentHeight (double height);
    double contentHeightNow () const { return contentHeight; }
    double contentHeightMade () const { return madeHeight; } // every section open, as built (the build's coordinates)
    ControllerBase* controllerBase () const { return controller; }
    InfoBox* infoBox () const { return info; }
    // For measuring (the draw benchmark, shared/pluginkit/testing/DrawBench.cpp): builds the UI into a
    // frame of its own that is not put in a window, zoomed by `zoom`, for drawing into an offscreen
    // context. `wrap` may supply a container of the given size to hold the content between it and the
    // frame (one that records what the views invalidate). close () releases it.
    using RootWrapper = std::function<VSTGUI::CViewContainer* (const VSTGUI::CRect&)>;
    VSTGUI::CFrame* openDetached (double zoom, const RootWrapper& wrap = {});

    // ParamHost
    const ParamTable& table () override { return controller->table (); }
    double norm (uint32_t id) override { return controller->getParamNormalized (id); }
    double plainValue (uint32_t id) override { return controller->plain (id); }
    void beginEdit (uint32_t id) override { controller->beginGesture (id); }
    void setNorm (uint32_t id, double v) override { controller->setFromUI (id, v); }
    void endEdit (uint32_t id) override { controller->endGesture (id); }
    std::string valueText (uint32_t id) override { return controller->table ().toText (id, plainValue (id)); }

protected:
    // Adds a view to a container; ParamViews are remembered for repainting.
    template <typename T>
    T* bind (VSTGUI::CViewContainer* parent, T* view)
    {
        parent->addView (view);
        if constexpr (std::is_base_of_v<ParamView, T>)
            byParam[view->paramId ()].push_back (view);
        return view;
    }
    // Sets tooltips on every bound parameter view from a help lookup.
    void applyParamTooltips (const char* (*helpFor) (uint32_t));
    // Called before the frame is released.
    virtual void onClose () {}

    ControllerBase* controller;
    const double madeWidth, madeHeight; // the plug-in's own content as it builds it (the Default layout)
    double fullContentHeight;           // the content's height in this layout, every section open
    double contentHeight;               // its height now (where the info strip starts; less while a section is folded)
    double baseWidth;                   // the whole window at 100 %, the info strip included
    double baseHeight;
    double scale = 1.0;
    std::map<uint32_t, std::vector<VSTGUI::CView*>> byParam;

private:
    // Lays the open frame out in a window of w x h px: zoomed by zoomFor and centred.
    void layoutFrame (double w, double h);
    // The root view and the info strip at the content height now.
    void placeInfoStrip ();
    // Adds the info strip under the content built by buildUI (the root view grows to hold it).
    void addInfoStrip ();
    // Appends the layout check's findings to $PK_LAYOUT_REPORT, when it is set (pk::layoutReport).
    void writeLayoutReport ();
    // The window asked of the host for content of w x contentH (at the zoom it shows); false when the host
    // keeps the window. keepIfRefused: the new shape stays anyway (zoomed to fit the window it has).
    bool resizeBase (double w, double contentH, bool keepIfRefused);

    // buildUI (building), then the blocks of an arranged layout, the info strip and the layout report
    void buildContent ();

    // layouts
    void resolveLayout ();   // the layout in the controller's state: spec, arrangement, geometry, base size
    void arrangeViews ();     // after building: the root's views into the panels' blocks
    void placeBlocks ();      // the blocks, the header's right part and the overlays where the geometry says
    void relayout ();         // everything built again in the layout the controller holds now
    void commitArrangement (const layout::Arrangement& a, bool keep); // a drag's result (kept: into the state)
    std::map<std::string, double> foldHeights () const; // the folded section's height now, by panel id
    void blockDragged (PanelBox* box, VSTGUI::CPoint where, bool drop) override;
    void blockDragCancelled (PanelBox* box) override;
    void blockResized (PanelBox* box, double width, bool done) override;
    void showDropMark (const layout::Box* mark); // (nullptr: hidden)
    void promptLayoutName ();

    layout::Spec spec;               // the editor's panels (as built for this layout)
    layout::Arrangement arrangement; // empty: the Default layout
    layout::Geometry geometry;       // where the blocks are now
    bool arranged = false;
    bool resolved = false;           // spec and size known for controller->uiLayout (getSize, open)
    bool building = false;           // inside buildUI: a section folding only records its height
    bool detached = false;           // openDetached: no window to ask for a size
    std::string appliedLayout;       // the layout text the frame was built in
    double foldedBy = 0;             // how much shorter the last section is now (folded) than built
    std::vector<PanelBox*> boxes;    // owned by the root view
    std::vector<VSTGUI::CView*> overlays;                              // views over the whole content (smemplr's modulation rings)
    std::vector<std::pair<VSTGUI::CView*, VSTGUI::CRect>> headerRight; // header controls at the right, as built
    DropMark* dropMark = nullptr;
    std::vector<std::string> problems;

    // Follows the view under the mouse (the frame tells it each view entered and left) and shows its
    // help in the info box.
    struct HoverWatch : VSTGUI::IMouseObserver
    {
        EditorBase* editor = nullptr;
        VSTGUI::SharedPointer<VSTGUI::CView> hovered; // the innermost view under the mouse
        void onMouseEntered (VSTGUI::CView* view, VSTGUI::CFrame* frame) override;
        void onMouseExited (VSTGUI::CView* view, VSTGUI::CFrame* frame) override;
        void onMouseEvent (VSTGUI::MouseEvent&, VSTGUI::CFrame*) override {}
    };
    HoverWatch hoverWatch;
    InfoBox* info = nullptr; // owned by the frame
};

} // namespace pk
