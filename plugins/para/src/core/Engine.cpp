#include "Engine.h"

#include <algorithm>
#include <cmath>

namespace para {

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

void Engine::prepare (double sampleRate, int maxBlock)
{
    sr = sampleRate;
    tail.prepare (sr, maxBlock);
    for (uint32_t f = 0; f < pk::kTailFields; ++f)
        tail.setParam (f, p[kTailBase + f]);
    smooth = (float)(1.0 - std::exp (-1.0 / (0.02 * sr)));
    semiSmooth = (float)(1.0 - std::exp (-1.0 / (0.005 * sr))); // 5 ms glide of the cutoffs
    liquidA = 1.0 - std::exp (-1.0 / (0.15 * sr));
    if (meters)
        meters->sampleRate.store ((float)sr);
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
    tail.reset ();
    prevHpBase = p[kHpFreq]; // the leader of Vocal movement is kept
    prevLpBase = p[kLpFreq];
    hpMul = lpMul = hpMulT = lpMulT = 1.0f;
    offset = targetOffset ();
    split = p[kSplit];
    liquidLeaderLp = leaderLp;
    for (auto& s : notch)
        s.reset ();
    zigPhase = 0.0;
    lastLpSemis = -1.0;
    notchCut = notchCutT = 0.0f;
    liquidSlow = 12.0 * std::log2 (std::max (1.0, leaderLp ? p[kLpFreq] : p[kHpFreq]));
    mix = (float)std::clamp (p[kDryWet], 0.0, 1.0);
    out = dbToGain (p[kOutput]);
    hpG = (float)filterGain (p[kHpGain]);
    lpG = (float)filterGain (p[kLpGain]);
    for (int c = 0; c < 2; ++c)
    {
        hp1[c].reset ();
        lp1[c].reset ();
    }
    const int slope = (int)std::lround (p[kSlope]);
    hpC.set (hpCutoff (p[kHpFreq], offset, split), resonanceToQ (p[kHpRes], slope), sr);
    lpC.set (lpCutoff (p[kLpFreq], offset, split), resonanceToQ (lpRes (), slope), sr);
    hpG1 = onePoleG (hpCutoff (p[kHpFreq], offset, split), sr);
    lpG1 = onePoleG (lpCutoff (p[kLpFreq], offset, split), sr);
}

// The cutoffs do not follow the notes any more (the notes only trigger the envelope).
double Engine::targetOffset () const { return 0.0; }

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
    const int slope = std::clamp ((int)std::lround (p[kSlope]), (int)kSlope12, (int)kSlope24);
    const double hpBase = p[kHpFreq], lpBase = p[kLpFreq];
    const double qHp = resonanceToQ (p[kHpRes], slope), qLp = resonanceToQ (lpRes (), slope);
    const double envAmount = p[kEnvAmount];
    const double attackStep = 1.0 / std::max (1.0, p[kEnvAttack] * 0.001 * sr);
    const double decayCoef = std::exp (-1.0 / std::max (1.0, p[kEnvDecay] * 0.001 * sr));
    const float mixT = (float)std::clamp (p[kDryWet], 0.0, 1.0), outT = dbToGain (p[kOutput]);
    const float hpGT = (float)filterGain (p[kHpGain]), lpGT = (float)filterGain (p[kLpGain]);
    const double offsetT = targetOffset ();
    // Vocal movement: the filter whose cutoff moved last leads (Liquid is Vocal as well)
    const bool liquid = p[kLiquid] >= 0.5;
    const bool vocal = liquid || std::lround (p[kMovement]) == kVocal;
    if (p[kLpFreq] != prevLpBase)
        leaderLp = true;
    else if (p[kHpFreq] != prevHpBase)
        leaderLp = false;
    prevLpBase = p[kLpFreq];
    prevHpBase = p[kHpFreq];
    // Liquid: the leader's position; a new leader starts from rest
    const double lead = 12.0 * std::log2 (std::max (1.0, leaderLp ? p[kLpFreq] : p[kHpFreq]));
    if (leaderLp != liquidLeaderLp)
    {
        liquidLeaderLp = leaderLp;
        liquidSlow = lead;
    }
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
        liquidSlow += (lead - liquidSlow) * liquidA;
        // Liquid: Split swings with the sweep so the leader overshoots the way it moves (a positive
        // Split raises the high-pass and lowers the low-pass)
        const double swing = liquid ? std::clamp ((lead - liquidSlow) * (leaderLp ? -2.0 : 2.0), -36.0, 36.0) : 0.0;
        split += (p[kSplit] + envAmount * env + swing - split) * semiSmooth;
        if (i % kCoeffInterval == 0)
        {
            double hz = hpCutoff (hpBase, offset, split), lz = lpCutoff (lpBase, offset, split);
            rawHp = hz;
            rawLp = lz;
            hpMulT = lpMulT = 1.0f;
            if (vocal) // crossed: the follower sits at the leader's cutoff and fades out
                vocalPush (hz, lz, leaderLp, p[kFade], hpMulT, lpMulT);
            curHp = hz;
            curLp = lz;
            // Liquid's notch: around the low-pass, zigzagging as the low-pass moves (the zigzag advances
            // one cycle per 5 semitones of travel), never down in the sub
            notchCutT = 0.0f;
            if (liquid && p[kNotch] >= 0.5)
            {
                const double lpSemis = 12.0 * std::log2 (std::max (1.0, lz));
                if (lastLpSemis >= 0.0)
                    zigPhase = std::fmod (zigPhase + std::fabs (lpSemis - lastLpSemis) / 5.0, 1000.0);
                lastLpSemis = lpSemis;
                const double f = zigPhase - std::floor (zigPhase);
                const double tri = f < 0.25 ? 4.0 * f : (f < 0.75 ? 2.0 - 4.0 * f : 4.0 * f - 4.0);
                notchHz = std::clamp (lz * std::pow (2.0, 7.0 * tri / 12.0), 20.0, 0.45 * sr);
                const double depth = std::clamp (2.0 * std::log2 (notchHz / 180.0), 0.0, 1.0); // none below 180 Hz
                notchCutT = (float)(depth * (1.0 - std::pow (10.0, -18.0 / 20.0)));             // -18 dB deep
                notchC.set (notchHz, 2.0, sr);
            }
            else
                lastLpSemis = -1.0;
            hpC.set (hz, qHp, sr);
            lpC.set (lz, qLp, sr);
            hpG1 = onePoleG (hz, sr);
            lpG1 = onePoleG (lz, sr);
        }
        mix += (mixT - mix) * smooth;
        out += (outT - out) * smooth;
        hpMul += (hpMulT - hpMul) * smooth;
        lpMul += (lpMulT - lpMul) * smooth;
        hpG += (hpGT - hpG) * smooth;
        lpG += (lpGT - lpG) * smooth;
        notchCut += (notchCutT - notchCut) * smooth;

        const float ins[2] = {xl[i], xr[i]};
        float* outs[2] = {yl, yr};
        for (int c = 0; c < 2; ++c)
        {
            const double x = ins[c];
            double l, h, l2, h2, tmp, wet;
            if (slope == kSlope18)
            {
                // Butterworth 3: a first-order and a second-order section (Q 1); the pair is in
                // quadrature, so it sums flat without inverting
                double h1, l1;
                hp1[c].tick (hpG1, x, tmp, h1);
                lp1[c].tick (lpG1, x, l1, tmp);
                hp[c][0].tick (hpC, h1, tmp, h);
                lp[c][0].tick (lpC, l1, l, tmp);
                wet = hpG * hpMul * h + lpG * lpMul * l;
            }
            else
            {
                hp[c][0].tick (hpC, x, tmp, h);
                lp[c][0].tick (lpC, x, l, tmp);
                if (slope == kSlope24)
                {
                    hp[c][1].tick (hpC, h, tmp, h2);
                    lp[c][1].tick (lpC, l, l2, tmp);
                    h = h2;
                    l = l2;
                }
                // second-order sections are 180 degrees apart at the crossing, so the high-pass is
                // inverted at 12 dB; the fourth-order pair is back in phase
                wet = slope == kSlope24 ? hpG * hpMul * h + lpG * lpMul * l : lpG * lpMul * l - hpG * hpMul * h;
            }
            if (notchCut > 1e-4f)
            {
                // a band cut: the input minus part of its (unity-peak) band-pass
                double nl, nh;
                notch[c].tick (notchC, wet, nl, nh);
                wet -= notchCut * (wet - nl - nh);
            }
            outs[c][i] = (float)((x * (1.0 - mix) + wet * mix) * out);
        }
        if (meters)
            meters->scope.push (0.5f * (ins[0] + ins[1]), 0.5f * (outs[0][i] + outs[1][i]));
    }
    if (hasTail)
        tail.process (yl, yr, n);
    if (meters)
    {
        meters->offset.store ((float)offset, std::memory_order_relaxed);
        meters->hpHz.store ((float)curHp, std::memory_order_relaxed);
        meters->hpMul.store (hpMul, std::memory_order_relaxed);
        meters->lpMul.store (lpMul, std::memory_order_relaxed);
        meters->lpHz.store ((float)curLp, std::memory_order_relaxed);
        // how far the engine has moved the filters from where they are set: the display adds this to
        // the current settings, so edits show at once and the movement shows too
        meters->hpShift.store ((float)(12.0 * std::log2 (rawHp / std::max (1.0, p[kHpFreq]))), std::memory_order_relaxed);
        meters->lpShift.store ((float)(12.0 * std::log2 (rawLp / std::max (1.0, p[kLpFreq]))), std::memory_order_relaxed);
        meters->leaderLp.store (leaderLp, std::memory_order_relaxed);
        meters->notchHz.store ((float)notchHz, std::memory_order_relaxed);
        meters->notchCut.store (notchCut, std::memory_order_relaxed);
        meters->blocks.fetch_add (1, std::memory_order_relaxed);
        meters->env.store ((float)envPeak, std::memory_order_relaxed);
        meters->note.store (lastNote, std::memory_order_relaxed);
    }
}

} // namespace para
