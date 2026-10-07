#pragma once

#include "Cids.h"
#include "SharedMeters.h"
#include "Params.h"

#include "pluginkit/vst/ControllerBase.h"

namespace moistr {

class Controller : public pk::ControllerBase
{
public:
    Controller () : pk::ControllerBase (paramTable ()) { setPresetInfo (kProcessorUID, "Moistr"); setGentlrIds (kGentlrIds); }
    static Steinberg::FUnknown* createInstance (void*) { return (Steinberg::Vst::IEditController*)new Controller (); }

    Steinberg::tresult PLUGIN_API terminate () override;
    Steinberg::tresult PLUGIN_API setComponentState (Steinberg::IBStream* state) override;
    Steinberg::IPlugView* PLUGIN_API createView (Steinberg::FIDString name) override;
    Steinberg::tresult PLUGIN_API notify (Steinberg::Vst::IMessage* message) override;

    SharedMeters* getShared () const { return shared; }

private:
    SharedMeters* shared = nullptr;
};

} // namespace moistr
