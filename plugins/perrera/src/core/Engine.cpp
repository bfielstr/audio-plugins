#include "Engine.h"

#include <algorithm>
#include <cmath>

namespace perrera {

namespace {
inline float dbToGain (double db) { return (float)std::pow (10.0, db / 20.0); }
constexpr int kCoeffInterval = 8; // samples between cutoff updates
} // namespace

ParamArray defaultParams ()
{
    ParamArray p {};
    for (uint32_t i = 0; i < kNumParams; ++i)
        p[i] = paramTable ().info (i).def;
    return p;
}

void Engine::prepare (double sampleRate, int)
{
    sr = sampleRate;
    smooth = (float)(1.0 - std::exp (-1.0 / (0.02 * sr)));
    semiSmooth = (float)(1.0 - std::exp (-1.0 / (0.005 * sr))); // 5 ms glide of the cutoffs
    reset ();
}

void Engine::reset ()
{
    for (auto& c : hp)
        for (auto& s : c)
            s.reset ();
    for (auto& c : lp)
        for (auto& s : c)
            s.reset ();
    env = 0.0;
    envRising = false;
    offset = targetOffset ();
    split = p[kSplit];
    mix = (float)std::clamp (p[kDryWet], 0.0, 1.0);
    out = dbToGain (p[kOutput]);
    const bool slope24 = std::lround (p[kSlope]) == kSlope24;
    hpC.set (hpCutoff (p[kHpFreq], offset, split), resonanceToQ (p[kHpRes], slope24), sr);
    lpC.set (lpCutoff (p[kLpFreq], offset, split), resonanceToQ (p[kLpRes], slope24), sr);
}

double Engine::targetOffset () const
{
    if (lastNote < 0)
        return 0.0;
    const double played = lastNote + p[kTranspose] + bend * p[kPbRange];
    return (played - p[kRoot]) * std::clamp (p[kKey], 0.0, 1.0);
}

void Engine::noteOn (int note)
{
    lastNote = std::clamp (note, 0, 127);
    envRising = true; // the envelope restarts from where it is
}

void Engine::noteOff (int)
{
    // the last note keeps being tracked, so a released note leaves the filters where they are
}

void Engine::process (const float* xl, const float* xr, float* yl, float* yr, int n)
{
    const bool slope24 = std::lround (p[kSlope]) == kSlope24;
    const double hpBase = p[kHpFreq], lpBase = p[kLpFreq];
    const double qHp = resonanceToQ (p[kHpRes], slope24), qLp = resonanceToQ (p[kLpRes], slope24);
    const double envAmount = p[kEnvAmount];
    const double attackStep = 1.0 / std::max (1.0, p[kEnvAttack] * 0.001 * sr);
    const double decayCoef = std::exp (-1.0 / std::max (1.0, p[kEnvDecay] * 0.001 * sr));
    const float mixT = (float)std::clamp (p[kDryWet], 0.0, 1.0), outT = dbToGain (p[kOutput]);
    const double offsetT = targetOffset ();
    double envPeak = 0.0;

    for (int i = 0; i < n; ++i)
    {
        // envelope: linear attack to 1, exponential decay
        if (envRising)
        {
            env += attackStep;
            if (env >= 1.0)
            {
                env = 1.0;
                envRising = false;
            }
        }
        else
            env = env > 1e-4 ? env * decayCoef : 0.0;
        envPeak = std::max (envPeak, env);

        // where the cutoffs are heading, glided
        offset += (offsetT - offset) * semiSmooth;
        split += (p[kSplit] + envAmount * env - split) * semiSmooth;
        if (i % kCoeffInterval == 0)
        {
            hpC.set (hpCutoff (hpBase, offset, split), qHp, sr);
            lpC.set (lpCutoff (lpBase, offset, split), qLp, sr);
        }
        mix += (mixT - mix) * smooth;
        out += (outT - out) * smooth;

        const float ins[2] = {xl[i], xr[i]};
        float* outs[2] = {yl, yr};
        for (int c = 0; c < 2; ++c)
        {
            const double x = ins[c];
            double l, h, l2, h2, tmp;
            hp[c][0].tick (hpC, x, tmp, h);
            lp[c][0].tick (lpC, x, l, tmp);
            if (slope24)
            {
                hp[c][1].tick (hpC, h, tmp, h2);
                lp[c][1].tick (lpC, l, l2, tmp);
                h = h2;
                l = l2;
            }
            // second-order sections are 180 degrees apart at the crossing, so the high-pass is
            // inverted at 12 dB; the fourth-order pair is back in phase
            const double wet = slope24 ? h + l : l - h;
            outs[c][i] = (float)((x * (1.0 - mix) + wet * mix) * out);
        }
    }
    if (meters)
    {
        meters->hpHz.store ((float)hpCutoff (hpBase, offset, split), std::memory_order_relaxed);
        meters->lpHz.store ((float)lpCutoff (lpBase, offset, split), std::memory_order_relaxed);
        meters->env.store ((float)envPeak, std::memory_order_relaxed);
        meters->note.store (lastNote, std::memory_order_relaxed);
    }
}

} // namespace perrera
