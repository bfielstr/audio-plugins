#include "Processor.h"

#include "Cids.h"
#include "State.h"

#include "pluginkit/vst/Presets.h"

#include "pluginterfaces/vst/ivstmessage.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"

#include <algorithm>

namespace detonatr {

using namespace Steinberg;
using namespace Steinberg::Vst;

Processor::Processor ()
{
    setControllerClass (kControllerUID);
    bridge = new Bridge ();
    engine.setMeters (&bridge->meters);
    for (uint32_t id = 0; id < kNumParams; ++id)
        normMirror[id].store (defaultNormalized (id));
    engine.prepare (48000.0, 512);
}

Processor::~Processor ()
{
    for (auto& c : localCarrier)
        c.reset ();
    if (bridge)
        bridge->release ();
}

tresult PLUGIN_API Processor::initialize (FUnknown* context)
{
    const tresult r = AudioEffect::initialize (context);
    if (r != kResultOk)
        return r;
    addAudioInput (STR16 ("Stereo In"), SpeakerArr::kStereo);
    addAudioOutput (STR16 ("Stereo Out"), SpeakerArr::kStereo);
    return kResultOk;
}

tresult PLUGIN_API Processor::connect (IConnectionPoint* other)
{
    const tresult r = AudioEffect::connect (other);
    if (r == kResultOk && peerConnection)
        if (auto msg = owned (allocateMessage ()))
        {
            msg->setMessageID (kBridgeMessageId);
            msg->getAttributes ()->setInt (kBridgeAttr, (int64)(intptr_t)bridge);
            sendMessage (msg);
        }
    return r;
}

tresult PLUGIN_API Processor::setBusArrangements (SpeakerArrangement* inputs, int32 numIns, SpeakerArrangement* outputs,
                                                 int32 numOuts)
{
    if (numIns == 1 && numOuts == 1 && inputs[0] == SpeakerArr::kStereo && outputs[0] == SpeakerArr::kStereo)
        return AudioEffect::setBusArrangements (inputs, numIns, outputs, numOuts);
    return kResultFalse;
}

tresult PLUGIN_API Processor::canProcessSampleSize (int32 s) { return s == kSample32 ? kResultTrue : kResultFalse; }

tresult PLUGIN_API Processor::setupProcessing (ProcessSetup& setup)
{
    for (uint32_t id = 0; id < kNumParams; ++id)
        engine.setParam (id, toPlain (id, normMirror[id].load ()));
    engine.prepare (setup.sampleRate, setup.maxSamplesPerBlock);
    sampleRate = setup.sampleRate;
    bridge->latency.store (engine.latency ());
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

uint32 PLUGIN_API Processor::getTailSamples ()
{
    // the resonators ring for up to their Decay (4 s at most), and Clean and Transient add their delays
    return (uint32)(engine.latency () + 4.5 * sampleRate);
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
            if (!q || q->getPointCount () <= 0)
                continue;
            int32 offset;
            ParamValue v;
            if (q->getPoint (q->getPointCount () - 1, offset, v) != kResultTrue)
                continue;
            const ParamID id = q->getParameterId ();
            if (id < kNumParams)
            {
                normMirror[id].store (v);
                engine.setParam (id, toPlain (id, v));
            }
        }
    // the recordings (a new one restarts its slot)
    for (int s = 0; s < kCarrierSlots; ++s)
        if (bridge->fetchCarrier (s, localCarrier[s], carrierGen[s]))
            engine.setCarrier (s, localCarrier[s].get ());

    const int n = data.numSamples;
    if (n <= 0 || data.numInputs < 1 || data.numOutputs < 1 || data.inputs[0].numChannels < 2 ||
        data.outputs[0].numChannels < 2)
        return kResultOk;
    engine.process (data.inputs[0].channelBuffers32[0], data.inputs[0].channelBuffers32[1], data.outputs[0].channelBuffers32[0],
                    data.outputs[0].channelBuffers32[1], n);
    data.outputs[0].silenceFlags = 0;
    bridge->latency.store (engine.latency (), std::memory_order_relaxed);
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
    if (st.hasRecordings)
        for (int s = 0; s < kCarrierSlots; ++s)
            bridge->setCarrier (s, st.recordings[(size_t)s].audio, st.recordings[(size_t)s].name);
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
    for (int s = 0; s < kCarrierSlots; ++s)
        st.recordings[(size_t)s] = {bridge->carrier (s), bridge->carrierName (s)};
    bridge->collectGarbage ();
    return writeState (stream, st) ? kResultOk : kResultFalse;
}

tresult PLUGIN_API Processor::notify (IMessage* message)
{
    if (pk::presets::handleProcessorMessage (*this, message))
        return kResultOk;
    return AudioEffect::notify (message);
}

} // namespace detonatr
