#pragma once

#include "Cids.h"
#include "Params.h"
#include "Shared.h"

#include "pluginkit/vst/ControllerBase.h"

#include "pluginterfaces/vst/ivstmidicontrollers.h"

#include <string>

namespace probr {

class Controller : public pk::ControllerBase, public Steinberg::Vst::IMidiMapping
{
public:
    Controller () : pk::ControllerBase (paramTable ()) { setPresetInfo (kProcessorUID, "Probr"); }
    static Steinberg::FUnknown* createInstance (void*) { return (Steinberg::Vst::IEditController*)new Controller (); }

    Steinberg::tresult PLUGIN_API initialize (Steinberg::FUnknown* context) override;
    Steinberg::tresult PLUGIN_API terminate () override;
    Steinberg::tresult PLUGIN_API setComponentState (Steinberg::IBStream* state) override;
    Steinberg::IPlugView* PLUGIN_API createView (Steinberg::FIDString name) override;
    Steinberg::tresult PLUGIN_API notify (Steinberg::Vst::IMessage* message) override;
    Steinberg::tresult PLUGIN_API getMidiControllerAssignment (Steinberg::int32 busIndex, Steinberg::int16 channel,
                                                               Steinberg::Vst::CtrlNumber midiControllerNumber,
                                                               Steinberg::Vst::ParamID& id) override;

    Shared* getShared () const { return shared; }

    // The Label and the Folder ("" the default one), as the editor shows and changes them: a change goes
    // to the processor (the next take uses it) and marks the project changed.
    const std::string& label () const { return labelText; }
    const std::string& folder () const { return folderText; }
    void setLabel (const std::string& l);
    void setFolder (const std::string& f);
    // the folder the takes go to now (the default one when Folder is "")
    std::string folderShown () const;

    OBJ_METHODS (Controller, pk::ControllerBase)
    DEFINE_INTERFACES
        DEF_INTERFACE (IMidiMapping)
    END_DEFINE_INTERFACES (pk::ControllerBase)
    REFCOUNT_METHODS (pk::ControllerBase)

private:
    void sendText ();

    Shared* shared = nullptr;
    std::string labelText, folderText;
};

} // namespace probr
