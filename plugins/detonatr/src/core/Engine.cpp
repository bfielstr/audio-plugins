#include "Engine.h"

#include <algorithm>
#include <cmath>

namespace detonatr {

namespace {
inline double dbToGain (double db) { return std::pow (10.0, db / 20.0); }
inline float peakDb (const float* L, const float* R, int n)
{
    float pk = 0.0f;
    for (int i = 0; i < n; ++i)
        pk = std::max (pk, std::max (std::fabs (L[i]), std::fabs (R[i])));
    return pk > 1e-5f ? 20.0f * std::log10 (pk) : -100.0f;
}
} // namespace

ParamArray defaultParams ()
{
    ParamArray p {};
    for (uint32_t i = 0; i < kNumParams; ++i)
        p[i] = paramTable ().info (i).def;
    return p;
}

Engine::Engine ()
{
    int chosen[kNumStages];
    for (int i = 0; i < kNumStages; ++i)
        chosen[i] = (int)std::lround (p[kOrderBase + (uint32_t)i]);
    ord = resolveOrder (chosen);
}

void Engine::prepare (double sampleRate, int block)
{
    sr = sampleRate;
    maxBlock = std::max (1, block);
    clean.prepare (sr, maxBlock);
    tone.prepare (sr, maxBlock);
    multiband.prepare (sr, maxBlock);
    transient.prepare (sr, maxBlock);
    sat.prepare (sr, maxBlock);
    for (uint32_t id = 0; id < kNumParams; ++id)
        setParam (id, p[id]);
    for (int c = 0; c < 2; ++c)
    {
        toneDry[c].assign ((size_t)maxBlock, 0.0f);
        work[c].assign ((size_t)maxBlock, 0.0f);
    }
    dryLen = latency () + 1;
    for (auto& d : dryLine)
        d.assign ((size_t)dryLen, 0.0f);
    smooth = (float)(1.0 - std::exp (-1.0 / (0.005 * sr)));
    if (meters)
        meters->sampleRate.store ((float)sr);
    reset ();
}

void Engine::reset ()
{
    clean.reset ();
    tone.reset ();
    multiband.reset ();
    transient.reset ();
    sat.reset ();
    for (auto& d : dryLine)
        std::fill (d.begin (), d.end (), 0.0f);
    dryPos = 0;
    toneMix = p[kToneOn] >= 0.5 ? 1.0f : 0.0f;
    toneIdle = toneMix == 0.0f;
    mix = (float)std::clamp (p[kDryWet], 0.0, 1.0);
    out = (float)dbToGain (p[kOutput]);
}

int Engine::latency () const { return clean.latency () + tone.latency () + multiband.latency () + transient.latency () + sat.latency (); }

void Engine::setMeters (Meters* m)
{
    meters = m;
    sat.setMeters (m ? &m->sat : nullptr);
    if (m)
        m->sampleRate.store ((float)sr);
}

void Engine::applyStageOn ()
{
    // a stage that is off only delays (Transient: no drop; Clean: nothing taken away)
    const bool cleanOn = p[kCleanOn] >= 0.5;
    clean.setDenoise (cleanOn ? p[kDenoise] : 0.0);
    clean.setDereverb (cleanOn ? p[kDereverb] : 0.0);
    multiband.setBypass (p[kMultibandOn] < 0.5);
    transient.setDrop (p[kTransientOn] >= 0.5 ? p[kDrop] : 0.0);
}

void Engine::setParam (uint32_t id, double v)
{
    if (id >= kNumParams)
        return;
    p[id] = v;
    if (isTailParam (id))
    {
        sat.setParam (tailField (id), v);
        return;
    }
    if (isMbParam (id))
    {
        const int64_t md = mdIdOf (id);
        if (md >= 0)
            multiband.setParam ((uint32_t)md, v);
        return;
    }
    if (id >= kOrderBase && id < kOrderBase + kNumStages)
    {
        int chosen[kNumStages];
        for (int i = 0; i < kNumStages; ++i)
            chosen[i] = (int)std::lround (p[kOrderBase + (uint32_t)i]);
        ord = resolveOrder (chosen);
        return;
    }
    switch (id)
    {
        case kCleanOn:
        case kDenoise:
        case kDereverb:
        case kMultibandOn:
        case kTransientOn:
        case kDrop: applyStageOn (); break;
        case kRoot: tone.setRoot (v); break;
        case kMaterial: tone.setMaterial ((int)std::lround (v)); break;
        case kDecay: tone.setDecay (v / 1000.0); break;
        case kResonators: tone.setResonators (v); break;
        case kCarriers: tone.setCarriers (v); break;
        case kCarrierLevel1:
        case kCarrierLevel2:
        case kCarrierLevel3:
        case kCarrierLevel4: tone.setCarrierLevel ((int)(id - kCarrierLevel1), v); break;
        case kToneDry: tone.setDry (v); break;
        case kDisperse: tone.setDisperse (v); break;
        case kDisperseFreq: tone.setDisperseFreq (v); break;
        case kSpike: transient.setSpike (v); break;
        case kFall: transient.setFall (v); break;
        case kSensitivity: transient.setSensitivity (v); break;
        default: break;
    }
}

void Engine::runStage (int stage, float* L, float* R, int n)
{
    switch (stage)
    {
        case kStageClean: clean.process (L, R, n); break;
        case kStageTone:
        {
            // faded against its own input when turned on or off; once off and faded out it stops
            // (and starts clean when it comes back)
            const float target = p[kToneOn] >= 0.5 ? 1.0f : 0.0f;
            if (target == 0.0f && toneMix < 1e-4f)
            {
                toneMix = 0.0f;
                toneIdle = true;
                break;
            }
            if (toneIdle)
            {
                tone.reset ();
                toneIdle = false;
            }
            std::copy (L, L + n, toneDry[0].begin ());
            std::copy (R, R + n, toneDry[1].begin ());
            tone.process (L, R, n);
            for (int i = 0; i < n; ++i)
            {
                toneMix += (target - toneMix) * smooth;
                L[i] = toneDry[0][(size_t)i] + (L[i] - toneDry[0][(size_t)i]) * toneMix;
                R[i] = toneDry[1][(size_t)i] + (R[i] - toneDry[1][(size_t)i]) * toneMix;
            }
            break;
        }
        case kStageMultiband: multiband.process (L, R, nullptr, nullptr, L, R, n); break;
        case kStageTransient: transient.process (L, R, n); break;
        case kStageSaturator: sat.process (L, R, n); break;
        default: break;
    }
}

void Engine::process (const float* inL, const float* inR, float* outL, float* outR, int n)
{
    for (int start = 0; start < n; start += maxBlock)
    {
        const int m = std::min (maxBlock, n - start);
        float* L = work[0].data ();
        float* R = work[1].data ();
        std::copy (inL + start, inL + start + m, L);
        std::copy (inR + start, inR + start + m, R);
        const float inPeak = meters ? peakDb (L, R, m) : 0.0f;

        for (int k = 0; k < kNumStages; ++k)
            runStage (ord.stage[k], L, R, m);

        const float mixT = (float)std::clamp (p[kDryWet], 0.0, 1.0), outT = (float)dbToGain (p[kOutput]);
        for (int i = 0; i < m; ++i)
        {
            // the dry signal, delayed by the latency so it lines up with the stages' output
            dryLine[0][(size_t)dryPos] = inL[start + i];
            dryLine[1][(size_t)dryPos] = inR[start + i];
            int r = dryPos + 1;
            if (r >= dryLen)
                r = 0;
            dryPos = r;
            const float dl = dryLine[0][(size_t)r], dr = dryLine[1][(size_t)r];
            mix += (mixT - mix) * smooth;
            out += (outT - out) * smooth;
            const float l = (dl + (L[i] - dl) * mix) * out, rr = (dr + (R[i] - dr) * mix) * out;
            outL[start + i] = l;
            outR[start + i] = rr;
            if (meters)
                meters->scope.push (0.5f * (dl + dr), 0.5f * (l + rr));
        }

        if (meters)
        {
            meters->inDb.store (inPeak, std::memory_order_relaxed);
            meters->outDb.store (peakDb (outL + start, outR + start, m), std::memory_order_relaxed);
            for (int b = 0; b <= multidyn::kSubBand; ++b) // the bands and the Sub band
            {
                const auto& bm = multiband.meter (b);
                meters->multiband.inputDb[(size_t)b].store (bm.inputDb, std::memory_order_relaxed);
                meters->multiband.outputDb[(size_t)b].store (bm.outputDb, std::memory_order_relaxed);
                meters->multiband.gainDb[(size_t)b].store (bm.gainDb, std::memory_order_relaxed);
            }
            meters->blocks.fetch_add (1, std::memory_order_relaxed);
        }
    }
}

} // namespace detonatr
