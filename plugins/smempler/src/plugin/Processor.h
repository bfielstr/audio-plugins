#pragma once

#include "Bridge.h"
#include "Engine.h"

#include "public.sdk/source/vst/vstaudioeffect.h"

#include <array>
#include <atomic>

namespace smempler {

class Processor : public Steinberg::Vst::AudioEffect
{
public:
    Processor ();
    ~Processor () override;

    static Steinberg::FUnknown* createInstance (void*)
    {
        return (Steinberg::Vst::IAudioProcessor*)new Processor ();
    }

    Steinberg::tresult PLUGIN_API initialize (Steinberg::FUnknown* context) override;
    Steinberg::tresult PLUGIN_API terminate () override;
    Steinberg::tresult PLUGIN_API connect (Steinberg::Vst::IConnectionPoint* other) override;
    Steinberg::tresult PLUGIN_API setBusArrangements (Steinberg::Vst::SpeakerArrangement* inputs,
                                                      Steinberg::int32 numIns,
                                                      Steinberg::Vst::SpeakerArrangement* outputs,
                                                      Steinberg::int32 numOuts) override;
    Steinberg::tresult PLUGIN_API canProcessSampleSize (Steinberg::int32 symbolicSampleSize) override;
    Steinberg::tresult PLUGIN_API setupProcessing (Steinberg::Vst::ProcessSetup& setup) override;
    Steinberg::tresult PLUGIN_API setActive (Steinberg::TBool state) override;
    Steinberg::tresult PLUGIN_API process (Steinberg::Vst::ProcessData& data) override;
    Steinberg::tresult PLUGIN_API setState (Steinberg::IBStream* state) override;
    Steinberg::tresult PLUGIN_API getState (Steinberg::IBStream* state) override;
    Steinberg::tresult PLUGIN_API notify (Steinberg::Vst::IMessage* message) override;
    Steinberg::uint32 PLUGIN_API getTailSamples () override { return Steinberg::Vst::kInfiniteTail; }

private:
    struct Ev
    {
        int offset;
        int kind; // 0 note on, 1 note off, 2 sustain, 3 bend
        int note;
        float value;
    };
    void handleParamChanges (Steinberg::Vst::IParameterChanges* changes, int numSamples);
    void sendBridge ();

    Bridge* bridge = nullptr;
    Engine engine;
    std::array<std::atomic<double>, kNumParams> normMirror;
    std::atomic<bool> reloadParams {false};
    SamplePtr localSample;
    uint32_t sampleGen = 0;
    SliceEditsPtr localEdits;
    uint32_t editsGen = 0;
    std::array<Ev, 2048> events {};
    int numEvents = 0;
    std::vector<float> scratchL, scratchR;
};

} // namespace smempler
