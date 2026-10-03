#pragma once

#include "Cids.h"
#include "SharedMeters.h"
#include "Params.h"

#include "pluginkit/vst/ControllerBase.h"

namespace dropr {

class Controller : public pk::ControllerBase
{
public:
    Controller () : pk::ControllerBase (paramTable ()) { setPresetInfo (kProcessorUID, "Dropr"); }
    static Steinberg::FUnknown* createInstance (void*) { return (Steinberg::Vst::IEditController*)new Controller (); }

    Steinberg::tresult PLUGIN_API terminate () override;
    Steinberg::tresult PLUGIN_API setComponentState (Steinberg::IBStream* state) override;
    Steinberg::IPlugView* PLUGIN_API createView (Steinberg::FIDString name) override;
    Steinberg::tresult PLUGIN_API notify (Steinberg::Vst::IMessage* message) override;

    SharedMeters* getShared () const { return shared; }

protected:
    Steinberg::Vst::Parameter* makeParameter (uint32_t id) override;

private:
    SharedMeters* shared = nullptr;
};

} // namespace dropr
