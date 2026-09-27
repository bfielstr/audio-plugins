#pragma once

#include "Bridge.h"
#include "Params.h"

#include "pluginkit/vst/ControllerBase.h"
#include "pluginterfaces/vst/ivstmidicontrollers.h"

#include <functional>
#include <vector>

namespace simplr {

class Editor;

class Controller : public pk::ControllerBase, public Steinberg::Vst::IMidiMapping
{
public:
    Controller () : pk::ControllerBase (paramTable ()) {}

    static Steinberg::FUnknown* createInstance (void*)
    {
        return (Steinberg::Vst::IEditController*)new Controller ();
    }

    Steinberg::tresult PLUGIN_API initialize (Steinberg::FUnknown* context) override;
    Steinberg::tresult PLUGIN_API terminate () override;
    Steinberg::tresult PLUGIN_API setComponentState (Steinberg::IBStream* state) override;
    Steinberg::IPlugView* PLUGIN_API createView (Steinberg::FIDString name) override;
    Steinberg::tresult PLUGIN_API notify (Steinberg::Vst::IMessage* message) override;
    Steinberg::tresult PLUGIN_API getMidiControllerAssignment (Steinberg::int32 busIndex, Steinberg::int16 channel,
                                                               Steinberg::Vst::CtrlNumber midiControllerNumber,
                                                               Steinberg::Vst::ParamID& id) override;


    // --- helpers for the editor -------------------------------------------------
    Bridge* getBridge () const { return bridge; }
    std::string sampleDisplayName ();

    // Sample actions (UI thread)
    bool loadSample (const std::string& path, bool isNewFile);
    void applyOps (const SampleOps& ops, bool remapFlags);
    void clearSample ();


    OBJ_METHODS (Controller, pk::ControllerBase)
    DEFINE_INTERFACES
        DEF_INTERFACE (IMidiMapping)
    END_DEFINE_INTERFACES (pk::ControllerBase)
    REFCOUNT_METHODS (pk::ControllerBase)

private:
    Bridge* bridge = nullptr;
    std::string pendingPath; // from setComponentState when no bridge is connected
};

} // namespace simplr
