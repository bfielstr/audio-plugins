#include "Processor.h"

#include "pluginkit/vst/Presets.h"

#include "pluginterfaces/vst/ivstmessage.h"

#include "Cids.h"
#include "State.h"

#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"

#include <cmath>

namespace stretchr {

using namespace Steinberg;
using namespace Steinberg::Vst;

Processor::Processor ()
{
    setControllerClass (kControllerUID);
    session = new Session ();
    for (uint32_t id = 0; id < kNumParams; ++id)
        normMirror[id].store (defaultNormalized (id));
}

Processor::~Processor ()
{
    cur.reset ();
    prev.reset ();
    if (session)
        session->release ();
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
            msg->setMessageID (kSessionMessageId);
            msg->getAttributes ()->setInt (kSessionAttr, (int64)(intptr_t)session);
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

void Processor::syncTail ()
{
    for (uint32_t id = 0; id < kNumParams; ++id)
        if (isTailParam (id))
            tail.setParam (tailField (id), toPlain (id, normMirror[id].load ()));
}

tresult PLUGIN_API Processor::setupProcessing (ProcessSetup& setup)
{
    session->hostRate.store (setup.sampleRate);
    tail.prepare (setup.sampleRate, setup.maxSamplesPerBlock);
    tail.setMeters (&session->tailMeters);
    syncTail ();
    return AudioEffect::setupProcessing (setup);
}

tresult PLUGIN_API Processor::setActive (TBool state)
{
    cur.reset ();
    prev.reset ();
    renderGen = 0;
    fade = 0;
    syncTail ();
    tail.reset ();
    return AudioEffect::setActive (state);
}

tresult PLUGIN_API Processor::process (ProcessData& data)
{
    session->processBegin ();
    struct EndGuard
    {
        Session* s;
        ~EndGuard () { s->processEnd (); }
    } endGuard {session};

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
                const uint32_t id = q->getParameterId ();
                normMirror[id].store (v);
                if (isTailParam (id))
                    tail.setParam (tailField (id), toPlain (id, v));
                else
                    session->setParam (id, toPlain (id, v), false);
            }
        }

    const int n = data.numSamples;
    if (n <= 0 || data.numInputs < 1 || data.numOutputs < 1 || data.inputs[0].numChannels < 2 ||
        data.outputs[0].numChannels < 2)
        return kResultOk;

    const double sr = processSetup.sampleRate;
    const ProcessContext* ctx = data.processContext;
    const bool offline = processSetup.processMode == kOffline;
    const bool playing = ctx && ((ctx->state & ProcessContext::kPlaying) || offline);
    const long long pos = ctx ? ctx->projectTimeSamples : 0;
    if (ctx)
    {
        session->transport.store ((double)pos / sr, std::memory_order_relaxed);
        if (ctx->state & ProcessContext::kTempoValid)
            session->hostBpm.store (ctx->tempo, std::memory_order_relaxed);
    }
    session->playing.store (playing, std::memory_order_relaxed);
    // On Play: the clip starts the moment the host starts playing (from wherever the playhead is)
    if (playing && !wasPlaying)
        playAnchor = pos;
    wasPlaying = playing;
    const bool onPlay = std::lround (session->param (kTrigger)) == kOnPlay;
    session->playOffset.store (onPlay ? (double)(pos - playAnchor) / sr : -1.0, std::memory_order_relaxed);

    const float* inL = data.inputs[0].channelBuffers32[0];
    const float* inR = data.inputs[0].channelBuffers32[1];
    float* outL = data.outputs[0].channelBuffers32[0];
    float* outR = data.outputs[0].channelBuffers32[1];
    session->captureBlock (inL, inR, n, pos, playing, sr);
    const bool capturing = session->capturing ();
    const bool hasClip = session->hasClip ();

    // Bouncing/freezing: the render must match the current settings before audio goes out.
    if (offline && hasClip && !capturing && !session->upToDate ())
        session->waitUntilRendered (60.0);
    {
        RenderedPtr before = cur;
        if (session->fetch (cur, renderGen))
        {
            prev = before;
            fade = before && cur && before != cur ? kFade : 0;
        }
    }

    const bool clipOn = playing && !capturing && hasClip && cur;
    const bool muteOutside = hasClip && session->param (kOutside) >= 0.5;
    // where the clip starts: at the moment playback started (On Play) or where it sits (Timeline)
    const long long startS = onPlay ? playAnchor : (long long)std::llround (session->clipStart () * sr);
    const long long len = cur ? cur->length () : 0;
    const long long span = cur ? std::max (len, (long long)std::llround (cur->srcSeconds * sr)) : 0;
    const Rendered* p = fade > 0 && prev ? prev.get () : nullptr;
    const long long plen = p ? p->length () : 0;
    for (int i = 0; i < n; ++i)
    {
        const float l = inL[i], r = inR[i];
        if (!playing || capturing)
        {
            outL[i] = l;
            outR[i] = r;
            continue;
        }
        const long long rel = pos + i - startS;
        if (clipOn && rel >= 0 && rel < span)
        {
            float a = rel < len ? cur->l[(size_t)rel] : 0.0f;
            float b = rel < len ? cur->r[(size_t)rel] : 0.0f;
            if (p && fade > 0)
            {
                const float x = (float)fade / (float)kFade;
                const float pa = rel < plen ? p->l[(size_t)rel] : 0.0f;
                const float pb = rel < plen ? p->r[(size_t)rel] : 0.0f;
                a = a * (1.0f - x) + pa * x;
                b = b * (1.0f - x) + pb * x;
            }
            outL[i] = a;
            outR[i] = b;
        }
        else
        {
            outL[i] = muteOutside ? 0.0f : l;
            outR[i] = muteOutside ? 0.0f : r;
        }
        if (fade > 0)
            --fade;
    }
    if (fade <= 0 && prev)
        prev.reset (); // still referenced by the session's graveyard: never frees here
    tail.process (outL, outR, n);
    data.outputs[0].silenceFlags = 0;
    return kResultOk;
}

tresult PLUGIN_API Processor::setState (IBStream* stream)
{
    if (!stream)
        return kInvalidArgument;
    State st;
    if (!readState (stream, st, true))
        return kResultFalse;
    for (uint32_t id = 0; id < kNumParams; ++id)
    {
        normMirror[id].store (st.norm[id]);
        if (!isTailParam (id))
            session->setParam (id, toPlain (id, st.norm[id]));
    }
    session->setClip (st.hasClip ? std::move (st.clip) : Clip {}, false);
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
    st.clip = session->clip ();
    st.hasClip = !st.clip.empty ();
    return writeState (stream, st) ? kResultOk : kResultFalse;
}

tresult PLUGIN_API Processor::notify (IMessage* message)
{
    if (pk::presets::handleProcessorMessage (*this, message))
        return kResultOk;
    return AudioEffect::notify (message);
}

} // namespace stretchr
