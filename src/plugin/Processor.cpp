#include "Processor.h"

#include "Cids.h"
#include "StateIO.h"

#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"

#include <algorithm>

namespace simplr {

using namespace Steinberg;
using namespace Steinberg::Vst;

Processor::Processor ()
{
    setControllerClass (kControllerUID);
    bridge = new Bridge ();
    for (uint32_t id = 0; id < kNumParams; ++id)
        normMirror[id].store (defaultNormalized (id));
}

Processor::~Processor ()
{
    if (bridge)
        bridge->release ();
}

tresult PLUGIN_API Processor::initialize (FUnknown* context)
{
    const tresult r = AudioEffect::initialize (context);
    if (r != kResultOk)
        return r;
    addAudioOutput (STR16 ("Stereo Out"), SpeakerArr::kStereo);
    addEventInput (STR16 ("Event In"), 1);
    return kResultOk;
}

tresult PLUGIN_API Processor::terminate () { return AudioEffect::terminate (); }

void Processor::sendBridge ()
{
    if (!peerConnection)
        return;
    if (auto msg = owned (allocateMessage ()))
    {
        msg->setMessageID (kBridgeMessageId);
        msg->getAttributes ()->setInt (kBridgeAttr, (int64)(intptr_t)bridge);
        sendMessage (msg);
    }
}

tresult PLUGIN_API Processor::connect (IConnectionPoint* other)
{
    const tresult r = AudioEffect::connect (other);
    if (r == kResultOk)
        sendBridge ();
    return r;
}

tresult PLUGIN_API Processor::setBusArrangements (SpeakerArrangement* inputs, int32 numIns,
                                                 SpeakerArrangement* outputs, int32 numOuts)
{
    if (numIns == 0 && numOuts == 1 && outputs[0] == SpeakerArr::kStereo)
        return AudioEffect::setBusArrangements (inputs, numIns, outputs, numOuts);
    return kResultFalse;
}

tresult PLUGIN_API Processor::canProcessSampleSize (int32 symbolicSampleSize)
{
    return symbolicSampleSize == kSample32 ? kResultTrue : kResultFalse;
}

tresult PLUGIN_API Processor::setupProcessing (ProcessSetup& setup)
{
    scratchL.assign ((size_t)std::max (1, setup.maxSamplesPerBlock), 0.0f);
    scratchR.assign ((size_t)std::max (1, setup.maxSamplesPerBlock), 0.0f);
    engine.prepare (setup.sampleRate, setup.maxSamplesPerBlock);
    for (uint32_t id = 0; id < kNumParams; ++id)
        engine.setParam (id, toPlain (id, normMirror[id].load ()));
    return AudioEffect::setupProcessing (setup);
}

tresult PLUGIN_API Processor::setActive (TBool state)
{
    if (state)
    {
        engine.prepare (processSetup.sampleRate, processSetup.maxSamplesPerBlock);
        for (uint32_t id = 0; id < kNumParams; ++id)
            engine.setParam (id, toPlain (id, normMirror[id].load ()));
    }
    else
        engine.reset ();
    return AudioEffect::setActive (state);
}

void Processor::handleParamChanges (IParameterChanges* changes, int numSamples)
{
    if (!changes)
        return;
    const int32 count = changes->getParameterCount ();
    for (int32 i = 0; i < count; ++i)
    {
        IParamValueQueue* q = changes->getParameterData (i);
        if (!q)
            continue;
        const ParamID id = q->getParameterId ();
        const int32 points = q->getPointCount ();
        if (points <= 0)
            continue;
        if (id < kNumParams)
        {
            int32 offset;
            ParamValue v;
            if (q->getPoint (points - 1, offset, v) == kResultTrue)
            {
                normMirror[id].store (v);
                engine.setParam (id, toPlain (id, v));
            }
        }
        else if (id == kMidiSustain || id == kMidiPitchBend)
        {
            for (int32 p = 0; p < points && numEvents < (int)events.size (); ++p)
            {
                int32 offset;
                ParamValue v;
                if (q->getPoint (p, offset, v) != kResultTrue)
                    continue;
                events[(size_t)numEvents++] = {std::clamp ((int)offset, 0, std::max (0, numSamples - 1)),
                                               id == kMidiSustain ? 2 : 3, 0, (float)v};
            }
        }
    }
}

tresult PLUGIN_API Processor::process (ProcessData& data)
{
    if (bridge->fetchSample (localSample, sampleGen))
        engine.setSample (localSample);
    if (bridge->fetchEdits (localEdits, editsGen))
        engine.setSliceEdits (localEdits);
    engine.setConstantPowerFade (bridge->constantPowerFade.load (std::memory_order_relaxed));

    if (reloadParams.exchange (false, std::memory_order_acq_rel))
        for (uint32_t id = 0; id < kNumParams; ++id)
            engine.setParam (id, toPlain (id, normMirror[id].load (std::memory_order_relaxed)));

    const int n = data.numSamples;
    numEvents = 0;
    handleParamChanges (data.inputParameterChanges, n);

    if (data.inputEvents)
    {
        const int32 count = data.inputEvents->getEventCount ();
        for (int32 i = 0; i < count && numEvents < (int)events.size (); ++i)
        {
            Event e {};
            if (data.inputEvents->getEvent (i, e) != kResultOk)
                continue;
            const int off = std::clamp ((int)e.sampleOffset, 0, std::max (0, n - 1));
            if (e.type == Event::kNoteOnEvent)
                events[(size_t)numEvents++] = {off, e.noteOn.velocity > 0.0f ? 0 : 1, e.noteOn.pitch, e.noteOn.velocity};
            else if (e.type == Event::kNoteOffEvent)
                events[(size_t)numEvents++] = {off, 1, e.noteOff.pitch, 0.0f};
        }
    }
    int pnote;
    float pvel;
    while (numEvents < (int)events.size () && bridge->popPreview (pnote, pvel))
        events[(size_t)numEvents++] = {0, pvel > 0.0f ? 0 : 1, pnote, pvel};

    // stable sort by offset
    for (int i = 1; i < numEvents; ++i)
    {
        const Ev e = events[(size_t)i];
        int j = i - 1;
        while (j >= 0 && events[(size_t)j].offset > e.offset)
        {
            events[(size_t)j + 1] = events[(size_t)j];
            --j;
        }
        events[(size_t)j + 1] = e;
    }

    HostInfo host;
    if (auto* ctx = data.processContext)
    {
        if (ctx->state & ProcessContext::kTempoValid)
            host.bpm = ctx->tempo;
        if (ctx->state & ProcessContext::kProjectTimeMusicValid)
        {
            host.ppq = ctx->projectTimeMusic;
            host.ppqValid = true;
        }
        host.playing = (ctx->state & ProcessContext::kPlaying) != 0;
    }
    if (host.bpm <= 0.0)
        host.bpm = 120.0;
    bridge->hostBpm.store (host.bpm, std::memory_order_relaxed);
    bridge->hostPlaying.store (host.playing, std::memory_order_relaxed);

    auto apply = [this] (const Ev& e) {
        switch (e.kind)
        {
            case 0: engine.noteOn (e.note, e.value); break;
            case 1: engine.noteOff (e.note); break;
            case 2: engine.setSustain (e.value >= 0.5f); break;
            default: engine.setPitchBend (e.value * 2.0f - 1.0f); break;
        }
    };

    const bool haveOut = data.numOutputs > 0 && data.outputs[0].numChannels >= 2 && n > 0;
    if (!haveOut)
    {
        for (int i = 0; i < numEvents; ++i)
            apply (events[(size_t)i]);
        return kResultOk;
    }

    float* L = data.outputs[0].channelBuffers32[0];
    float* R = data.outputs[0].channelBuffers32[1];
    const double ppqPerSample = host.bpm / 60.0 / processSetup.sampleRate;
    int pos = 0, ei = 0;
    while (pos < n)
    {
        while (ei < numEvents && events[(size_t)ei].offset <= pos)
            apply (events[(size_t)ei++]);
        const int next = ei < numEvents ? std::min (n, events[(size_t)ei].offset) : n;
        HostInfo h = host;
        h.ppq = host.ppq + pos * ppqPerSample;
        engine.render (L + pos, R + pos, next - pos, h);
        pos = next;
    }
    while (ei < numEvents)
        apply (events[(size_t)ei++]);

    float heads[Bridge::kMaxPlayheads];
    const int nh = engine.playPositions (heads, Bridge::kMaxPlayheads);
    for (int i = 0; i < nh; ++i)
        bridge->playheads[(size_t)i].store (heads[i], std::memory_order_relaxed);
    bridge->numPlayheads.store (nh, std::memory_order_release);

    data.outputs[0].silenceFlags = engine.activeVoices () == 0 ? 3 : 0;
    return kResultOk;
}

tresult PLUGIN_API Processor::setState (IBStream* stream)
{
    if (!stream)
        return kInvalidArgument;
    PluginState st;
    if (!readState (stream, st))
        return kResultFalse;
    for (uint32_t id = 0; id < kNumParams; ++id)
    {
        const double v = st.has[id] ? st.norm[id] : defaultNormalized (id);
        normMirror[id].store (v);
    }
    // The audio thread copies the mirror into the engine at the start of the next block.
    reloadParams.store (true, std::memory_order_release);
    bridge->constantPowerFade.store (st.constantPowerFade);
    bridge->setEdits (st.edits);
    if (!st.samplePath.empty ())
    {
        std::string err;
        bridge->loadSample (st.samplePath, st.ops, err);
    }
    else
        bridge->clearSample ();
    return kResultOk;
}

tresult PLUGIN_API Processor::getState (IBStream* stream)
{
    if (!stream)
        return kInvalidArgument;
    PluginState st;
    for (uint32_t id = 0; id < kNumParams; ++id)
    {
        st.norm[id] = normMirror[id].load ();
        st.has[id] = true;
    }
    st.samplePath = bridge->samplePath ();
    st.ops = bridge->sampleOps ();
    if (auto e = bridge->editsNow ())
        st.edits = *e;
    st.constantPowerFade = bridge->constantPowerFade.load ();
    bridge->collectGarbage ();
    return writeState (stream, st) ? kResultOk : kResultFalse;
}

} // namespace simplr
