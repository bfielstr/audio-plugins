#pragma once

#include "Cids.h"
#include "GestureFile.h"
#include "SharedMeters.h"
#include "Params.h"

#include "pluginkit/vst/ControllerBase.h"

#include <array>
#include <memory>
#include <string>
#include <vector>

namespace moistr {

class Controller : public pk::ControllerBase
{
public:
    Controller () : pk::ControllerBase (paramTable ()) { setPresetInfo (kProcessorUID, "Moistr"); setGentlrIds (kGentlrIds); }
    static Steinberg::FUnknown* createInstance (void*) { return (Steinberg::Vst::IEditController*)new Controller (); }

    // a new instance: the Neuro recipe (newInstanceValues) unless the user saved a default
    Steinberg::tresult PLUGIN_API initialize (Steinberg::FUnknown* context) override;
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

    // The one gesture's user gesture (0.28): reads a file of lanes (GestureFile.h), sends it to the processor and
    // sets Gesture to User (false with a reason when the file is not one moistr can play); as the engine plays it
    // (nullptr: none) and as read
    bool loadUserScene (const std::string& path, std::string& error);
    void clearUserScene ();
    const Scene* userScene () const { return userSceneCurve.get (); }
    const SceneData& userSceneData () const { return userSceneFile; }
    // the Gestures folder, made when it is missing ("" when there is no preset folder)
    std::string makeGestureFolder () const;

    // a LAB slot's kind now (smemplr::FxType; Empty for a kind this build does not run)
    int labKind (int slot);

protected:
    void resetExtraState () override; // (Init and factory presets: no user gestures)
    // the LAB's slots' values shown in their kinds' units; the chains' kept-for-later parameters hidden
    Steinberg::Vst::Parameter* makeParameter (uint32_t id) override;

private:
    void sendUserGesture (int slot);
    void sendUserScene ();
    void setUserScene (SceneData d); // (and its curves for the display)
    SharedMeters* shared = nullptr;
    std::array<GestureData, kNumGestureSlots> user;
    SceneData userSceneFile;
    std::unique_ptr<Scene> userSceneCurve;
};

} // namespace moistr
