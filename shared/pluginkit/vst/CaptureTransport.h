// The host's transport for a capture buffer (pluginkit/Capture.h) from a VST3 process call: a processor
// pushes its output once a block with
//   shared->capture.push (outL, outR, data.numSamples, pk::captureTransport (data), processSetup.sampleRate);
#pragma once

#include "pluginkit/Capture.h"

#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"

namespace pk {

inline CaptureBuffer::Transport captureTransport (const Steinberg::Vst::ProcessData& data)
{
    using Steinberg::Vst::ProcessContext;
    CaptureBuffer::Transport t;
    if (const ProcessContext* c = data.processContext)
    {
        if ((c->state & ProcessContext::kTempoValid) && c->tempo > 0)
            t.bpm = c->tempo;
        t.playing = (c->state & ProcessContext::kPlaying) != 0;
        if (c->state & ProcessContext::kProjectTimeMusicValid)
        {
            t.ppq = c->projectTimeMusic;
            t.ppqValid = true;
        }
    }
    return t;
}

// The output of a block into a capture buffer: the main output bus's first two channels (one channel: both).
inline void captureOutput (CaptureBuffer& buf, const Steinberg::Vst::ProcessData& data, double sampleRate)
{
    if (data.numOutputs < 1 || data.numSamples <= 0 || data.outputs[0].numChannels < 1 || !data.outputs[0].channelBuffers32)
        return;
    const auto& bus = data.outputs[0];
    const float* l = bus.channelBuffers32[0];
    const float* r = bus.numChannels > 1 ? bus.channelBuffers32[1] : l;
    if (l && r)
        buf.push (l, r, data.numSamples, captureTransport (data), sampleRate);
}

} // namespace pk
