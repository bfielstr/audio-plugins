#pragma once

#include "Cids.h"
#include "Bridge.h"
#include "Params.h"

#include "pluginkit/vst/ControllerBase.h"
#include "pluginterfaces/vst/ivstmidicontrollers.h"

#include <functional>
#include <vector>

namespace smemplr {

class Editor;

class Controller : public pk::ControllerBase, public Steinberg::Vst::IMidiMapping
{
public:
    Controller () : pk::ControllerBase (paramTable ()) { setPresetInfo (kProcessorUID, "Smemplr", "Smempler"); /* renamed: its presets come along */ }

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


    // The rack: a slot's current effect; when it changes, the slot's parameters take the effect's
    // names (the host is told), and values show in the effect's units.
    Steinberg::tresult PLUGIN_API setParamNormalized (Steinberg::Vst::ParamID tag, Steinberg::Vst::ParamValue value) override;
    int slotType (int slot);
    // Tells the host when the latency changed (effects with latency loaded into the rack or taken
    // out); the editor calls it while open.
    void checkLatency ();

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

protected:
    Steinberg::Vst::Parameter* makeParameter (uint32_t id) override;
    // Init and factory presets: no LFO mappings, as in a new smemplr (the loaded sample stays)
    void resetExtraState () override
    {
        if (bridge)
            bridge->setMods ({});
    }
    bool isSetting (uint32_t id) const override { return !isMidiParam (id); } // (not reset, copied or pasted)

private:
    void retitleSlot (int slot);

    Bridge* bridge = nullptr;
    int reportedLatency = -1;
    std::array<int, kRackSlots> titledType {};
    std::string pendingPath; // from setComponentState when no bridge is connected
};

} // namespace smemplr
