#pragma once

#include "SharedSpectrum.h"
#include "Params.h"

#include "pluginkit/vst/ControllerBase.h"

namespace locus {

class Controller : public pk::ControllerBase
{
public:
    Controller () : pk::ControllerBase (paramTable ()) {}
    static Steinberg::FUnknown* createInstance (void*) { return (Steinberg::Vst::IEditController*)new Controller (); }

    Steinberg::tresult PLUGIN_API terminate () override;
    Steinberg::tresult PLUGIN_API setComponentState (Steinberg::IBStream* state) override;
    Steinberg::IPlugView* PLUGIN_API createView (Steinberg::FIDString name) override;
    Steinberg::tresult PLUGIN_API notify (Steinberg::Vst::IMessage* message) override;

    SharedSpectrum* getShared () const { return shared; }

private:
    SharedSpectrum* shared = nullptr;
};

} // namespace locus
