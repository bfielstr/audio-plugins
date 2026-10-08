#pragma once

#include "Cids.h"
#include "GestureFile.h"
#include "SharedMeters.h"
#include "Params.h"

#include "pluginkit/vst/ControllerBase.h"

#include <array>
#include <string>
#include <vector>

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

    // The user gestures: the JSON files in <preset folder>/Gestures (sorted by name), and per slot the one it
    // has (from a file or a saved state; empty: none) as the processor plays it.
    struct GestureFileItem
    {
        std::string name, path;
    };
    std::string gestureFolder () const;
    std::vector<GestureFileItem> gestureFiles () const;
    // reads a file into a slot, sends it to the processor and sets the slot's Gesture to User (false with a
    // reason when the file is not a gesture)
    bool loadUserGesture (int slot, const std::string& path, std::string& error);
    void clearUserGesture (int slot);
    const GestureData& userGesture (int slot) const { return user[(size_t)slot]; }

protected:
    void resetExtraState () override; // (Init and factory presets: no user gestures)

private:
    void sendUserGesture (int slot);
    SharedMeters* shared = nullptr;
    std::array<GestureData, kNumGestureSlots> user;
};

} // namespace moistr
