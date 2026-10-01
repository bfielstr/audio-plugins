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
    voc.prepare (sr, maxBlock);
    spk.prepare (sr, maxBlock);
    mot.prepare (sr, maxBlock);
    for (int i = 0; i < 2; ++i)
    {
        tr[i].prepare (sr, maxBlock);
        lim[i].prepare (sr, maxBlock);
        comp[i].prepare (sr, maxBlock);
    }
    tape.prepare (sr, maxBlock);
    tail.prepare (sr, maxBlock);
    for (uint32_t id = 0; id < kNumParams; ++id)
        applyParam (id, p[id]);
    for (int s = 0; s < kNumStages; ++s)
        bypass[(size_t)s].prepare (stageLatency (s));
    for (int c = 0; c < 2; ++c)
    {
        work[c].assign ((size_t)maxBlock, 0.0f);
        bypassBuf[c].assign ((size_t)maxBlock, 0.0f);
    }
    dryLen = latency () - tail.latency () + 1; // the stages' latency (the tail comes after the dry/wet)
    for (auto& d : dryLine)
        d.assign ((size_t)dryLen, 0.0f);
    smooth = (float)(1.0 - std::exp (-1.0 / (0.005 * sr)));
    fadeStep = (float)(1.0 / (0.01 * sr));
    if (meters)
        meters->sampleRate.store ((float)sr);
    reset ();
}

void Engine::reset ()
{
    for (int s = 0; s < kNumStages; ++s)
    {
        resetStage (s);
        bypass[(size_t)s].reset ();
        fade[(size_t)s] = p[stageOnParam (s)] >= 0.5 ? 1.0f : 0.0f;
        warmup[(size_t)s] = 0;
    }
    tail.reset ();
    for (auto& d : dryLine)
        std::fill (d.begin (), d.end (), 0.0f);
    dryPos = 0;
    mix = (float)std::clamp (p[kDryWet], 0.0, 1.0);
    out = (float)dbToGain (p[kOutput]);
}

int Engine::stageLatency (int stage) const
{
    switch (stage)
    {
        case kStageVocoder: return voc.latency ();
        case kStageSpike: return spk.latency ();
        case kStageMotion: return mot.latency ();
        case kStageTransient1: return tr[0].latency ();
        case kStageLimiter1: return lim[0].latency ();
        case kStageTransient2: return tr[1].latency ();
        case kStageComp1: return comp[0].latency ();
        case kStageComp2: return comp[1].latency ();
        case kStageTape: return tape.latency ();
        case kStageLimiter2: return lim[1].latency ();
        default: return 0;
    }
}

int Engine::latency () const
{
    int l = tail.latency ();
    for (int s = 0; s < kNumStages; ++s)
        l += stageLatency (s);
    return l;
}

void Engine::setMeters (Meters* m)
{
    meters = m;
    tail.setMeters (m ? &m->sat : nullptr);
    if (m)
        m->sampleRate.store ((float)sr);
}

void Engine::resetStage (int stage)
{
    switch (stage)
    {
        case kStageVocoder: voc.reset (); break;
        case kStageSpike: spk.reset (); break;
        case kStageMotion: mot.reset (); break;
        case kStageTransient1: tr[0].reset (); break;
        case kStageLimiter1: lim[0].reset (); break;
        case kStageTransient2: tr[1].reset (); break;
        case kStageComp1: comp[0].reset (); break;
        case kStageComp2: comp[1].reset (); break;
        case kStageTape: tape.reset (); break;
        case kStageLimiter2: lim[1].reset (); break;
        default: break;
    }
}

void Engine::setParam (uint32_t id, double v)
{
    if (id >= kNumParams)
        return;
    p[id] = v;
    applyParam (id, v);
}

void Engine::applyParam (uint32_t id, double v)
{
    if (isTailParam (id))
    {
        tail.setParam (tailField (id), v);
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
    const bool on = v >= 0.5;
    for (int i = 0; i < 2; ++i)
    {
        if (id >= kTransientBase[i] && id < kTransientBase[i] + kTrFields)
        {
            Transient& t = tr[i];
            switch (id - kTransientBase[i])
            {
                case kTrGain: t.setGainDb (v); break;
                case kTrThreshold: t.setThresholdDb (v); break;
                case kTrDeadband: t.setDeadbandDb (v); break;
                case kTrRatio: t.setRatio (v); break;
                case kTrOvershoot: t.setOvershootMs (v); break;
                case kTrRise: t.setRiseMs (v); break;
                case kTrRecovery: t.setRecoveryMs (v); break;
                case kTrOverdrive: t.setOverdrive (v); break;
                case kTrOutput: t.setOutputDb (v); break;
                case kTrMix: t.setMix (v); break;
                default: break;
            }
            return;
        }
        if (id >= kLimiterBase[i] && id < kLimiterBase[i] + kLimFields)
        {
            Limiter& l = lim[i];
            switch (id - kLimiterBase[i])
            {
                case kLimGain: l.setGainDb (v); break;
                case kLimCeiling: l.setCeilingDb (v); break;
                case kLimLookahead: l.setLookaheadMs (v); break;
                case kLimAttack: l.setAttackMs (v); break;
                case kLimRelease: l.setReleaseMs (v); break;
                case kLimLink: l.setLink (v); break;
                case kLimTruePeak: l.setTruePeak (on); break;
                default: break;
            }
            return;
        }
        if (id >= kCompBase[i] && id < kCompBase[i] + kCompFields)
        {
            Ttm& c = comp[i];
            const uint32_t b = kCompBase[i];
            switch (id - b)
            {
                case kCompThreshold: c.setThresholdDb (v); break;
                case kCompAutoThreshold: c.setAutoThreshold (on); break;
                case kCompRatio: c.setRatio (v); break;
                case kCompAttack: c.setAttackMs (v); break;
                case kCompRelease: c.setReleaseMs (v); break;
                case kCompAutoRelease: c.setAutoRelease (on); break;
                case kCompKnee: c.setKneeDb (v); break;
                case kCompRange: c.setRangeDb (v); break;
                case kCompHold: c.setHoldMs (v); break;
                case kCompAutoGain: c.setAutoGain (on); break;
                case kCompDry: c.setDryDb (v); break;
                case kCompXoverLow:
                case kCompXoverHigh: c.setCrossovers (p[b + kCompXoverLow], p[b + kCompXoverHigh]); break;
                case kCompOutput: c.setOutputDb (v); break;
                default: break;
            }
            return;
        }
    }
    switch (id)
    {
        case kVocBands: voc.setBands ((int)std::lround (v)); break;
        case kVocLow:
        case kVocHigh: voc.setRange (p[kVocLow], p[kVocHigh]); break;
        case kVocOrder: voc.setOrder ((int)std::lround (v) + 1); break;
        case kVocAttack: voc.setAttack (v); break;
        case kVocRelease: voc.setRelease (v); break;
        case kVocRatio: voc.setRatio (v); break;

        case kSpkMode: spk.setMode ((int)std::lround (v)); break;
        case kSpkDepth: spk.setDepth (v); break;
        case kSpkSensitivity: spk.setSensitivity (v); break;
        case kSpkDecay: spk.setDecay (v); break;
        case kSpkSharpness: spk.setSharpness (v); break;
        case kSpkDecayTilt: spk.setDecayTilt (v); break;
        case kSpkLink: spk.setLink (v); break;
        case kSpkLow:
        case kSpkHigh: spk.setRange (p[kSpkLow], p[kSpkHigh]); break;
        case kSpkMix: spk.setMix (v); break;
        case kSpkTrim: spk.setTrimDb (v); break;

        case kMotOrbs: mot.setOrbs ((int)std::lround (v)); break;
        case kMotPattern: mot.setPattern ((int)std::lround (v)); break;
        case kMotSpeed: mot.setSpeed (v); break;
        case kMotDistance: mot.setDistance (v); break;
        case kMotRadius: mot.setRadius (v); break;
        case kMotSpread: mot.setSpread (v); break;
        case kMotRandom: mot.setRandomness (v); break;
        case kMotFloor: mot.setFloor (on); break;
        case kMotMix: mot.setMix (v); break;

        case kTapeSplit: tape.setSplit (v); break;
        case kTapeLowDrive: tape.setDriveDb (0, v); break;
        case kTapeLowMix: tape.setMix (0, v); break;
        case kTapeLowDyn: tape.setDynamics (0, v); break;
        case kTapeLowLevel: tape.setLevelDb (0, v); break;
        case kTapeHighDrive: tape.setDriveDb (1, v); break;
        case kTapeHighMix: tape.setMix (1, v); break;
        case kTapeHighDyn: tape.setDynamics (1, v); break;
        case kTapeHighLevel: tape.setLevelDb (1, v); break;
        default: break;
    }
}

void Engine::processStage (int stage, float* L, float* R, int n)
{
    switch (stage)
    {
        case kStageVocoder: voc.process (L, R, n); break;
        case kStageSpike: spk.process (L, R, n); break;
        case kStageMotion: mot.process (L, R, n); break;
        case kStageTransient1: tr[0].process (L, R, n); break;
        case kStageLimiter1: lim[0].process (L, R, n); break;
        case kStageTransient2: tr[1].process (L, R, n); break;
        case kStageComp1: comp[0].process (L, R, n); break;
        case kStageComp2: comp[1].process (L, R, n); break;
        case kStageTape: tape.process (L, R, n); break;
        case kStageLimiter2: lim[1].process (L, R, n); break;
        default: break;
    }
}

void Engine::runStage (int stage, float* L, float* R, int n)
{
    const float target = p[stageOnParam (stage)] >= 0.5 ? 1.0f : 0.0f;
    float& f = fade[(size_t)stage];
    int& wait = warmup[(size_t)stage];
    float* bl = bypassBuf[0].data ();
    float* br = bypassBuf[1].data ();
    std::copy (L, L + n, bl);
    std::copy (R, R + n, br);
    bypass[(size_t)stage].process (bl, br, n); // the input, delayed by the stage's latency
    if (f == 0.0f && wait == 0 && target == 0.0f)
    {
        std::copy (bl, bl + n, L);
        std::copy (br, br + n, R);
        return;
    }
    if (f == 0.0f && wait == 0)
    {
        // coming back: from a clean state, faded in once its delay is full again
        resetStage (stage);
        wait = std::max (1, stageLatency (stage));
    }
    processStage (stage, L, R, n);
    if (f == 1.0f && target == 1.0f)
        return;
    for (int i = 0; i < n; ++i)
    {
        if (wait > 0 && target == 1.0f)
            --wait;
        else
        {
            wait = 0;
            f = std::clamp (f + (target > f ? fadeStep : -fadeStep), 0.0f, 1.0f);
        }
        L[i] = bl[i] + (L[i] - bl[i]) * f;
        R[i] = br[i] + (R[i] - br[i]) * f;
    }
}

void Engine::writeMeters ()
{
    Meters& m = *meters;
    constexpr auto rx = std::memory_order_relaxed;
    m.vocBands.store (voc.bands (), rx);
    for (int b = 0; b < voc.bands (); ++b)
    {
        m.vocLevelDb[(size_t)b].store (voc.bandLevelDb (b), rx);
        m.vocFreq[(size_t)b].store ((float)voc.centre (b), rx);
    }
    for (int b = 0; b < Spike::kBands; ++b)
    {
        m.spikeGainDb[(size_t)b].store (spk.bandGainDb (b), rx);
        m.spikeFreq[(size_t)b].store ((float)spk.centre (b), rx);
    }
    m.orbs.store (mot.orbs (), rx);
    for (int k = 0; k < mot.orbs (); ++k)
    {
        const auto o = mot.orbPosition (k);
        m.orbX[(size_t)k].store ((float)o.x, rx);
        m.orbY[(size_t)k].store ((float)o.y, rx);
    }
    m.motionDistance.store ((float)mot.currentDistance (), rx);
    m.motionRadius.store ((float)mot.currentRadius (), rx);
    for (int i = 0; i < 2; ++i)
    {
        float boost = 0.0f, cut = 0.0f;
        tr[i].takeGainRange (boost, cut);
        // the largest since the display last looked (it resets them)
        m.trBoostDb[i].store (std::max (m.trBoostDb[i].load (rx), boost), rx);
        m.trCutDb[i].store (std::min (m.trCutDb[i].load (rx), cut), rx);
        m.limReductionDb[i].store (std::max (m.limReductionDb[i].load (rx), lim[i].takeReductionDb ()), rx);
        for (int b = 0; b < Ttm::kBands; ++b)
        {
            const auto bm = comp[i].meter (b);
            m.compLevelDb[i][b].store (bm.levelDb, rx);
            m.compTargetDb[i][b].store (bm.targetDb, rx);
            m.compGainDb[i][b].store (bm.gainDb, rx);
        }
        m.compMakeupDb[i].store (comp[i].makeupDb (), rx);
    }
    m.stageBlocks.fetch_add (1, rx);
}

void Engine::process (const float* inL, const float* inR, float* outL, float* outR, int n)
{
    const dsp::NoDenormals guard;
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
            // the dry signal, delayed by the stages' latency so it lines up with their output
            dryLine[0][(size_t)dryPos] = inL[start + i];
            dryLine[1][(size_t)dryPos] = inR[start + i];
            int r = dryPos + 1;
            if (r >= dryLen)
                r = 0;
            dryPos = r;
            const float dl = dryLine[0][(size_t)r], dr = dryLine[1][(size_t)r];
            mix += (mixT - mix) * smooth;
            out += (outT - out) * smooth;
            L[i] = (dl + (L[i] - dl) * mix) * out;
            R[i] = (dr + (R[i] - dr) * mix) * out;
            if (meters)
                meters->scope.push (0.5f * (dl + dr), 0.5f * (L[i] + R[i]));
        }
        // the Smacheratr at the very end (always in the path: off, it only delays)
        tail.process (L, R, m);
        std::copy (L, L + m, outL + start);
        std::copy (R, R + m, outR + start);

        if (meters)
        {
            meters->inDb.store (inPeak, std::memory_order_relaxed);
            meters->outDb.store (peakDb (L, R, m), std::memory_order_relaxed);
            writeMeters ();
            meters->blocks.fetch_add (1, std::memory_order_relaxed);
        }
    }
}

} // namespace detonatr
