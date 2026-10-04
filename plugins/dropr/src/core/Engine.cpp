#include "Engine.h"

#include <algorithm>
#include <cmath>

#if defined(__SSE__) || defined(_M_X64) || defined(_M_IX86)
#include <xmmintrin.h>
#define DROPR_FTZ 1
#endif

namespace dropr {

namespace {
inline float dbToGain (double db) { return (float)std::pow (10.0, db / 20.0); }
inline double coeff (double ms, double sr) { return 1.0 - std::exp (-1.0 / (std::max (0.001, ms) * 0.001 * sr)); }
constexpr float kDbPerLog2 = 6.02059991f; // 20 log10 (2)

// the filters' states would otherwise fall into denormals in silence (x86: flush them to zero)
struct NoDenormals
{
#if DROPR_FTZ
    unsigned int old;
    NoDenormals () : old (_mm_getcsr ()) { _mm_setcsr (old | 0x8040); }
    ~NoDenormals () { _mm_setcsr (old); }
#endif
};
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
    xfCoef = 1.0 - std::exp (-(double)kChunk / (kXoverSmoothMs * 0.001 * sr));
    smooth = (float)coeff (20.0, sr);
    mkSmooth = (float)coeff (20.0, sr);
    duckStep = (float)(1.0 / std::max (1.0, kModeDuckMs * 0.001 * sr));
    tail.prepare (sr, maxBlock);
    for (uint32_t f = 0; f < smacheratr::kTailAllFields; ++f)
    {
        const uint32_t id = smacheratr::tailParamOf (f, {kTailBase, kTailExtBase, kTailExt2Base, kTailExt3Base, kTailExt4Base});
        tail.setParam (f, p[id]);
    }
    reset ();
}

void Engine::retune ()
{
    const float fs = (float)sr;
    for (int j = 0; j < kNumXovers; ++j)
    {
        split[j].setup ((float)xf[j], fs);
        dryAp[j].setup ((float)xf[j], fs);
        for (int k = j + 1; k < kNumXovers; ++k)
            ap[j][k].setup ((float)xf[k], fs);
    }
    for (int k = 0; k < kMaxBands; ++k)
    {
        const double lo = k == 0 ? kLowestBandHz : xf[k - 1];
        band[k].holdLen = std::max (1, (int)std::ceil (sr / lo));
        band[k].fall = (float)std::exp (-4.0 / band[k].holdLen);
    }
}

void Engine::reset ()
{
    xoversFrom ([this] (uint32_t id) { return p[id]; }, xf);
    retune ();
    for (int j = 0; j < kNumXovers; ++j)
    {
        split[j].reset ();
        dryAp[j].reset ();
        for (int k = 0; k < kNumXovers; ++k)
            ap[j][k].reset ();
    }
    nb = std::clamp ((int)std::lround (p[kBands]), 1, kMaxBands);
    const double makeup = p[kMakeup], tilt = p[kTilt];
    for (int k = 0; k < kMaxBands; ++k)
    {
        Band& b = band[k];
        b.env[0] = b.env[1] = 0.0f;
        b.hold[0] = b.hold[1] = 0;
        b.gr[0] = b.gr[1] = 0.0f;
        const int g = std::min (k, nb - 1);
        b.mk = (float)(p[kBandGain1 + (uint32_t)g] + tiltDb (tilt, bandCentre (xf, g, nb)) + makeup);
        lastG[k][0] = lastG[k][1] = 1.0f;
        fadeFrom[k][0] = fadeFrom[k][1] = 1.0f;
    }
    fadePos = fadeLen = 1;
    mode = modeTarget = std::clamp ((int)std::lround (p[kMode]), 0, kNumModes - 1);
    duck = 1.0f;
    gin = dbToGain (p[kInput]);
    gout = dbToGain (p[kOutput]);
    mix = (float)std::clamp (p[kMix], 0.0, 1.0);
    tail.reset ();
}

void Engine::process (const float* xl, const float* xr, float* yl, float* yr, int n)
{
    {
        NoDenormals guard;
        for (int b = 0; b < kMaxBands; ++b)
        {
            band[b].mLevel = -150.0f;
            band[b].mGain = 0.0f;
            band[b].mOut = -150.0f;
        }
        for (int pos = 0; pos < n; pos += kChunk)
            processChunk (xl + pos, xr + pos, yl + pos, yr + pos, std::min (kChunk, n - pos));
    }
    if (meters)
    {
        meters->bands.store (nb, std::memory_order_relaxed);
        for (int k = 0; k < kMaxBands; ++k)
        {
            const bool used = k < nb;
            meters->levelDb[k].store (used ? band[k].mLevel : -150.0f, std::memory_order_relaxed);
            meters->gainDb[k].store (used ? band[k].mGain : 0.0f, std::memory_order_relaxed);
            meters->outDb[k].store (used ? band[k].mOut : -150.0f, std::memory_order_relaxed);
        }
    }
    tail.process (yl, yr, n);
}

void Engine::processChunk (const float* xl, const float* xr, float* yl, float* yr, int n)
{
    // the crossovers: smoothed towards their parameters, the filters retuned when they move
    {
        double target[kNumXovers];
        xoversFrom ([this] (uint32_t id) { return p[id]; }, target);
        bool moved = false;
        for (int j = 0; j < kNumXovers; ++j)
        {
            const double r = target[j] / xf[j];
            if (std::fabs (r - 1.0) < 1e-5)
            {
                if (xf[j] != target[j])
                {
                    xf[j] = target[j];
                    moved = true;
                }
                continue;
            }
            xf[j] *= std::pow (r, xfCoef);
            moved = true;
        }
        if (moved)
            retune ();
    }
    // Bands: regroup, crossfading every split band's gain from what it had over 20 ms
    const int nbT = std::clamp ((int)std::lround (p[kBands]), 1, kMaxBands);
    if (nbT != nb)
    {
        for (int k = 0; k < kMaxBands; ++k)
        {
            fadeFrom[k][0] = lastG[k][0];
            fadeFrom[k][1] = lastG[k][1];
        }
        // bands split off the old top band start from its detector
        for (int k = nb; k < nbT; ++k)
        {
            band[k].env[0] = band[nb - 1].env[0];
            band[k].env[1] = band[nb - 1].env[1];
            band[k].hold[0] = band[nb - 1].hold[0];
            band[k].hold[1] = band[nb - 1].hold[1];
            band[k].gr[0] = band[nb - 1].gr[0];
            band[k].gr[1] = band[nb - 1].gr[1];
            band[k].mk = band[nb - 1].mk;
        }
        nb = nbT;
        fadePos = 0;
        fadeLen = std::max (1, (int)std::lround (kBandsFadeMs * 0.001 * sr));
    }
    modeTarget = std::clamp ((int)std::lround (p[kMode]), 0, kNumModes - 1);

    const GainLaw law = GainLaw::from ([this] (uint32_t id) { return p[id]; });
    const float link = (float)std::clamp (p[kLink], 0.0, 1.0);
    const float adapt = (float)std::clamp (p[kAdaptive], 0.0, 1.0);
    atkC = (float)coeff (p[kAttack], sr);
    relC = (float)coeff (p[kRelease], sr);
    atkL = std::log2 (1.0f - atkC);
    relL = std::log2 (1.0f - relC);
    float mkT[kMaxBands];
    for (int g = 0; g < nb; ++g)
        mkT[g] = (float)(p[kBandGain1 + (uint32_t)g] + tiltDb (p[kTilt], bandCentre (xf, g, nb)) + p[kMakeup]);
    const float ginT = dbToGain (p[kInput]), goutT = dbToGain (p[kOutput]);
    const float mixT = (float)std::clamp (p[kMix], 0.0, 1.0);

    const float* in[2] = {xl, xr};
    for (int i = 0; i < n; ++i)
    {
        gin += (ginT - gin) * smooth;
        gout += (goutT - gout) * smooth;
        mix += (mixT - mix) * smooth;
        if (mode != modeTarget)
        {
            duck -= duckStep;
            if (duck <= 0.0f)
            {
                duck = 0.0f;
                mode = modeTarget;
            }
        }
        else if (duck < 1.0f)
            duck = std::min (1.0f, duck + duckStep);

        float b[kMaxBands][2], dry[2];
        for (int c = 0; c < 2; ++c)
        {
            const float x = in[c][i];
            float d = x;
            for (int j = 0; j < kNumXovers; ++j)
                d = dryAp[j].tick (d, c);
            dry[c] = d;
            float rest = x * gin;
            for (int j = 0; j < kNumXovers; ++j)
            {
                float lo, hi;
                split[j].tick (rest, c, lo, hi);
                b[j][c] = lo;
                rest = hi;
            }
            b[kMaxBands - 1][c] = rest;
            for (int j = 0; j + 1 < kNumXovers; ++j)
                for (int k = j + 1; k < kNumXovers; ++k)
                    b[j][c] = ap[j][k].tick (b[j][c], c);
        }
        const bool ms = mode == kModeMidSide;
        if (ms)
            for (int k = 0; k < kMaxBands; ++k)
            {
                const float m = 0.5f * (b[k][0] + b[k][1]), s = 0.5f * (b[k][0] - b[k][1]);
                b[k][0] = m;
                b[k][1] = s;
            }
        // the bands' gains (the top one takes the sum of the split bands above it)
        float G[kMaxBands][2];
        for (int g = 0; g < nb; ++g)
        {
            Band& B = band[g];
            float lvl[2];
            for (int c = 0; c < 2; ++c)
            {
                float v = b[g][c];
                if (g == nb - 1)
                    for (int k = g + 1; k < kMaxBands; ++k)
                        v += b[k][c];
                v = std::fabs (v);
                if (v >= B.env[c])
                {
                    B.env[c] = v;
                    B.hold[c] = B.holdLen;
                }
                else if (B.hold[c] > 0)
                    --B.hold[c];
                else
                    B.env[c] *= B.fall;
                lvl[c] = kDbPerLog2 * std::log2 (std::max (B.env[c], 1e-8f));
            }
            const float loud = std::max (lvl[0], lvl[1]);
            lvl[0] += link * (loud - lvl[0]);
            lvl[1] += link * (loud - lvl[1]);
            float t[2];
            t[0] = (float)std::max (-240.0, law.gain (lvl[0]));
            t[1] = std::fabs (lvl[1] - lvl[0]) < 1e-4f ? t[0] : (float)std::max (-240.0, law.gain (lvl[1]));
            B.mk += (mkT[g] - B.mk) * mkSmooth;
            for (int c = 0; c < 2; ++c)
            {
                const float diff = t[c] - B.gr[c];
                const bool down = diff < 0.0f;
                float coef = down ? atkC : relC;
                if (adapt > 0.0f)
                {
                    const float f = 1.0f + 3.0f * adapt * std::min (1.0f, std::fabs (diff) * (1.0f / 24.0f));
                    coef = 1.0f - std::exp2 (f * (down ? atkL : relL));
                }
                B.gr[c] += diff * coef;
                G[g][c] = std::exp2 ((B.gr[c] + B.mk) * (1.0f / kDbPerLog2));
            }
            B.mLevel = std::max (B.mLevel, loud);
            B.mGain = std::min (B.mGain, std::min (B.gr[0], B.gr[1]));
            B.mOut = std::max (B.mOut, std::max (lvl[0] + B.gr[0], lvl[1] + B.gr[1]) + B.mk);
        }
        // apply them to the split bands (crossfaded after a Bands change)
        float y[2] = {0.0f, 0.0f};
        const bool fading = fadePos < fadeLen;
        const float a = fading ? (float)fadePos / (float)fadeLen : 1.0f;
        for (int k = 0; k < kMaxBands; ++k)
        {
            const int g = std::min (k, nb - 1);
            for (int c = 0; c < 2; ++c)
            {
                float gk = G[g][c];
                if (fading)
                    gk = fadeFrom[k][c] + (gk - fadeFrom[k][c]) * a;
                lastG[k][c] = gk;
                y[c] += b[k][c] * gk;
            }
        }
        if (fading)
            ++fadePos;
        if (ms)
        {
            const float l = y[0] + y[1], r = y[0] - y[1];
            y[0] = l;
            y[1] = r;
        }
        const float w = mix * duck;
        yl[i] = (dry[0] * (1.0f - mix) + y[0] * w) * gout;
        yr[i] = (dry[1] * (1.0f - mix) + y[1] * w) * gout;
    }
}

} // namespace dropr
