#include "Engine.h"

#include "ClarityBand.h"
#include "Color.h"

#include <algorithm>
#include <cmath>

namespace smacheratr {

namespace {
inline float dbToGain (double db) { return (float)std::pow (10.0, db / 20.0); }
constexpr double kDcHz = 10.0;
} // namespace

ParamArray defaultParams ()
{
    ParamArray p {};
    for (uint32_t i = 0; i < kNumParams; ++i)
        p[i] = paramTable ().info (i).def;
    return p;
}

void Engine::Channel::reset ()
{
    for (int k = 0; k < kClarityBands; ++k)
    {
        bandHp[k].reset ();
        bandLp[k].reset ();
        postHp[k].reset ();
        postLp[k].reset ();
    }
    dc.reset ();
    preLo.reset ();
    preHi.reset ();
    postLo.reset ();
    postHi.reset ();
    os.reset ();
    dryDelay.reset ();
    wetDelay.reset ();
    lookDelay.reset ();
}

void Engine::prepare (double sampleRate, int mb)
{
    sr = sampleRate;
    maxBlock = std::max (1, mb);
    look = std::clamp ((int)std::lround (0.001 * sr), 1, kMaxLook);
    lmAtk = 1.0 - std::exp (-1.0 / (0.015 * sr));
    lmRel = 1.0 - std::exp (-1.0 / (0.15 * sr));
    for (int k = 0; k < kClarityBands; ++k)
        bandFreq[k] = bandWidth[k] = -1.0; // Clarity's bands are designed for this rate on the next block
    // the limiter's gain (in dB) reaches its target within the look-ahead and releases in 50 ms
    limAtk = (float)std::exp (-5.0 / look);
    limRel = (float)std::exp (-1.0 / (0.050 * sr));
    lookPeaks.assign ((size_t)look, 0.0f);
    for (auto& c : chan)
    {
        c.os.prepare (sr, maxBlock);
        c.dc.c = highPass (sr, kDcHz, M_SQRT1_2);
        c.lookDelay.resize (look);
    }
    for (auto& c : chan)
    {
        c.dryDelay.resize (latency ());
        c.wetDelay.resize (c.os.latency ()); // stands in for the oversampler when Hi-Quality is off
    }
    for (int c = 0; c < 2; ++c)
    {
        dry[c].assign ((size_t)maxBlock, 0.0f);
        pre[c].assign ((size_t)maxBlock, 0.0f);
    }
    for (auto* v : {&wet, &gDrive, &gOut, &gMix, &msMid, &msSide, &gPost[0], &gPost[1]})
        v->assign ((size_t)maxBlock, 0.0f);
    osBuf.assign ((size_t)maxBlock * 4, 0.0f);
    smooth = (float)(1.0 - std::exp (-1.0 / (0.02 * sr)));
    updateFilters (true);
    reset ();
}

void Engine::reset ()
{
    for (auto& c : chan)
        c.reset ();
    drive = dbToGain (p[kDrive]);
    out = dbToGain (p[kOutput]);
    mix = (float)std::clamp (p[kDryWet], 0.0, 1.0);
    limGainDb = 0.0f;
    std::fill (lookPeaks.begin (), lookPeaks.end (), 0.0f);
    lookPos = 0;
}

void Engine::updateFilters (bool force)
{
    const double rate = p[kHiQuality] >= 0.5 ? 4.0 * sr : sr;
    const double lo = colorDb (p[kColorLo]), hi = colorDb (p[kColorHi]), freq = p[kColorFreq], width = p[kColorWidth];
    if (!force && lo == cLo && hi == cHi && freq == cFreq && width == cWidth && rate == cRate)
        return;
    cLo = lo;
    cHi = hi;
    cFreq = freq;
    cWidth = width;
    cRate = rate;
    const double f = colorPeakHz (freq, rate), q = colorQ (width);
    const BiquadCoeffs preLo = lowShelf (rate, kColorLowHz, lo), postLo = lowShelf (rate, kColorLowHz, -lo);
    const BiquadCoeffs preHi = peak (rate, f, hi, q), postHi = peak (rate, f, -hi, q);
    for (auto& c : chan)
    {
        c.preLo.c = preLo;
        c.postLo.c = postLo;
        c.preHi.c = preHi;
        c.postHi.c = postHi;
    }
}

float Engine::shapeChain (Channel& c, float v, bool color, int post) const
{
    double d = v;
    if (color)
    {
        d = c.preLo.process (d);
        d = c.preHi.process (d);
    }
    d = analogClip (d);
    if (color)
    {
        d = c.postLo.process (d);
        d = c.postHi.process (d);
    }
    if (post == kPostSoft)
        d = analogClip (d);
    else if (post == kPostHard)
        d = digitalClip (d);
    return (float)d;
}

void Engine::process (const float* xl, const float* xr, float* yl, float* yr, int n)
{
    if (n > maxBlock)
    {
        for (int pos = 0; pos < n; pos += maxBlock)
        {
            const int m = std::min (maxBlock, n - pos);
            process (xl + pos, xr + pos, yl + pos, yr + pos, m);
        }
        return;
    }
    const float mixT = (float)std::clamp (p[kDryWet], 0.0, 1.0);
    if (mixT <= 0.0f && mix < 1e-5f)
    {
        // fully dry: only the dry delay runs (the latency stays), the wet path restarts from
        // silence when the dry/wet opens again
        const float outT = dbToGain (p[kOutput]);
        for (int i = 0; i < n; ++i)
        {
            out += (outT - out) * smooth;
            const float l = chan[0].dryDelay.push (xl[i]), r = chan[1].dryDelay.push (xr[i]);
            yl[i] = l * out;
            yr[i] = r * out;
        }
        mix = 0.0f;
        if (!wetIdle)
        {
            wetIdle = true;
            if (meters)
            {
                meters->inPeak.store (0.0f, std::memory_order_relaxed);
                meters->outPeak.store (0.0f, std::memory_order_relaxed);
                meters->clarityDb.store (0.0f, std::memory_order_relaxed);
                meters->clarity2Db.store (0.0f, std::memory_order_relaxed);
            }
        }
        return;
    }
    if (!inMs && p[kMidSide] >= 0.5)
    {
        // Mid/Side: the mid and the side go through the curve as two channels, so the side is driven
        // by its own (lower) level and keeps its size next to a mid that is being squashed; back to
        // left / right after (the dry path makes the same round trip, so the mix is unchanged)
        for (int i = 0; i < n; ++i)
        {
            msMid[(size_t)i] = 0.5f * (xl[i] + xr[i]);
            msSide[(size_t)i] = 0.5f * (xl[i] - xr[i]);
        }
        inMs = true;
        process (msMid.data (), msSide.data (), yl, yr, n);
        inMs = false;
        for (int i = 0; i < n; ++i)
        {
            const float m = yl[i], s = yr[i];
            yl[i] = m + s;
            yr[i] = m - s;
        }
        return;
    }
    if (wetIdle)
    {
        wetIdle = false;
        for (auto& c : chan)
        {
            c.dc.reset ();
            c.preLo.reset ();
            c.preHi.reset ();
            c.postLo.reset ();
            c.postHi.reset ();
            c.os.reset ();
            c.wetDelay.reset ();
            c.lookDelay.reset ();
        }
        limGainDb = 0.0f;
        std::fill (lookPeaks.begin (), lookPeaks.end (), 0.0f);
    }
    updateFilters (false);
    const bool hiq = p[kHiQuality] >= 0.5, color = p[kColorOn] >= 0.5, dc = p[kDcFilter] >= 0.5;
    const bool preLimit = p[kPreLimit] >= 0.5;
    const float threshold = dbToGain (p[kPreLimitThreshold]);
    const int post = std::clamp ((int)std::lround (p[kPostClip]), (int)kPostOff, (int)kPostHard);

    // per-sample smoothed gains, shared by both channels
    const float driveT = dbToGain (p[kDrive]), outT = dbToGain (p[kOutput]);
    for (int i = 0; i < n; ++i)
    {
        drive += (driveT - drive) * smooth;
        out += (outT - out) * smooth;
        mix += (mixT - mix) * smooth;
        gDrive[(size_t)i] = drive;
        gOut[(size_t)i] = out;
        gMix[(size_t)i] = mix;
    }

    // Clarity, a compressor on up to two bands (one button; a band works while its Range is above 0) (ClarityBand.h: 12 dB/oct below, 6 dB/oct above,
    // around each band's frequency): when the drive pushes a band past -18 dBFS into the curve, it is
    // turned down before the curve (3 dB for every 5 over, at most the band's Range), so it does not
    // pile up into mud and intermodulate, and after it by half as much (the curve squashes the cut
    // before it back up). The bands work one after the other, each measuring its own band.
    bool clarity[kClarityBands];
    for (int k = 0; k < kClarityBands; ++k)
    {
        clarity[k] = clarityBandOn (p[kClarity], p[kClarityRangeIds[k]]);
        if (clarity[k] != clarityWas[k])
        {
            clarityWas[k] = clarity[k];
            lmEnv[k] = 0.0;
            lmCutDb[k] = 0.0f;
            gBandPre[k] = gBandPost[k] = 1.0f;
            for (auto& c : chan)
            {
                c.bandHp[k].reset ();
                c.bandLp[k].reset ();
                c.postHp[k].reset ();
                c.postLp[k].reset ();
            }
        }
        if (!clarity[k])
            continue;
        const double levelDb = 10.0 * std::log10 (std::max (1e-12, lmEnv[k]));
        lmCutDb[k] = (float)std::clamp ((levelDb + 18.0) * 0.6, 0.0, std::clamp (p[kClarityRangeIds[k]], 0.0, 24.0));
        if (p[kClarityFreqIds[k]] != bandFreq[k] || p[kClarityWidthIds[k]] != bandWidth[k])
        {
            bandFreq[k] = p[kClarityFreqIds[k]];
            bandWidth[k] = p[kClarityWidthIds[k]];
            const ClarityBand b = clarityBand (sr, bandFreq[k], bandWidth[k]);
            for (auto& c : chan)
            {
                c.bandHp[k].c = c.postHp[k].c = b.hp;
                c.bandLp[k].c = c.postLp[k].c = b.lp;
            }
            bandNorm[k] = (float)b.norm;
        }
    }

    // DC filter, look-ahead delay and the stereo-linked pre-limiter, then the drive
    float inPk = 0.0f, outPk = 0.0f;
    float gPreTarget[kClarityBands], gPostTarget[kClarityBands];
    for (int k = 0; k < kClarityBands; ++k)
    {
        gPreTarget[k] = dbToGain (-lmCutDb[k]);
        gPostTarget[k] = dbToGain (-0.5 * lmCutDb[k]);
    }
    for (int i = 0; i < n; ++i)
    {
        for (int k = 0; k < kClarityBands; ++k)
            if (clarity[k])
            {
                gBandPre[k] += (gPreTarget[k] - gBandPre[k]) * smooth;
                gBandPost[k] += (gPostTarget[k] - gBandPost[k]) * smooth;
                gPost[k][(size_t)i] = gBandPost[k];
            }
        float v[2] = {xl[i], xr[i]};
        for (int c = 0; c < 2; ++c)
        {
            dry[c][(size_t)i] = chan[c].dryDelay.push (v[c]);
            if (dc)
                v[c] = (float)chan[c].dc.process (v[c]);
        }
        // the loudest sample in the look-ahead window sets the gain the window needs; the gain
        // gets there (in dB) before that sample reaches the drive
        lookPeaks[(size_t)lookPos] = std::max (std::fabs (v[0]), std::fabs (v[1]));
        if (++lookPos >= look)
            lookPos = 0;
        float limGain = 1.0f;
        if (preLimit)
        {
            float pk = 0.0f;
            for (float x : lookPeaks)
                pk = std::max (pk, x);
            const float needDb = pk > threshold ? 20.0f * std::log10 (threshold / pk) : 0.0f;
            limGainDb = needDb + (needDb < limGainDb ? limAtk : limRel) * (limGainDb - needDb);
            limGain = std::exp (limGainDb * 0.11512925f); // dB -> gain
        }
        else
            limGainDb = 0.0f;
        double lmPower[kClarityBands] = {0.0, 0.0};
        for (int c = 0; c < 2; ++c)
        {
            float d = chan[c].lookDelay.push (v[c]) * limGain * gDrive[(size_t)i];
            for (int k = 0; k < kClarityBands; ++k)
                if (clarity[k])
                {
                    const double band = chan[c].bandLp[k].process (chan[c].bandHp[k].process (d)) * bandNorm[k];
                    lmPower[k] = std::max (lmPower[k], 2.0 * band * band); // a sine's peak level
                    d = (float)(d + (gBandPre[k] - 1.0f) * band);
                }
            pre[c][(size_t)i] = d;
            inPk = std::max (inPk, std::fabs (d));
        }
        for (int k = 0; k < kClarityBands; ++k)
            if (clarity[k])
                lmEnv[k] += (lmPower[k] - lmEnv[k]) * (lmPower[k] > lmEnv[k] ? lmAtk : lmRel);
    }

    float* outs[2] = {yl, yr};
    for (int c = 0; c < 2; ++c)
    {
        Channel& ch = chan[c];
        if (hiq)
        {
            ch.os.up (pre[c].data (), osBuf.data (), n);
            for (int i = 0; i < 4 * n; ++i)
                osBuf[(size_t)i] = shapeChain (ch, osBuf[(size_t)i], color, post);
            ch.os.down (osBuf.data (), wet.data (), n);
        }
        else
            for (int i = 0; i < n; ++i)
                wet[(size_t)i] = ch.wetDelay.push (shapeChain (ch, pre[c][(size_t)i], color, post));
        for (int k = 0; k < kClarityBands; ++k)
            if (clarity[k])
                for (int i = 0; i < n; ++i)
                {
                    const float w = wet[(size_t)i];
                    const double band = ch.postLp[k].process (ch.postHp[k].process (w)) * bandNorm[k];
                    wet[(size_t)i] = (float)(w + (gPost[k][(size_t)i] - 1.0f) * band);
                }
        for (int i = 0; i < n; ++i)
        {
            const float w = wet[(size_t)i], m = gMix[(size_t)i];
            outPk = std::max (outPk, std::fabs (w));
            outs[c][i] = (dry[c][(size_t)i] * (1.0f - m) + w * m) * gOut[(size_t)i];
        }
    }
    if (meters)
    {
        meters->inPeak.store (inPk, std::memory_order_relaxed);
        meters->outPeak.store (outPk, std::memory_order_relaxed);
        meters->clarityDb.store (clarity[0] ? -lmCutDb[0] : 0.0f, std::memory_order_relaxed);
        meters->clarity2Db.store (clarity[1] ? -lmCutDb[1] : 0.0f, std::memory_order_relaxed);
    }
}

} // namespace smacheratr
