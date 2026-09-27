#pragma once

#include "Bridge.h"
#include "Params.h"

#include "pluginterfaces/vst/ivstmidicontrollers.h"
#include "public.sdk/source/vst/vsteditcontroller.h"

#include <functional>
#include <vector>

namespace simplr {

class Editor;

class Controller : public Steinberg::Vst::EditController, public Steinberg::Vst::IMidiMapping
{
public:
    static Steinberg::FUnknown* createInstance (void*)
    {
        return (Steinberg::Vst::IEditController*)new Controller ();
    }

    Steinberg::tresult PLUGIN_API initialize (Steinberg::FUnknown* context) override;
    Steinberg::tresult PLUGIN_API terminate () override;
    Steinberg::tresult PLUGIN_API setComponentState (Steinberg::IBStream* state) override;
    Steinberg::tresult PLUGIN_API setState (Steinberg::IBStream* state) override; // editor-only state
    Steinberg::tresult PLUGIN_API getState (Steinberg::IBStream* state) override;
    Steinberg::IPlugView* PLUGIN_API createView (Steinberg::FIDString name) override;
    Steinberg::tresult PLUGIN_API notify (Steinberg::Vst::IMessage* message) override;
    Steinberg::tresult PLUGIN_API setParamNormalized (Steinberg::Vst::ParamID tag,
                                                      Steinberg::Vst::ParamValue value) override;
    Steinberg::tresult PLUGIN_API getMidiControllerAssignment (Steinberg::int32 busIndex, Steinberg::int16 channel,
                                                               Steinberg::Vst::CtrlNumber midiControllerNumber,
                                                               Steinberg::Vst::ParamID& id) override;

    void editorAttached (Steinberg::Vst::EditorView* editor) override;
    void editorRemoved (Steinberg::Vst::EditorView* editor) override;

    // --- helpers for the editor -------------------------------------------------
    Bridge* getBridge () const { return bridge; }
    double plain (uint32_t id) { return toPlain (id, getParamNormalized (id)); }
    void beginGesture (uint32_t id) { beginEdit (id); }
    void endGesture (uint32_t id) { endEdit (id); }
    void setFromUI (uint32_t id, double normalized); // setParamNormalized + performEdit
    void setPlainFromUI (uint32_t id, double plainValue); // full gesture
    void markDirty ();
    std::string sampleDisplayName ();

    // Sample actions (UI thread)
    bool loadSample (const std::string& path, bool isNewFile);
    void applyOps (const SampleOps& ops, bool remapFlags);
    void clearSample ();

    double uiScale = 1.0;
    bool uiShowTips = true;

    OBJ_METHODS (Controller, EditController)
    DEFINE_INTERFACES
        DEF_INTERFACE (IMidiMapping)
    END_DEFINE_INTERFACES (EditController)
    REFCOUNT_METHODS (EditController)

private:
    Bridge* bridge = nullptr;
    Editor* editor = nullptr;
    std::string pendingPath; // from setComponentState when no bridge is connected
};

} // namespace simplr
