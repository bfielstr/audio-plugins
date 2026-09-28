#pragma once

#include "Cids.h"
#include "Params.h"
#include "SharedMeters.h"

#include "pluginkit/vst/ControllerBase.h"

#include "pluginterfaces/vst/ivstmidicontrollers.h"

namespace perrera {

class Controller : public pk::ControllerBase, public Steinberg::Vst::IMidiMapping
{
public:
    Controller () : pk::ControllerBase (paramTable ()) { setPresetInfo (kProcessorUID, "Perrera"); }
    static Steinberg::FUnknown* createInstance (void*) { return (Steinberg::Vst::IEditController*)new Controller (); }

    Steinberg::tresult PLUGIN_API initialize (Steinberg::FUnknown* context) override;
    Steinberg::tresult PLUGIN_API terminate () override;
    Steinberg::tresult PLUGIN_API setComponentState (Steinberg::IBStream* state) override;
    Steinberg::IPlugView* PLUGIN_API createView (Steinberg::FIDString name) override;
    Steinberg::tresult PLUGIN_API notify (Steinberg::Vst::IMessage* message) override;
    Steinberg::tresult PLUGIN_API getMidiControllerAssignment (Steinberg::int32 busIndex, Steinberg::int16 channel,
                                                               Steinberg::Vst::CtrlNumber midiControllerNumber,
                                                               Steinberg::Vst::ParamID& id) override;

    SharedMeters* getShared () const { return shared; }

    OBJ_METHODS (Controller, pk::ControllerBase)
    DEFINE_INTERFACES
        DEF_INTERFACE (IMidiMapping)
    END_DEFINE_INTERFACES (pk::ControllerBase)
    REFCOUNT_METHODS (pk::ControllerBase)

private:
    SharedMeters* shared = nullptr;
};

} // namespace perrera
