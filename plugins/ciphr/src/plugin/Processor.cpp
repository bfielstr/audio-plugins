#include "Processor.h"

#include "pluginkit/vst/Presets.h"

#include "Cids.h"
#include "State.h"

#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstmessage.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"

#include <algorithm>

namespace ciphr {

using namespace Steinberg;
using namespace Steinberg::Vst;

Processor::Processor ()
{
    setControllerClass (kControllerUID);
    shared = new SharedMeters ();
    engine.setMeters (&shared->meters);
    engine.setTailMeters (&shared->tailMeters);
    for (uint32_t id = 0; id < kNumParams; ++id)
        normMirror[id].store (defaultNormalized (id));
    engine.prepare (48000.0, 512);
}

Processor::~Processor ()
{
    if (shared)
        shared->release ();
}

tresult PLUGIN_API Processor::initialize (FUnknown* context)
{
    const tresult r = AudioEffect::initialize (context);
    if (r != kResultOk)
        return r;
    // the stereo input (Input, Input Path): a side-chain bus, so hosts let any track feed it
    addAudioInput (STR16 ("Input"), SpeakerArr::kStereo, kAux, BusInfo::kDefaultActive);
    addAudioOutput (STR16 ("Stereo Out"), SpeakerArr::kStereo);
    addEventInput (STR16 ("Event In"), 1);
    // Save as Default, then Menu > Defaults (a project's setState comes after)
    pk::presets::applyDefault (*this, kProcessorUID, "Ciphr", kGentlrIds, [this] (uint32_t id, double n) { normMirror[id].store (n); });
    return kResultOk;
}

tresult PLUGIN_API Processor::connect (IConnectionPoint* other)
{
    const tresult r = AudioEffect::connect (other);
    if (r == kResultOk && peerConnection)
        if (auto msg = owned (allocateMessage ()))
        {
            msg->setMessageID (kSharedMessageId);
            msg->getAttributes ()->setInt (kSharedAttr, (int64)(intptr_t)shared);
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
    shared->latency.store (engine.latency ());
    shared->sampleRate.store (setup.sampleRate);
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
                normMirror[q->getParameterId ()].store (v);
                engine.setParam (q->getParameterId (), toPlain (q->getParameterId (), v));
            }
        }

    const int n = data.numSamples;
    // the notes, in order of their offsets
    int count = 0;
    if (data.inputEvents)
    {
        const int32 total = data.inputEvents->getEventCount ();
        for (int32 i = 0; i < total && count < (int)events.size (); ++i)
        {
            Event e {};
            if (data.inputEvents->getEvent (i, e) != kResultOk)
                continue;
            const int off = std::clamp ((int)e.sampleOffset, 0, std::max (0, n - 1));
            if (e.type == Event::kNoteOnEvent)
                events[(size_t)count++] = {off, e.noteOn.velocity > 0.0f, e.noteOn.pitch, e.noteOn.velocity};
            else if (e.type == Event::kNoteOffEvent)
                events[(size_t)count++] = {off, false, e.noteOff.pitch, 0.0f};
        }
    }
    std::stable_sort (events.begin (), events.begin () + count, [] (const Ev& a, const Ev& b) { return a.offset < b.offset; });
    auto apply = [this] (const Ev& e) {
        if (e.on)
            engine.noteOn (e.note, e.velocity);
        else
            engine.noteOff (e.note);
    };

    if (n <= 0 || data.numOutputs < 1 || data.outputs[0].numChannels < 2)
    {
        for (int i = 0; i < count; ++i)
            apply (events[(size_t)i]);
        return kResultOk;
    }
    const float* inL = nullptr;
    const float* inR = nullptr;
    if (data.numInputs >= 1 && data.inputs[0].numChannels >= 2 && data.inputs[0].channelBuffers32)
    {
        inL = data.inputs[0].channelBuffers32[0];
        inR = data.inputs[0].channelBuffers32[1];
    }
    float* L = data.outputs[0].channelBuffers32[0];
    float* R = data.outputs[0].channelBuffers32[1];
    // render between the events (a note starts on its sample)
    int pos = 0, ei = 0;
    while (pos < n)
    {
        while (ei < count && events[(size_t)ei].offset <= pos)
            apply (events[(size_t)ei++]);
        const int next = ei < count ? std::min (n, events[(size_t)ei].offset) : n;
        engine.process (inL ? inL + pos : nullptr, inR ? inR + pos : nullptr, L + pos, R + pos, next - pos);
        pos = next;
    }
    while (ei < count)
        apply (events[(size_t)ei++]);
    bool silent = true;
    for (int i = 0; i < n && silent; ++i)
        silent = L[i] == 0.0f && R[i] == 0.0f;
    data.outputs[0].silenceFlags = silent ? 3 : 0;
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
    reloadParams.store (true, std::memory_order_release); // (the audio thread takes them with its next block)
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

tresult PLUGIN_API Processor::notify (IMessage* message)
{
    if (pk::presets::handleProcessorMessage (*this, message))
        return kResultOk;
    return AudioEffect::notify (message);
}

} // namespace ciphr
