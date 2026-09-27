#include "Processor.h"

#include "Cids.h"
#include "State.h"

#include "pluginterfaces/vst/ivstparameterchanges.h"

#include <algorithm>
#include <vector>

namespace multidyn {

using namespace Steinberg;
using namespace Steinberg::Vst;

Processor::Processor ()
{
    setControllerClass (kControllerUID);
    meters = new Meters ();
    for (uint32_t id = 0; id < kNumParams; ++id)
        normMirror[id].store (defaultNormalized (id));
}

Processor::~Processor ()
{
    if (meters)
        meters->release ();
}

tresult PLUGIN_API Processor::initialize (FUnknown* context)
{
    const tresult r = AudioEffect::initialize (context);
    if (r != kResultOk)
        return r;
    addAudioInput (STR16 ("Stereo In"), SpeakerArr::kStereo);
    addAudioInput (STR16 ("Sidechain"), SpeakerArr::kStereo, kAux, 0); // inactive until the host routes it
    addAudioOutput (STR16 ("Stereo Out"), SpeakerArr::kStereo);
    return kResultOk;
}

tresult PLUGIN_API Processor::connect (IConnectionPoint* other)
{
    const tresult r = AudioEffect::connect (other);
    if (r == kResultOk && peerConnection)
        if (auto msg = owned (allocateMessage ()))
        {
            msg->setMessageID (kMeterMessageId);
            msg->getAttributes ()->setInt (kMeterAttr, (int64)(intptr_t)meters);
            sendMessage (msg);
        }
    return r;
}

tresult PLUGIN_API Processor::setBusArrangements (SpeakerArrangement* inputs, int32 numIns, SpeakerArrangement* outputs,
                                                 int32 numOuts)
{
    if (numOuts != 1 || outputs[0] != SpeakerArr::kStereo || numIns < 1 || numIns > 2 || inputs[0] != SpeakerArr::kStereo)
        return kResultFalse;
    if (numIns == 2 && inputs[1] != SpeakerArr::kStereo && inputs[1] != SpeakerArr::kMono)
        return kResultFalse;
    return AudioEffect::setBusArrangements (inputs, numIns, outputs, numOuts);
}

tresult PLUGIN_API Processor::canProcessSampleSize (int32 s) { return s == kSample32 ? kResultTrue : kResultFalse; }

tresult PLUGIN_API Processor::setupProcessing (ProcessSetup& setup)
{
    engine.prepare (setup.sampleRate, setup.maxSamplesPerBlock);
    for (uint32_t id = 0; id < kNumParams; ++id)
        engine.setParam (id, toPlain (id, normMirror[id].load ()));
    engine.reset ();
    return AudioEffect::setupProcessing (setup);
}

tresult PLUGIN_API Processor::setActive (TBool state)
{
    if (state)
    {
        for (uint32_t id = 0; id < kNumParams; ++id)
            engine.setParam (id, toPlain (id, normMirror[id].load ()));
        engine.reset ();
    }
    return AudioEffect::setActive (state);
}

tresult PLUGIN_API Processor::process (ProcessData& data)
{
    if (reloadParams.exchange (false, std::memory_order_acq_rel))
        for (uint32_t id = 0; id < kNumParams; ++id)
            engine.setParam (id, toPlain (id, normMirror[id].load (std::memory_order_relaxed)));

    if (auto* changes = data.inputParameterChanges)
        for (int32 i = 0; i < changes->getParameterCount (); ++i)
        {
            IParamValueQueue* q = changes->getParameterData (i);
            if (!q || q->getParameterId () >= kNumParams || q->getPointCount () <= 0)
                continue;
            int32 offset;
            ParamValue v;
            if (q->getPoint (q->getPointCount () - 1, offset, v) == kResultTrue)
            {
                const ParamID id = q->getParameterId ();
                normMirror[id].store (v);
                engine.setParam (id, toPlain (id, v));
            }
        }

    const int n = data.numSamples;
    if (n <= 0 || data.numInputs < 1 || data.numOutputs < 1 || data.inputs[0].numChannels < 2 ||
        data.outputs[0].numChannels < 2)
        return kResultOk;

    const float* inL = data.inputs[0].channelBuffers32[0];
    const float* inR = data.inputs[0].channelBuffers32[1];
    const float* scL = nullptr;
    const float* scR = nullptr;
    auto* scBus = getAudioInput (1);
    if (scBus && scBus->isActive () && data.numInputs > 1 && data.inputs[1].numChannels > 0 &&
        data.inputs[1].channelBuffers32 && data.inputs[1].channelBuffers32[0])
    {
        scL = data.inputs[1].channelBuffers32[0];
        scR = data.inputs[1].numChannels > 1 ? data.inputs[1].channelBuffers32[1] : scL;
    }
    meters->sidechainConnected.store (scL != nullptr, std::memory_order_relaxed);

    engine.process (inL, inR, scL, scR, data.outputs[0].channelBuffers32[0], data.outputs[0].channelBuffers32[1], n);
    data.outputs[0].silenceFlags = 0;

    for (int b = 0; b < kNumBands; ++b)
    {
        const auto& m = engine.meter (b);
        meters->inputDb[(size_t)b].store (m.inputDb, std::memory_order_relaxed);
        meters->outputDb[(size_t)b].store (m.outputDb, std::memory_order_relaxed);
        meters->gainDb[(size_t)b].store (m.gainDb, std::memory_order_relaxed);
    }
    return kResultOk;
}

tresult PLUGIN_API Processor::setState (IBStream* stream)
{
    if (!stream)
        return kInvalidArgument;
    State st;
    if (!readState (stream, st))
        return kResultFalse;
    for (uint32_t id = 0; id < kNumParams; ++id)
        normMirror[id].store (st.norm[id]);
    reloadParams.store (true, std::memory_order_release);
    return kResultOk;
}

tresult PLUGIN_API Processor::getState (IBStream* stream)
{
    if (!stream)
        return kInvalidArgument;
    State st;
    for (uint32_t id = 0; id < kNumParams; ++id)
    {
        st.norm[id] = normMirror[id].load ();
        st.has[id] = true;
    }
    return writeState (stream, st) ? kResultOk : kResultFalse;
}

} // namespace multidyn
