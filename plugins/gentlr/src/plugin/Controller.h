#pragma once

#include "Cids.h"
#include "Params.h"
#include "SharedMeters.h"

#include "pluginkit/vst/ControllerBase.h"

namespace gentlr {

class Controller : public pk::ControllerBase
{
public:
    Controller () : pk::ControllerBase (paramTable ()) { setPresetInfo (kProcessorUID, "Gentlr", "Gently"); /* renamed: its presets and settings come along */ }
    static Steinberg::FUnknown* createInstance (void*) { return (Steinberg::Vst::IEditController*)new Controller (); }

    Steinberg::tresult PLUGIN_API terminate () override;
    Steinberg::tresult PLUGIN_API setComponentState (Steinberg::IBStream* state) override;
    Steinberg::IPlugView* PLUGIN_API createView (Steinberg::FIDString name) override;
    Steinberg::tresult PLUGIN_API notify (Steinberg::Vst::IMessage* message) override;

    SharedMeters* getShared () const { return shared; }

private:
    SharedMeters* shared = nullptr;
};

} // namespace gentlr
