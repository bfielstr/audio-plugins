#pragma once

#include "Session.h"

#include "smacheratr/src/core/Tail.h"

#include "public.sdk/source/vst/vstaudioeffect.h"

#include <array>
#include <atomic>

namespace stretchr {

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
    Steinberg::uint32 PLUGIN_API getLatencySamples () override { return (Steinberg::uint32)tail.latency (); }
    Steinberg::tresult PLUGIN_API setState (Steinberg::IBStream* state) override;
    Steinberg::tresult PLUGIN_API getState (Steinberg::IBStream* state) override;
    Steinberg::tresult PLUGIN_API notify (Steinberg::Vst::IMessage* message) override;

private:
    Session* session = nullptr;
    smacheratr::Tail tail; // the end-of-chain saturator, after the clip
    void syncTail ();
    std::array<std::atomic<double>, kNumParams> normMirror;
    RenderedPtr cur, prev;
    uint32_t renderGen = 0;
    int fade = 0;
    static constexpr int kFade = 1024;
};

} // namespace stretchr
