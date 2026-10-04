#include "Engine.h"

#include "ClarityBand.h"
#include "Color.h"
#include "Glue.h"
#include "NoOverlap.h"

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
    for (int k = 0; k < kGentlrBands; ++k)
    {
        bandHp[k].reset ();
        bandLp[k].reset ();
        postHp[k].reset ();
        postLp[k].reset ();
        bandHp2[k].reset ();
        postHp2[k].reset ();
    }
    dc.reset ();
    preLo.reset ();
    preHi.reset ();
    postLo.reset ();
    postHi.reset ();
    os.reset ();
    regionOs.reset ();
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
    for (int k = 0; k < kGentlrBands; ++k)
        bandFreq[k] = bandWidth[k] = -1.0; // Clarity's bands are designed for this rate on the next block
    // the limiter's gain (in dB) reaches its target within the look-ahead and releases in 50 ms
    limAtk = (float)std::exp (-5.0 / look);
    limRel = (float)std::exp (-1.0 / (0.050 * sr));
    lookPeaks.assign ((size_t)look, 0.0f);
    for (auto& c : chan)
    {
        c.os.prepare (sr, maxBlock);
        c.regionOs.prepare (sr, maxBlock);
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
        region[c].assign ((size_t)maxBlock, 0.0f);
    }
    for (auto* v : {&wet, &gDrive, &gOut, &gMix, &msMid, &msSide, &gPost[0], &gPost[1], &gPost[2], &gPost[3], &gRegion, &gRegionMix})
        v->assign ((size_t)maxBlock, 0.0f);
    osBuf.assign ((size_t)maxBlock * 4, 0.0f);
    regionOsBuf.assign ((size_t)maxBlock * 4, 0.0f);
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
    regionGain = dbToGain (p[kClarityDriveAmount]);
    regionMix = 0.0f;
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

void Engine::clipCeiling (float* yl, float* yr, int n) const
{
    // Soft and Hard Clip: nothing leaves above 0 dBFS. The clip after the curve (in the oversampled
    // path) is the sound, soft or hard; what comes after it can rise above it again (Hi-Quality's downsampling filter
    // overshooting the clipped edges, Gentlr's band filters, the dry part of a mix, Output, Mid/Side
    // back to left / right), so the very end is held to 0 dBFS too: only those overshoots are cut.
    if ((int)std::lround (p[kPostClip]) == kPostOff)
        return;
    for (int i = 0; i < n; ++i)
    {
        yl[i] = std::clamp (yl[i], -1.0f, 1.0f);
        yr[i] = std::clamp (yr[i], -1.0f, 1.0f);
    }
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
                meters->clarityLevelDb.store (-120.0f, std::memory_order_relaxed);
                meters->clarity2LevelDb.store (-120.0f, std::memory_order_relaxed);
                meters->claritySubDb.store (0.0f, std::memory_order_relaxed);
                meters->claritySubLevelDb.store (-120.0f, std::memory_order_relaxed);
                meters->clarityHighDb.store (0.0f, std::memory_order_relaxed);
                meters->clarityHighLevelDb.store (-120.0f, std::memory_order_relaxed);
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
        clipCeiling (yl, yr, n); // (mid and side each held to 0 dB add up to twice that in a channel)
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
            c.regionOs.reset ();
            c.wetDelay.reset ();
            c.lookDelay.reset ();
        }
        regionMix = 0.0f;
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

    // Gentlr (called Clarity before), a compressor on up to four bands (one button; a band works while
    // its Range is above 0; the third and fourth are the Sub and High bands, shelves at the ends of the
    // spectrum, which also need their own buttons) (ClarityBand.h: the two bands' shape is the Slope,
    // 12 / 12 dB/oct, Signature 24 / 12 or Classic 12 / 6, around each band's frequency): when the drive pushes a band past its threshold into the curve (-18 dBFS, or the
    // band's Threshold with Advanced on), it is turned down before the curve (3 dB for every 5 over, at
    // most the band's Range: clarityCutDb), so it does not pile up into mud and intermodulate, and
    // after it by half as much (the curve squashes the cut before it back up). The bands work one
    // after the other, each measuring its own band.
    const bool advanced = p[kClarityAdvanced] >= 0.5;
    bool clarity[kGentlrBands];
    double levelDb[kGentlrBands] = {-120.0, -120.0, -120.0, -120.0};
    for (int k = 0; k < kGentlrBands; ++k)
        clarity[k] = clarityBandOn (p[kClarity], p[kGentlrRangeIds[k]]); // (Sub and High too: no button of their own)
    // where the bands sit: as set, glued borders held (Glue.h), and with No Overlap kept apart (what the
    // editors write already holds both, and is left as it is)
    GentlrLayout layout;
    for (int k = 0; k < kGentlrBands; ++k)
    {
        layout.on[k] = clarity[k];
        layout.freq[k] = p[kGentlrFreqIds[k]];
        layout.width[k] = hasWidth (k) ? p[kClarityWidthIds[k]] : 0.0; // (the Sub and High bands have none)
    }
    bool glued[kGluePairs];
    for (int g = 0; g < kGluePairs; ++g)
        glued[g] = p[kClarityGlueIds[g]] >= 0.5;
    applyGlue (layout, glued);
    if (p[kClarityNoOverlap] >= 0.5)
        resolveOverlaps (layout);
    for (int k = 0; k < kGentlrBands; ++k)
    {
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
                c.bandHp2[k].reset ();
                c.postHp2[k].reset ();
            }
        }
        if (!clarity[k])
            continue;
        levelDb[k] = 10.0 * std::log10 (std::max (1e-12, lmEnv[k]));
        const double thresholdDb = advanced ? p[kGentlrThresholdIds[k]] : kClarityThresholdDb;
        lmCutDb[k] = (float)clarityCutDb (levelDb[k], thresholdDb, p[kGentlrRangeIds[k]]);
        // (the Slope shapes the two bands, not the Sub and High shelves)
        const int slope = hasWidth (k) ? claritySlopeOf (p[kClaritySlope]) : kSlopeClassic;
        if (layout.freq[k] != bandFreq[k] || layout.width[k] != bandWidth[k] || slope != bandSlope[k])
        {
            bandFreq[k] = layout.freq[k];
            bandWidth[k] = layout.width[k];
            bandSlope[k] = slope;
            const ClarityBand b = k == kSubBand    ? subBand (sr, bandFreq[k])
                                  : k == kHighBand ? highBand (sr, bandFreq[k])
                                                   : clarityBand (sr, bandFreq[k], bandWidth[k], slope);
            if (b.hp2On && !bandHp2On[k])
                for (auto& c : chan)
                {
                    // the second section comes in from rest (it held whatever it had when last used)
                    c.bandHp2[k].reset ();
                    c.postHp2[k].reset ();
                }
            bandHp2On[k] = b.hp2On;
            for (auto& c : chan)
            {
                c.bandHp[k].c = c.postHp[k].c = b.hp;
                c.bandLp[k].c = c.postLp[k].c = b.lp;
                c.bandHp2[k].c = c.postHp2[k].c = b.hp2;
            }
            bandNorm[k] = (float)b.norm;
        }
    }

    // Gentlr's region drive (Advanced): faded in and out, and only running while it is in, so with it
    // off nothing here touches the sound
    const float regionMixT = advanced && p[kClarityDrive] >= 0.5 && (clarity[0] || clarity[1] || clarity[kSubBand] || clarity[kHighBand]) ? 1.0f : 0.0f;
    const float regionGainT = dbToGain (std::clamp (p[kClarityDriveAmount], 0.0, 36.0));
    if (regionMixT <= 0.0f && regionMix < 1e-5f)
        regionMix = 0.0f;
    else if (regionMix <= 0.0f)
    {
        // starting from silence: the region's oversampler starts empty, as if the region had been silent
        for (auto& c : chan)
            c.regionOs.reset ();
        regionGain = regionGainT;
    }
    const bool regionOn = regionMix > 0.0f || regionMixT > 0.0f;
    if (regionOn)
        for (int i = 0; i < n; ++i)
        {
            regionGain += (regionGainT - regionGain) * smooth;
            regionMix += (regionMixT - regionMix) * smooth;
            gRegion[(size_t)i] = regionGain;
            gRegionMix[(size_t)i] = regionMix;
        }

    // DC filter, look-ahead delay and the stereo-linked pre-limiter, then the drive
    float inPk = 0.0f, outPk = 0.0f;
    float gPreTarget[kGentlrBands], gPostTarget[kGentlrBands];
    for (int k = 0; k < kGentlrBands; ++k)
    {
        gPreTarget[k] = dbToGain (-lmCutDb[k]);
        gPostTarget[k] = dbToGain (-0.5 * lmCutDb[k]);
    }
    for (int i = 0; i < n; ++i)
    {
        for (int k = 0; k < kGentlrBands; ++k)
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
        double lmPower[kGentlrBands] = {0.0, 0.0, 0.0, 0.0};
        for (int c = 0; c < 2; ++c)
        {
            float d = chan[c].lookDelay.push (v[c]) * limGain * gDrive[(size_t)i];
            double cutBands = 0.0; // the bands as they leave, after their cuts (the region drive's input)
            for (int k = 0; k < kGentlrBands; ++k)
                if (clarity[k])
                {
                    const double h = chan[c].bandHp[k].process (d);
                    const double band = chan[c].bandLp[k].process (bandHp2On[k] ? chan[c].bandHp2[k].process (h) : h) * bandNorm[k];
                    lmPower[k] = std::max (lmPower[k], 2.0 * band * band); // a sine's peak level
                    d = (float)(d + (gBandPre[k] - 1.0f) * band);
                    cutBands += gBandPre[k] * band;
                }
            pre[c][(size_t)i] = d;
            if (regionOn)
                region[c][(size_t)i] = (float)cutBands;
            inPk = std::max (inPk, std::fabs (d));
        }
        for (int k = 0; k < kGentlrBands; ++k)
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
            if (regionOn)
            {
                // the region, driven at the oversampled rate and added to what goes into the curve
                ch.regionOs.up (region[c].data (), regionOsBuf.data (), n);
                for (int i = 0; i < 4 * n; ++i)
                {
                    const size_t j = (size_t)(i / 4);
                    osBuf[(size_t)i] += (float)(gRegionMix[j] * clarityRegionDrive (regionOsBuf[(size_t)i], gRegion[j]));
                }
            }
            for (int i = 0; i < 4 * n; ++i)
                osBuf[(size_t)i] = shapeChain (ch, osBuf[(size_t)i], color, post);
            ch.os.down (osBuf.data (), wet.data (), n);
        }
        else
        {
            if (regionOn)
                for (int i = 0; i < n; ++i)
                    pre[c][(size_t)i] += (float)(gRegionMix[(size_t)i] * clarityRegionDrive (region[c][(size_t)i], gRegion[(size_t)i]));
            for (int i = 0; i < n; ++i)
                wet[(size_t)i] = ch.wetDelay.push (shapeChain (ch, pre[c][(size_t)i], color, post));
        }
        for (int k = 0; k < kGentlrBands; ++k)
            if (clarity[k])
                for (int i = 0; i < n; ++i)
                {
                    const float w = wet[(size_t)i];
                    const double h = ch.postHp[k].process (w);
                    const double band = ch.postLp[k].process (bandHp2On[k] ? ch.postHp2[k].process (h) : h) * bandNorm[k];
                    wet[(size_t)i] = (float)(w + (gPost[k][(size_t)i] - 1.0f) * band);
                }
        for (int i = 0; i < n; ++i)
        {
            const float w = wet[(size_t)i], m = gMix[(size_t)i];
            outPk = std::max (outPk, std::fabs (w));
            outs[c][i] = (dry[c][(size_t)i] * (1.0f - m) + w * m) * gOut[(size_t)i];
        }
    }
    if (!inMs)
        clipCeiling (yl, yr, n);
    if (meters)
    {
        meters->inPeak.store (inPk, std::memory_order_relaxed);
        meters->outPeak.store (outPk, std::memory_order_relaxed);
        meters->clarityDb.store (clarity[0] ? -lmCutDb[0] : 0.0f, std::memory_order_relaxed);
        meters->clarity2Db.store (clarity[1] ? -lmCutDb[1] : 0.0f, std::memory_order_relaxed);
        meters->clarityLevelDb.store ((float)levelDb[0], std::memory_order_relaxed);
        meters->clarity2LevelDb.store ((float)levelDb[1], std::memory_order_relaxed);
        meters->claritySubDb.store (clarity[kSubBand] ? -lmCutDb[kSubBand] : 0.0f, std::memory_order_relaxed);
        meters->claritySubLevelDb.store ((float)levelDb[kSubBand], std::memory_order_relaxed);
        meters->clarityHighDb.store (clarity[kHighBand] ? -lmCutDb[kHighBand] : 0.0f, std::memory_order_relaxed);
        meters->clarityHighLevelDb.store ((float)levelDb[kHighBand], std::memory_order_relaxed);
    }
}

} // namespace smacheratr
