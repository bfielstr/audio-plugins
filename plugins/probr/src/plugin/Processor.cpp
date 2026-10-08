#include "Processor.h"

#include "Cids.h"
#include "State.h"

#include "pluginkit/vst/Presets.h"

#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstmessage.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"

#include <cmath>
#include <cstring>

#ifndef PROBR_VERSION
#define PROBR_VERSION "0"
#endif

namespace probr {

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace {
std::string binaryText (IAttributeList* a, const char* id)
{
    const void* data = nullptr;
    uint32 size = 0;
    if (!a || a->getBinary (id, data, size) != kResultOk || !data)
        return {};
    return std::string ((const char*)data, size);
}
} // namespace

Processor::Processor ()
{
    setControllerClass (kControllerUID);
    shared = new Shared ();
    shared->settings.set (kDefaultLabel, "");
    probe.setStatus (&shared->status);
    for (uint32_t id = 0; id < kNumParams; ++id)
        normMirror[id].store (defaultNormalized (id));
    disk = makeDiskFileSystem ();
    probe.prepare (48000.0, 512);
    writer = std::make_unique<Writer> (probe.ring (), shared->status, shared->settings, *disk, std::string ("probr ") + PROBR_VERSION);
}

Processor::~Processor ()
{
    if (writer)
    {
        probe.endTake (kEndDeactivated);
        writer->stop ();
    }
    writer.reset ();
    if (shared)
        shared->release ();
}

tresult PLUGIN_API Processor::initialize (FUnknown* context)
{
    const tresult r = AudioEffect::initialize (context);
    if (r != kResultOk)
        return r;
    addAudioInput (STR16 ("Stereo In"), SpeakerArr::kStereo);
    addAudioOutput (STR16 ("Stereo Out"), SpeakerArr::kStereo);
    // optional: notes and pitch bend routed here are written beside the audio
    addEventInput (STR16 ("MIDI In"), 16);
    // Save as Default (a project's setState comes after)
    pk::presets::applyDefault (*this, kProcessorUID, "Probr");
    return kResultOk;
}

tresult PLUGIN_API Processor::terminate ()
{
    probe.endTake (kEndDeactivated);
    writer->stop ();
    return AudioEffect::terminate ();
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

tresult PLUGIN_API Processor::setBusArrangements (SpeakerArrangement* inputs, int32 numIns, SpeakerArrangement* outputs, int32 numOuts)
{
    if (numIns == 1 && numOuts == 1 && inputs[0] == SpeakerArr::kStereo && outputs[0] == SpeakerArr::kStereo)
        return AudioEffect::setBusArrangements (inputs, numIns, outputs, numOuts);
    return kResultFalse;
}

tresult PLUGIN_API Processor::canProcessSampleSize (int32 s) { return s == kSample32 ? kResultTrue : kResultFalse; }

tresult PLUGIN_API Processor::setupProcessing (ProcessSetup& setup)
{
    // (the ring is made again for the new rate: nothing may use it meanwhile)
    probe.endTake (kEndSampleRate);
    writer->stop ();
    probe.prepare (setup.sampleRate, setup.maxSamplesPerBlock);
    applyParams ();
    return AudioEffect::setupProcessing (setup);
}

void Processor::applyParams ()
{
    probe.setRecord (std::lround (toPlain (kRecord, normMirror[kRecord].load (std::memory_order_relaxed))) == kRecordArmed);
    probe.setMode ((int)std::lround (toPlain (kMode, normMirror[kMode].load (std::memory_order_relaxed))));
}

tresult PLUGIN_API Processor::setActive (TBool state)
{
    if (state)
    {
        applyParams ();
        writer->start ();
    }
    else
    {
        // the take ends with the processing; what is left in the ring is written before this returns
        probe.endTake (kEndDeactivated);
        writer->stop ();
    }
    return AudioEffect::setActive (state);
}

tresult PLUGIN_API Processor::process (ProcessData& data)
{
    if (reloadParams.exchange (false, std::memory_order_acq_rel))
        applyParams ();
    int numMidi = 0;
    if (auto* changes = data.inputParameterChanges)
        for (int32 i = 0; i < changes->getParameterCount (); ++i)
        {
            IParamValueQueue* q = changes->getParameterData (i);
            if (!q || q->getPointCount () <= 0)
                continue;
            const ParamID id = q->getParameterId ();
            int32 offset;
            ParamValue v;
            if (id >= kMidiPitchBend && id < kMidiPitchBend + 16)
            {
                // pitch bend (IMidiMapping), every point at its sample
                for (int32 p = 0; p < q->getPointCount () && numMidi < (int)midiBuf.size (); ++p)
                    if (q->getPoint (p, offset, v) == kResultTrue)
                    {
                        MidiRec& m = midiBuf[(size_t)numMidi++];
                        m = MidiRec {};
                        m.sample = offset;
                        m.kind = kMidiBend;
                        m.channel = (int32_t)(id - kMidiPitchBend);
                        m.value = (float)(v * 2.0 - 1.0);
                    }
                continue;
            }
            if (id >= kNumParams)
                continue;
            if (q->getPoint (q->getPointCount () - 1, offset, v) == kResultTrue)
            {
                normMirror[id].store (v, std::memory_order_relaxed);
                applyParams ();
            }
        }
    if (auto* events = data.inputEvents)
    {
        Event e {};
        for (int32 i = 0; i < events->getEventCount () && numMidi < (int)midiBuf.size (); ++i)
        {
            if (events->getEvent (i, e) != kResultOk)
                continue;
            if (e.type != Event::kNoteOnEvent && e.type != Event::kNoteOffEvent)
                continue;
            MidiRec& m = midiBuf[(size_t)numMidi++];
            m = MidiRec {};
            m.sample = e.sampleOffset;
            if (e.type == Event::kNoteOnEvent)
            {
                m.kind = e.noteOn.velocity > 0.0f ? kMidiNoteOn : kMidiNoteOff; // (a note on at velocity 0 is a note off)
                m.channel = e.noteOn.channel;
                m.pitch = e.noteOn.pitch;
                m.value = e.noteOn.velocity;
            }
            else
            {
                m.kind = kMidiNoteOff;
                m.channel = e.noteOff.channel;
                m.pitch = e.noteOff.pitch;
                m.value = e.noteOff.velocity;
            }
        }
    }

    const int n = data.numSamples;
    if (n <= 0 || data.numInputs < 1 || data.numOutputs < 1 || data.inputs[0].numChannels < 2 || data.outputs[0].numChannels < 2 ||
        !data.inputs[0].channelBuffers32 || !data.outputs[0].channelBuffers32)
        return kResultOk;

    Transport t;
    if (const ProcessContext* c = data.processContext)
    {
        t.flags |= Transport::kValid | Transport::kSamplesValid;
        t.projectSample = c->projectTimeSamples;
        if (c->state & ProcessContext::kPlaying)
            t.flags |= Transport::kPlaying;
        if (c->state & ProcessContext::kProjectTimeMusicValid)
        {
            t.flags |= Transport::kPpqValid;
            t.ppq = c->projectTimeMusic;
        }
        if (c->state & ProcessContext::kBarPositionValid)
        {
            t.flags |= Transport::kBarValid;
            t.barStart = c->barPositionMusic;
        }
        if (c->state & ProcessContext::kTempoValid)
        {
            t.flags |= Transport::kTempoValid;
            t.tempo = c->tempo;
        }
        if (c->state & ProcessContext::kTimeSigValid)
        {
            t.flags |= Transport::kSigValid;
            t.sigNum = c->timeSigNumerator;
            t.sigDen = c->timeSigDenominator;
        }
    }
    probe.process (data.inputs[0].channelBuffers32[0], data.inputs[0].channelBuffers32[1], data.outputs[0].channelBuffers32[0],
                   data.outputs[0].channelBuffers32[1], n, t, midiBuf.data (), numMidi);
    data.outputs[0].silenceFlags = data.inputs[0].silenceFlags;
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
        if (id != kRecord) // (Record is not saved: it stays as it is)
            normMirror[id].store (st.norm[id]);
    shared->settings.set (st.label, st.folder);
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
        st.has[id] = id != kRecord;
    }
    shared->settings.get (st.label, st.folder);
    return writeState (stream, st) ? kResultOk : kResultFalse;
}

tresult PLUGIN_API Processor::notify (IMessage* message)
{
    if (message && message->getMessageID () && std::strcmp (message->getMessageID (), kTextMessageId) == 0)
    {
        // (the writer takes them at the next take's start)
        shared->settings.set (binaryText (message->getAttributes (), kLabelAttr), binaryText (message->getAttributes (), kFolderAttr));
        return kResultOk;
    }
    if (pk::presets::handleProcessorMessage (*this, message))
        return kResultOk;
    return AudioEffect::notify (message);
}

} // namespace probr
