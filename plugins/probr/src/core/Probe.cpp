#include "Probe.h"

#include "Params.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace probr {

const char* endReasonText (int32_t reason)
{
    switch (reason)
    {
        case kEndDisarmed: return "record off";
        case kEndStopped: return "transport stopped";
        case kEndDeactivated: return "deactivated";
        case kEndOverrun: return "writer fell behind";
        case kEndDiskFull: return "disk full";
        case kEndWriteError: return "write error";
        case kEndSizeLimit: return "size limit";
        case kEndSampleRate: return "sample rate changed";
        default: return "unknown";
    }
}

namespace {

constexpr uint32_t kCompared = Transport::kValid | Transport::kPlaying | Transport::kPpqValid | Transport::kBarValid | Transport::kTempoValid |
                               Transport::kSigValid | Transport::kSamplesValid;

// `p` moved on by `d` samples by the time map's rule
TimePoint advance (const TimePoint& p, int64_t d, double sr)
{
    TimePoint q = p;
    q.sample = p.sample + d;
    if (!p.t.playing ())
        return q;
    if (p.t.has (Transport::kPpqValid) && p.t.has (Transport::kTempoValid))
    {
        q.t.ppq = p.t.ppq + (double)d / sr * p.t.tempo / 60.0;
        if (p.t.has (Transport::kBarValid) && p.t.has (Transport::kSigValid) && p.t.sigNum > 0 && p.t.sigDen > 0)
        {
            const double bar = 4.0 * p.t.sigNum / p.t.sigDen;
            if (q.t.ppq >= q.t.barStart + bar)
                q.t.barStart += bar * std::floor ((q.t.ppq - q.t.barStart) / bar + 1e-9);
        }
    }
    if (p.t.has (Transport::kSamplesValid))
        q.t.projectSample = p.t.projectSample + d;
    return q;
}

int32_t reasonOf (int32_t fault)
{
    switch (fault)
    {
        case kFaultDiskFull: return kEndDiskFull;
        case kFaultOverrun: return kEndOverrun;
        case kFaultSizeLimit: return kEndSizeLimit;
        default: return kEndWriteError;
    }
}

} // namespace

double ppqAfter (const TimePoint& p, int64_t samples, double sampleRate)
{
    if (!p.t.has (Transport::kPpqValid))
        return std::numeric_limits<double>::quiet_NaN ();
    return advance (p, samples, sampleRate).t.ppq;
}

void Probe::prepare (double sampleRate, int maxBlock, double bufferSeconds)
{
    sr = sampleRate > 0 ? sampleRate : 48000.0;
    (void)maxBlock;
    // stereo floats, plus room for the records around them
    const double bytes = std::max (bufferSeconds, 0.5) * sr * 8.0 * 1.1 + 65536.0;
    rb.allocate ((size_t)bytes);
    if (st)
        st->sampleRate.store (sr);
}

void Probe::publishState ()
{
    if (!st)
        return;
    st->state.store (!armed ? kStateOff : stopped ? kStateStopped : open ? kStateRecording : kStateArmed, std::memory_order_relaxed);
}

bool Probe::startTake (const Transport& t)
{
    TakeStart ts;
    ts.sampleRate = sr;
    ts.mode = mode;
    ts.t = t;
    // (the start, its first time map entry and some audio: else it would end at once)
    if (rb.freeBytes () < sizeof (Ring::Header) * 2 + sizeof (TakeStart) + sizeof (TimePoint) + kKeepFree + 8192)
        return false;
    if (!rb.push (kRecTakeStart, {{&ts, sizeof (ts)}}, kKeepFree))
        return false;
    open = true;
    frames = 0;
    midiCount = 0;
    if (st)
    {
        st->takeFrames.store (0, std::memory_order_relaxed);
        st->midiEvents.store (0, std::memory_order_relaxed);
    }
    last.sample = -1; // (the first block makes the first entry)
    return true;
}

void Probe::finishTake (int32_t reason)
{
    if (!open)
        return;
    TakeEnd te;
    te.reason = reason;
    rb.push (kRecTakeEnd, {{&te, sizeof (te)}}); // (kKeepFree was kept for it)
    open = false;
}

void Probe::endTake (int32_t reason)
{
    finishTake (reason);
    publishState ();
}

void Probe::fail (int32_t fault, int32_t reason)
{
    finishTake (reason);
    stopped = true;
    if (st)
        st->fault.store (fault, std::memory_order_release);
}

bool Probe::pushPoint (int64_t sample, const Transport& t)
{
    TimePoint p;
    p.sample = sample;
    p.t = t;
    if (!rb.push (kRecTimeMap, {{&p, sizeof (p)}}, kKeepFree))
        return false;
    last = p;
    return true;
}

bool Probe::timeMap (const Transport& t, int n)
{
    const int64_t s = frames;
    bool need = last.sample < 0 || s - last.sample >= kTimeMapStep;
    if (!need)
    {
        // does the transport go on the way the last entry says?
        const TimePoint pred = advance (last, s - last.sample, sr);
        need = (t.flags & kCompared) != (last.t.flags & kCompared) ||
               (t.has (Transport::kPpqValid) && std::fabs (t.ppq - pred.t.ppq) > 1e-5) ||
               (t.has (Transport::kTempoValid) && t.tempo != last.t.tempo) ||
               (t.has (Transport::kSigValid) && (t.sigNum != last.t.sigNum || t.sigDen != last.t.sigDen)) ||
               (t.has (Transport::kSamplesValid) && t.projectSample != pred.t.projectSample);
    }
    if (need && !pushPoint (s, t))
        return false;
    // inside a long block: extrapolated from its start
    TimePoint start;
    start.sample = s;
    start.t = t;
    while (last.sample + kTimeMapStep < s + n)
    {
        const TimePoint q = advance (start, last.sample + kTimeMapStep - s, sr);
        if (!pushPoint (q.sample, q.t))
            return false;
    }
    return true;
}

bool Probe::pushAudio (const float* l, const float* r, int n)
{
    for (int done = 0; done < n;)
    {
        const uint32_t k = (uint32_t)std::min (n - done, kChunkFrames);
        if (!rb.push (kRecAudio, {{&k, sizeof (k)}, {l + done, k * sizeof (float)}, {r + done, k * sizeof (float)}}, kKeepFree))
            return false;
        done += (int)k;
    }
    return true;
}

void Probe::process (const float* inL, const float* inR, float* outL, float* outR, int n, const Transport& t, const MidiRec* midi,
                     int numMidi)
{
    if (n <= 0)
        return;
    // the audio, untouched
    if (outL != inL)
        std::memcpy (outL, inL, (size_t)n * sizeof (float));
    if (outR != inR)
        std::memcpy (outR, inR, (size_t)n * sizeof (float));
    if (st)
    {
        float pl = 0.0f, pr = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            pl = std::max (pl, std::fabs (inL[i]));
            pr = std::max (pr, std::fabs (inR[i]));
        }
        if (!(pl <= st->peakL.load (std::memory_order_relaxed)))
            st->peakL.store (pl, std::memory_order_relaxed);
        if (!(pr <= st->peakR.load (std::memory_order_relaxed)))
            st->peakR.store (pr, std::memory_order_relaxed);
        st->noTransport.store (armed && mode == kModeWhilePlaying && !t.has (Transport::kValid), std::memory_order_relaxed);
    }

    if (!armed)
    {
        finishTake (kEndDisarmed);
        stopped = false;
        wasArmed = false;
        publishState ();
        return;
    }
    if (!wasArmed)
    {
        // arming again clears the last problem (its warning shows until then)
        wasArmed = true;
        if (st)
            st->fault.store (kFaultNone, std::memory_order_release);
    }
    if (const int32_t fault = st ? st->fault.load (std::memory_order_acquire) : kFaultNone; fault != kFaultNone)
    {
        // the writer stopped the take (the disk is full, the folder cannot be written)
        finishTake (reasonOf (fault));
        stopped = true;
    }
    if (stopped)
    {
        publishState ();
        return;
    }

    const bool want = mode == kModeAlways || (t.has (Transport::kValid) && t.playing ());
    if (open && !want)
        finishTake (kEndStopped);
    if (!open && want && !startTake (t))
    {
        fail (kFaultOverrun, kEndOverrun);
        publishState ();
        return;
    }
    if (open)
    {
        if (!timeMap (t, n))
        {
            fail (kFaultOverrun, kEndOverrun);
            publishState ();
            return;
        }
        const TimePoint blockStart {frames, t};
        for (int i = 0; i < numMidi; ++i)
        {
            MidiRec r = midi[i];
            const int64_t off = std::clamp<int64_t> (r.sample, 0, n - 1);
            r.sample = frames + off;
            r.ppq = ppqAfter (blockStart, off, sr);
            if (!rb.push (kRecMidi, {{&r, sizeof (r)}}, kKeepFree))
            {
                fail (kFaultOverrun, kEndOverrun);
                publishState ();
                return;
            }
            ++midiCount;
        }
        if (!pushAudio (inL, inR, n))
        {
            fail (kFaultOverrun, kEndOverrun);
            publishState ();
            return;
        }
        frames += n;
        if (st)
        {
            st->takeFrames.store (frames, std::memory_order_relaxed);
            st->midiEvents.store (midiCount, std::memory_order_relaxed);
        }
    }
    publishState ();
}

} // namespace probr
