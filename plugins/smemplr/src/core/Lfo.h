#pragma once

#include <cmath>
#include <cstdint>

namespace smemplr {

inline float randomBipolar (uint32_t& seed)
{
    seed = seed * 1664525u + 1013904223u;
    return (float)((seed >> 8) & 0xFFFFFF) / 8388607.5f - 1.0f;
}

inline float lfoShape (int wave, double phase, float randomValue)
{
    const float p = (float)phase;
    switch (wave)
    {
        case 0: return std::sin (2.0f * (float)M_PI * p);
        case 1: return p < 0.5f ? 1.0f : -1.0f;
        case 2: return p < 0.25f ? 4.0f * p : (p < 0.75f ? 2.0f - 4.0f * p : 4.0f * p - 4.0f);
        case 3: return 1.0f - 2.0f * p;
        case 4: return 2.0f * p - 1.0f;
        default: return randomValue;
    }
}

class Lfo
{
public:
    void start (double initialPhase, uint32_t seedIn)
    {
        phase = initialPhase - std::floor (initialPhase);
        seed = seedIn | 1u;
        rnd = randomBipolar (seed);
        elapsedMs = 0.0f;
    }

    // Value at the current phase, then advance by `samples`. `attackMs` fades the depth in.
    float advance (int wave, double freqHz, int samples, float sr, float attackMs)
    {
        const float v = lfoShape (wave, phase, rnd);
        const float fade = attackMs <= 0.0f ? 1.0f : std::min (1.0f, elapsedMs / attackMs);
        phase += freqHz * samples / sr;
        if (phase >= 1.0)
        {
            phase -= std::floor (phase);
            rnd = randomBipolar (seed);
        }
        elapsedMs += 1000.0f * samples / sr;
        return v * fade;
    }

    void setPhase (double p)
    {
        const double wrapped = p - std::floor (p);
        if (wrapped < phase - 0.5) // crossed a cycle boundary
            rnd = randomBipolar (seed);
        phase = wrapped;
    }

    double phase = 0.0;

private:
    uint32_t seed = 1;
    float rnd = 0.0f;
    float elapsedMs = 0.0f;
};

} // namespace smemplr
