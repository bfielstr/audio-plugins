#pragma once

#include "Bridge.h"
#include "Cids.h"
#include "Params.h"

#include "pluginkit/vst/ControllerBase.h"

#include <string>

namespace detonatr {

class Controller : public pk::ControllerBase
{
public:
    Controller () : pk::ControllerBase (paramTable ()) { setPresetInfo (kProcessorUID, "Detonatr"); }
    static Steinberg::FUnknown* createInstance (void*) { return (Steinberg::Vst::IEditController*)new Controller (); }

    Steinberg::tresult PLUGIN_API terminate () override;
    Steinberg::tresult PLUGIN_API setComponentState (Steinberg::IBStream* state) override;
    Steinberg::IPlugView* PLUGIN_API createView (Steinberg::FIDString name) override;
    Steinberg::tresult PLUGIN_API notify (Steinberg::Vst::IMessage* message) override;

    Bridge* getBridge () const { return bridge; }

    // The Tone stage's recordings (UI thread). The project keeps their audio.
    bool loadRecording (int slot, const std::string& path, std::string& error);
    void clearRecording (int slot);

private:
    Bridge* bridge = nullptr;
};

} // namespace detonatr
