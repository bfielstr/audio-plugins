#include "Engine.h"

#include <algorithm>
#include <cmath>

namespace orbitr {

namespace {
inline float dbToGain (double db) { return (float)std::pow (10.0, db / 20.0); }
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
    for (uint32_t id = 0; id < kTailBase; ++id)
        applyMotion (id, p[id]);
    motion.prepare (sr, maxBlock);
    dry.prepare (motion.latency ());
    tail.prepare (sr, maxBlock);
    for (uint32_t f = 0; f < smacheratr::kTailAllFields; ++f)
    {
        const uint32_t id = smacheratr::tailParamOf (f, {kTailBase, kTailExtBase, kTailExt2Base, kTailExt3Base, kTailExt4Base});
        tail.setParam (f, p[id]);
    }
    smooth = (float)(1.0 - std::exp (-1.0 / (0.02 * sr)));
    reset ();
}

void Engine::reset ()
{
    motion.reset ();
    dry.reset ();
    wet = (float)std::clamp (p[kDryWet], 0.0, 1.0);
    out = dbToGain (p[kOutput]);
    tail.reset ();
}

void Engine::applyMotion (uint32_t id, double v)
{
    switch (id)
    {
        case kOrbs: motion.setOrbs ((int)std::lround (v)); break;
        case kPattern: motion.setPattern ((int)std::lround (v)); break;
        case kSpeed: motion.setSpeed (v); break;
        case kDistance: motion.setDistance (v); break;
        case kRadius: motion.setRadius (v); break;
        case kSpread: motion.setSpread (v); break;
        case kRandom: motion.setRandomness (v); break;
        case kFloor: motion.setFloor (v >= 0.5); break;
        case kMix: motion.setMix (v); break;
        default: break;
    }
}

void Engine::setParam (uint32_t id, double plain)
{
    if (id >= kNumParams)
        return;
    p[id] = plain;
    if (id >= kTailExt4Base)
        tail.setParam (smacheratr::kTailExt4First + (id - kTailExt4Base), plain);
    else if (id >= kTailExt3Base)
        tail.setParam (smacheratr::kTailExt3First + (id - kTailExt3Base), plain);
    else if (id >= kTailExt2Base)
        tail.setParam (pk::kTailFields + pk::kTailExtFields + (id - kTailExt2Base), plain);
    else if (id >= kTailExtBase)
        tail.setParam (pk::kTailFields + (id - kTailExtBase), plain);
    else if (id >= kTailBase)
        tail.setParam (id - kTailBase, plain);
    else
        applyMotion (id, plain);
}

void Engine::process (const float* xl, const float* xr, float* yl, float* yr, int n)
{
    const dsp::NoDenormals guard;
    const float wetT = (float)std::clamp (p[kDryWet], 0.0, 1.0), outT = dbToGain (p[kOutput]);
    // the effect works in place: each slice of the input is kept for the dry path first
    float bufL[256], bufR[256];
    for (int a = 0; a < n; a += 256)
    {
        const int m = std::min (256, n - a);
        std::copy (xl + a, xl + a + m, bufL);
        std::copy (xr + a, xr + a + m, bufR);
        std::copy (bufL, bufL + m, yl + a);
        std::copy (bufR, bufR + m, yr + a);
        motion.process (yl + a, yr + a, m);
        for (int i = 0; i < m; ++i)
        {
            wet += (wetT - wet) * smooth;
            out += (outT - out) * smooth;
            float dl = bufL[i], dr = bufR[i];
            dry.tick (dl, dr);
            yl[a + i] = (dl + (yl[a + i] - dl) * wet) * out;
            yr[a + i] = (dr + (yr[a + i] - dr) * wet) * out;
        }
    }
    if (meters)
    {
        constexpr auto rx = std::memory_order_relaxed;
        const int k = motion.orbs ();
        meters->orbs.store (k, rx);
        for (int i = 0; i < k; ++i)
        {
            const auto o = motion.orbPosition (i);
            meters->orbX[(size_t)i].store ((float)o.x, rx);
            meters->orbY[(size_t)i].store ((float)o.y, rx);
        }
        meters->distance.store ((float)motion.currentDistance (), rx);
        meters->radius.store ((float)motion.currentRadius (), rx);
        meters->blocks.fetch_add (1, std::memory_order_release);
    }
    tail.process (yl, yr, n);
}

} // namespace orbitr
