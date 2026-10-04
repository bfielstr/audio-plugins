// VSTGUI editor base: a freely resizable window that shows the UI uniformly zoomed and centred (the
// margins in the ground colour), parameter binding for pluginkit widgets, a periodic idle(), the info
// box under every editor's content and switchable, wrapped hover tooltips.
#pragma once

#include "pluginkit/ui/InfoBox.h"
#include "pluginkit/ui/Widgets.h"
#include "pluginkit/vst/ControllerBase.h"

#include "public.sdk/source/vst/vstguieditor.h"
#include "vstgui/lib/cframe.h"

#include <algorithm>
#include <functional>
#include <map>
#include <type_traits>
#include <vector>

namespace pk {

class EditorBase : public Steinberg::Vst::VSTGUIEditor, public ParamHost
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
    // The window size including the info strip, at 100 %.
    double fullWidth () const { return baseWidth; }
    double fullHeight () const { return baseHeight; }
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
    // The panel of the optional Smacheratr at the end of the chain (pk::addTailParams at `base`, the
    // extended fields at `extBase`, Gentlr's Advanced block at `ext2Base`, its High band and No
    // Overlap at `ext3Base`): every Smacheratr control, in two rows and three knobs. Gentlr (called
    // Clarity before) has one button, Advanced and a band selector; the selected band's Frequency,
    // Width and Range are shown (its Threshold sliders and region Drive are in
    // smacheratr::TailDisplays, at the right of the colour display, and so are No Overlap and the bands'
    // Slope, at the right of the panel's title). Needs about 680 x 78.
    Panel* addTailPanel (VSTGUI::CViewContainer* parent, const VSTGUI::CRect& r, uint32_t base, uint32_t extBase, uint32_t ext2Base,
                         uint32_t ext3Base, const char* title = "smacheratr  (end of the chain)");
public:
    // Shows Gentlr band `band`'s controls in the tail panel (the display calls it when a band is picked).
    void showTailBand (int band);

protected:
    // Sets tooltips on every bound parameter view from a help lookup.
    void applyParamTooltips (const char* (*helpFor) (uint32_t));
    // Called before the frame is released.
    virtual void onClose () {}

    ControllerBase* controller;
    const double contentHeight;        // the plug-in's own height (where the info strip starts)
    const double baseWidth, baseHeight; // the whole window at 100 %, the info strip included
    double scale = 1.0;
    std::map<uint32_t, std::vector<VSTGUI::CView*>> byParam;
    int tailBand = 0;                                              // Gentlr band shown in the tail panel
    std::vector<VSTGUI::CView*> tailBandViews[4], tailBandButtons; // its controls, per band (1, 2, Sub, High); the selector

private:
    // Lays the open frame out in a window of w x h px: zoomed by zoomFor and centred.
    void layoutFrame (double w, double h);
    // Adds the info strip under the content built by buildUI (the root view grows to hold it).
    void addInfoStrip ();
    // Appends the layout check's findings to $PK_LAYOUT_REPORT, when it is set (pk::layoutReport).
    void writeLayoutReport ();

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
