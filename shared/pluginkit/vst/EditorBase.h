// VSTGUI editor base: fixed-aspect zoomable frame, host-driven resizing, parameter binding for
// pluginkit widgets, a periodic idle() and switchable hover tooltips.
#pragma once

#include "pluginkit/ui/Widgets.h"
#include "pluginkit/vst/ControllerBase.h"

#include "public.sdk/source/vst/vstguieditor.h"

#include <map>
#include <type_traits>
#include <vector>

namespace pk {

class EditorBase : public Steinberg::Vst::VSTGUIEditor, public ParamHost
{
public:
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

    void setTooltipsEnabled (bool on);
    bool tooltipsEnabled () const { return controller->uiShowTips; }
    void resizeTo (double scale);
    double currentScale () const { return scale; }

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
    // extended fields at `extBase`, Gently's Advanced block at `ext2Base`): every Smacheratr control,
    // in two rows and three knobs. Gently (called Clarity before) has one button, Advanced and a band
    // selector; the selected band's Frequency, Width and Range are shown (its Threshold sliders and
    // region Drive are in smacheratr::TailDisplays, at the right of the colour display).
    // Needs about 680 x 78.
    Panel* addTailPanel (VSTGUI::CViewContainer* parent, const VSTGUI::CRect& r, uint32_t base, uint32_t extBase, uint32_t ext2Base,
                         const char* title = "smacheratr  (end of the chain)");
public:
    // Shows Gently band `band`'s controls in the tail panel (the display calls it when a band is picked).
    void showTailBand (int band);

protected:
    // Sets tooltips on every bound parameter view from a help lookup.
    void applyParamTooltips (const char* (*helpFor) (uint32_t));
    // Called before the frame is released.
    virtual void onClose () {}

    ControllerBase* controller;
    const double baseWidth, baseHeight;
    double scale = 1.0;
    std::map<uint32_t, std::vector<VSTGUI::CView*>> byParam;
    int tailBand = 0;                                              // Gently band shown in the tail panel
    std::vector<VSTGUI::CView*> tailBandViews[2], tailBandButtons; // its controls, per band; the selector
};

} // namespace pk
