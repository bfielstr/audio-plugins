#pragma once

#include "Engine.h"
#include "GestureFile.h"
#include "SharedMeters.h"

#include "pluginkit/RtShared.h"

#include "public.sdk/source/vst/vstaudioeffect.h"

#include <array>
#include <atomic>
#include <memory>
#include <mutex>

namespace moistr {

class Processor : public Steinberg::Vst::AudioEffect
{
public:
    Processor ();
    ~Processor () override;
    static Steinberg::FUnknown* createInstance (void*) { return (Steinberg::Vst::IAudioProcessor*)new Processor (); }

    Steinberg::tresult PLUGIN_API initialize (Steinberg::FUnknown* context) override;
    Steinberg::tresult PLUGIN_API connect (Steinberg::Vst::IConnectionPoint* other) override;
    Steinberg::tresult PLUGIN_API setBusArrangements (Steinberg::Vst::SpeakerArrangement* inputs, Steinberg::int32 numIns,
                                                      Steinberg::Vst::SpeakerArrangement* outputs,
                                                      Steinberg::int32 numOuts) override;
    Steinberg::tresult PLUGIN_API canProcessSampleSize (Steinberg::int32 symbolicSampleSize) override;
    Steinberg::tresult PLUGIN_API setupProcessing (Steinberg::Vst::ProcessSetup& setup) override;
    Steinberg::tresult PLUGIN_API setActive (Steinberg::TBool state) override;
    Steinberg::tresult PLUGIN_API process (Steinberg::Vst::ProcessData& data) override;
    Steinberg::tresult PLUGIN_API setState (Steinberg::IBStream* state) override;
    Steinberg::tresult PLUGIN_API getState (Steinberg::IBStream* state) override;
    Steinberg::tresult PLUGIN_API notify (Steinberg::Vst::IMessage* message) override;
    Steinberg::uint32 PLUGIN_API getLatencySamples () override { return (Steinberg::uint32)engine.latency (); }

private:
    // the slots' user gestures: as saved (under userMutex) and as the engine plays them (immutable copies,
    // published to the audio thread)
    struct UserBank
    {
        Gesture g[kNumGestureSlots];
        bool has[kNumGestureSlots] {};
        Scene scene; // the one gesture's (0.28)
        bool hasScene = false;
    };
    void publishUser ();
    std::mutex userMutex;
    std::array<GestureData, kNumGestureSlots> userData;
    SceneData sceneData;
    pk::RtShared<UserBank> userBank;
    std::shared_ptr<const UserBank> userNow; // (the audio thread's)
    uint32_t userGen = 0;

    Engine engine;
    SharedMeters* shared = nullptr;
    std::array<std::atomic<double>, kNumParams> normMirror;
    std::atomic<bool> reloadParams {false};
};

} // namespace moistr
