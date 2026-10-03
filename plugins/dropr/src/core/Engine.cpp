#include "Engine.h"

#include <algorithm>
#include <cmath>

namespace dropr {

namespace {
inline float dbToGain (double db) { return (float)std::pow (10.0, db / 20.0); }
inline double coeff (double ms, double sr) { return 1.0 - std::exp (-1.0 / (std::max (0.01, ms) * 0.001 * sr)); }
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
    look = std::max (1, (int)std::lround (kLookaheadMs * 0.001 * sr));
    for (auto& d : delay)
        d.assign ((size_t)look, 0.0f);
    gainLine.assign ((size_t)look + 1, 1.0f);
    fastA = coeff (0.1, sr);
    fastR = coeff (10.0, sr);
    slowC = coeff (50.0, sr);
    ease = (float)coeff (0.5, sr);
    smooth = (float)coeff (20.0, sr);
    tail.prepare (sr, maxBlock);
    for (uint32_t f = 0; f < smacheratr::kTailAllFields; ++f)
    {
        const uint32_t id = f < pk::kTailFields                        ? kTailBase + f
                            : f < pk::kTailFields + pk::kTailExtFields ? kTailExtBase + (f - pk::kTailFields)
                                                                       : kTailExt2Base + (f - pk::kTailFields - pk::kTailExtFields);
        tail.setParam (f, p[id]);
    }
    shapeDirty = true;
    reset ();
}

void Engine::reset ()
{
    for (auto& d : delay)
        std::fill (d.begin (), d.end (), 0.0f);
    delayPos = 0;
    if (shapeDirty)
    {
        shape = shapeOf ([this] (uint32_t id) { return p[id]; });
        shapeDirty = false;
    }
    // before any hit: the shape's last level
    gain = dbToGain ((shape.valueAt (1.0) - 1.0) * std::clamp (p[kDepth], 0.0, 60.0));
    std::fill (gainLine.begin (), gainLine.end (), gain);
    gainPos = 0;
    fastEnv = slowEnv = 0.0;
    holdOff = 0;
    envPos = 1.0;
    mix = (float)std::clamp (p[kMix], 0.0, 1.0);
    out = dbToGain (p[kOutput]);
    tail.reset ();
}

void Engine::process (const float* xl, const float* xr, float* yl, float* yr, int n)
{
    if (shapeDirty)
    {
        shape = shapeOf ([this] (uint32_t id) { return p[id]; });
        shapeDirty = false;
    }
    const double sensDb = p[kSensitivity], depth = std::clamp (p[kDepth], 0.0, 60.0);
    const int retrig = std::max (1, (int)std::lround (p[kRetrigger] * 0.001 * sr));
    const double step = 1.0 / std::max (1.0, p[kLength] * 0.001 * sr);
    // the gain meets the audio (look - pre) samples after it is computed
    const int pre = std::clamp ((int)std::lround (p[kPre] * 0.001 * sr), 0, look);
    const int lag = look - pre;
    const int lineLen = (int)gainLine.size ();
    const float mixT = (float)std::clamp (p[kMix], 0.0, 1.0), outT = dbToGain (p[kOutput]);
    for (int i = 0; i < n; ++i)
    {
        const double lvl = std::max (std::fabs ((double)xl[i]), std::fabs ((double)xr[i]));
        fastEnv += (lvl - fastEnv) * (lvl > fastEnv ? fastA : fastR);
        slowEnv += (lvl - slowEnv) * slowC;
        if (holdOff > 0)
            --holdOff;
        else if (fastEnv > 0.003 && 20.0 * std::log10 ((fastEnv + 1e-9) / (slowEnv + 1e-9)) > sensDb)
        {
            envPos = 0.0;
            holdOff = retrig;
            slowEnv = fastEnv; // the hit is the new level: its tail does not trigger again
            ++hits;
        }
        const double y = shape.valueAt (envPos);
        if (envPos < 1.0)
            envPos = std::min (1.0, envPos + step);
        const float gT = (float)std::exp ((y - 1.0) * depth * 0.11512925); // dB -> gain
        gain += (gT - gain) * ease;
        // the gain line: written now, read lag samples later
        gainLine[(size_t)gainPos] = gain;
        int readPos = gainPos - lag;
        if (readPos < 0)
            readPos += lineLen;
        const float g = gainLine[(size_t)readPos];
        if (++gainPos >= lineLen)
            gainPos = 0;
        // the audio, look samples late
        const float dl = delay[0][(size_t)delayPos], dr = delay[1][(size_t)delayPos];
        delay[0][(size_t)delayPos] = xl[i];
        delay[1][(size_t)delayPos] = xr[i];
        if (++delayPos >= look)
            delayPos = 0;
        mix += (mixT - mix) * smooth;
        out += (outT - out) * smooth;
        const float wg = (1.0f - mix) + mix * g;
        yl[i] = dl * wg * out;
        yr[i] = dr * wg * out;
    }
    if (meters)
    {
        meters->position.store ((float)envPos, std::memory_order_relaxed);
        meters->gainDb.store (20.0f * std::log10 (std::max (1e-6f, gain)), std::memory_order_relaxed);
        meters->hits.store (hits, std::memory_order_relaxed);
    }
    tail.process (yl, yr, n);
}

} // namespace dropr
