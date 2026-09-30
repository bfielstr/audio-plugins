#include "Engine.h"

#include <algorithm>
#include <cmath>

namespace deepr {

namespace {
inline float dbToGain (double db) { return (float)std::pow (10.0, db / 20.0); }
inline float coeff (double ms, double sr) { return (float)(1.0 - std::exp (-1.0 / (std::max (0.1, ms) * 0.001 * sr))); }
} // namespace

ParamArray defaultParams ()
{
    ParamArray p {};
    for (uint32_t i = 0; i < kNumParams; ++i)
        p[i] = paramTable ().info (i).def;
    return p;
}

void Engine::prepare (double sampleRate, int maxBlock)
{
    sr = sampleRate;
    tail.prepare (sr, maxBlock);
    for (uint32_t f = 0; f < smacheratr::kTailAllFields; ++f)
    {
        const uint32_t id = f < pk::kTailFields                        ? kTailBase + f
                            : f < pk::kTailFields + pk::kTailExtFields ? kTailExtBase + (f - pk::kTailFields)
                                                                       : kTailExt2Base + (f - pk::kTailFields - pk::kTailExtFields);
        tail.setParam (f, p[id]);
    }
    smooth = (float)(1.0 - std::exp (-1.0 / (0.02 * sr)));
    updateFilters (true);
    reset ();
}

void Engine::reset ()
{
    split.reset ();
    dryAllpass.reset ();
    for (auto& f : band)
        f.reset ();
    env = 0.0f;
    gCut = 1.0f;
    mono = (float)std::clamp (p[kMonoSub], 0.0, 1.0);
    subGain = dbToGain (p[kSubGain]);
    mix = (float)std::clamp (p[kMix], 0.0, 1.0);
    out = dbToGain (p[kOutput]);
    tail.reset ();
}

void Engine::updateFilters (bool force)
{
    if (force || p[kSplit] != splitHz)
    {
        splitHz = p[kSplit];
        const float g = levlr::cornerG (splitHz, sr);
        split.setup (g, levlr::kSlope24);
        dryAllpass.setup (g, levlr::kSlope24);
    }
    if (force || p[kDipFreq] != dipHz || p[kDipWidth] != dipWidth)
    {
        dipHz = p[kDipFreq];
        dipWidth = p[kDipWidth];
        const smacheratr::BiquadCoeffs b = smacheratr::bandPass (sr, std::min (dipHz, 0.4 * sr), dipQ (dipWidth));
        for (auto& f : band)
            f.c = b;
    }
}

void Engine::process (const float* xl, const float* xr, float* yl, float* yr, int n)
{
    updateFilters (false);
    const float depth = (float)std::clamp (p[kDepth], 0.0, 12.0);
    const double threshold = p[kThreshold];
    const float atk = coeff (p[kAttack], sr), rel = coeff (p[kRelease], sr), ease = coeff (1.0, sr);
    const int listen = std::clamp ((int)std::lround (p[kListen]), (int)kListenOff, (int)kListenCut);
    const float monoT = (float)std::clamp (p[kMonoSub], 0.0, 1.0), subT = dbToGain (p[kSubGain]);
    const float mixT = (float)std::clamp (p[kMix], 0.0, 1.0), outT = dbToGain (p[kOutput]);
    float peak = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        mono += (monoT - mono) * smooth;
        subGain += (subT - subGain) * smooth;
        mix += (mixT - mix) * smooth;
        out += (outT - out) * smooth;
        const float x[2] = {xl[i], xr[i]};
        float lo[2], hi[2], dry[2];
        for (int c = 0; c < 2; ++c)
        {
            split.tick (x[c], c, lo[c], hi[c]);
            dry[c] = dryAllpass.tick (x[c], c);
        }
        // the sub: its side folded into its mid, then its level
        const float mid = 0.5f * (lo[0] + lo[1]), side = 0.5f * (lo[0] - lo[1]);
        lo[0] = (mid + (1.0f - mono) * side) * subGain;
        lo[1] = (mid - (1.0f - mono) * side) * subGain;
        // the key: the sub's peak level (the mid, as the sub is heard), fast up / slow down
        const float a = std::fabs (mid * subGain);
        env += (a - env) * (a > env ? atk : rel);
        const double envDb = 20.0 * std::log10 (std::max (1e-6f, env));
        const float cutDb = depth * (float)dipKey (envDb, threshold);
        // the dip's gain (the key is already smoothed by the envelope; 1 ms more so a step never clicks)
        const float gT = std::exp (-cutDb * 0.11512925f); // dB -> gain
        gCut += (gT - gCut) * ease;
        peak = std::max (peak, env);
        float y[2];
        for (int c = 0; c < 2; ++c)
        {
            const float removed = (1.0f - gCut) * (float)band[c].process (hi[c]);
            float wet;
            if (listen == kListenSub)
                wet = lo[c];
            else if (listen == kListenCut)
                wet = removed;
            else
                wet = lo[c] + hi[c] - removed;
            y[c] = (listen == kListenOff ? dry[c] * (1.0f - mix) + wet * mix : wet) * out;
        }
        yl[i] = y[0];
        yr[i] = y[1];
    }
    if (meters)
    {
        meters->subDb.store (20.0f * std::log10 (std::max (1e-6f, peak)), std::memory_order_relaxed);
        meters->cutDb.store (20.0f * std::log10 (std::max (1e-6f, gCut)), std::memory_order_relaxed);
    }
    tail.process (yl, yr, n);
}

} // namespace deepr
