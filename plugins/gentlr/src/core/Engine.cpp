#include "Engine.h"

#include "smacheratr/src/core/ClarityBand.h"
#include "smacheratr/src/core/NoOverlap.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
#include <xmmintrin.h>
#define GENTLR_SSE 1
#endif

namespace gentlr {

namespace {
inline float dbToGain (double db) { return (float)std::pow (10.0, db / 20.0); }
inline double coeffOfMs (double ms, double sr) { return 1.0 - std::exp (-1.0 / (std::max (0.01, ms) * 0.001 * sr)); }

// Flushes denormals to zero while alive (as Levlr does): the band filters decay towards them after
// the audio stops, and they are slow on x86.
class NoDenormals
{
public:
    NoDenormals ()
    {
#if defined(GENTLR_SSE)
        old = _mm_getcsr ();
        _mm_setcsr ((unsigned int)(old | 0x8040)); // FTZ | DAZ
#elif defined(__aarch64__) && !defined(_MSC_VER)
        uint64_t fpcr;
        asm volatile ("mrs %0, fpcr" : "=r"(fpcr));
        old = fpcr;
        fpcr |= (uint64_t)1 << 24; // FZ
        asm volatile ("msr fpcr, %0" : : "r"(fpcr));
#endif
    }
    ~NoDenormals ()
    {
#if defined(GENTLR_SSE)
        _mm_setcsr ((unsigned int)old);
#elif defined(__aarch64__) && !defined(_MSC_VER)
        asm volatile ("msr fpcr, %0" : : "r"(old));
#endif
    }
    NoDenormals (const NoDenormals&) = delete;
    NoDenormals& operator= (const NoDenormals&) = delete;

private:
    uint64_t old = 0;
};
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
    for (int k = 0; k < kAllBands; ++k)
    {
        hp[k].reset ();
        lp[k].reset ();
        hp2[k].reset ();
    }
    os.reset ();
    dryDelay.reset ();
    wetDelay.reset ();
}

void Engine::prepare (double sampleRate, int maxBlock)
{
    sr = sampleRate;
    tail.prepare (sr, maxBlock);
    for (uint32_t id = kTailBase; id < kNumParams; ++id)
        if (isTailParam (id))
            tail.setParam (tailField (id), p[id]);
    for (auto& c : chan)
    {
        c.os.prepare (sr, kChunk);
        c.dryDelay.resize (c.os.latency ());
        c.wetDelay.resize (c.os.latency ());
    }
    osBuf.assign ((size_t)kChunk * 4, 0.0f);
    smooth = (float)(1.0 - std::exp (-1.0 / (0.02 * sr)));     // 20 ms on Output, Mix and the Drive
    smoothCut = (float)(1.0 - std::exp (-1.0 / (0.002 * sr))); // 2 ms on the cuts (they move every kCtrl samples)
    fxStep = (float)(1.0 / (0.005 * sr));                       // 5 ms out, 5 ms back in
    for (int k = 0; k < kAllBands; ++k)
        bandFreq[k] = bandWidth[k] = -1.0; // the bands are designed for this rate
    bool works[kAllBands];
    for (int k = 0; k < kAllBands; ++k)
        works[k] = bandWorks (k, p[onParam (k)], p[rangeParam (k)]);
    retune (true, works);
    if (meters)
        meters->sampleRate.store ((float)sr);
    reset ();
}

void Engine::resetBand (int k)
{
    for (int c = 0; c < 2; ++c)
    {
        chan[c].hp[k].reset ();
        chan[c].lp[k].reset ();
        chan[c].hp2[k].reset ();
        env[c][k] = 0.0;
        cutDb[c][k] = 0.0f;
        gBand[c][k] = gTarget[c][k] = 1.0f;
    }
}

void Engine::reset ()
{
    for (auto& c : chan)
        c.reset ();
    for (int k = 0; k < kAllBands; ++k)
    {
        resetBand (k);
        running[k] = bandWorks (k, p[onParam (k)], p[rangeParam (k)]);
    }
    mode = std::clamp ((int)std::lround (p[kStereo]), 0, kNumStereoModes - 1);
    fx = 1.0f;
    hold = 0;
    ctrlCountdown = 0;
    out = dbToGain (p[kOutput]);
    mix = (float)std::clamp (p[kMix], 0.0, 1.0);
    regionGain = dbToGain (std::clamp (p[kDriveAmount], 0.0, 36.0));
    regionMix = 0.0f;
    tail.reset ();
}

void Engine::setParam (uint32_t id, double plain)
{
    if (id >= kNumParams)
        return;
    p[id] = plain;
    if (isTailParam (id))
        tail.setParam (tailField (id), plain);
}

void Engine::retune (bool force, const bool* works)
{
    // where the bands sit: as set, or with No Overlap the working ones kept apart
    smacheratr::GentlrLayout layout;
    for (int k = 0; k < kAllBands; ++k)
    {
        layout.on[k] = works[k];
        layout.freq[k] = p[freqParam (k)];
        layout.width[k] = hasWidth (k) ? p[bandParam (k, kWidth)] : 0.0; // (the Sub and High bands have no width)
    }
    if (p[kNoOverlap] >= 0.5)
        smacheratr::resolveOverlaps (layout);
    for (int k = 0; k < kAllBands; ++k)
    {
        const double f = layout.freq[k], w = layout.width[k];
        // (the Slope shapes bands 1 and 2, not the Sub and High shelves)
        const int slope = hasWidth (k) ? smacheratr::claritySlopeOf (p[kSlope]) : smacheratr::kSlopeClassic;
        if (!force && f == bandFreq[k] && w == bandWidth[k] && slope == bandSlope[k])
            continue;
        const smacheratr::ClarityBand b = k == kSub    ? smacheratr::subBand (sr, f)
                                          : k == kHigh ? smacheratr::highBand (sr, f)
                                                       : smacheratr::clarityBand (sr, f, w, slope);
        bandFreq[k] = f;
        bandWidth[k] = w;
        bandSlope[k] = slope;
        bandNorm[k] = (float)b.norm;
        if (b.hp2On && !hp2On[k])
            for (auto& c : chan)
                c.hp2[k].reset (); // the second section comes in from rest (it held whatever it had when last used)
        hp2On[k] = b.hp2On;
        for (auto& c : chan)
        {
            c.hp[k].c = b.hp;
            c.lp[k].c = b.lp;
            c.hp2[k].c = b.hp2;
        }
    }
}

void Engine::process (const float* inL, const float* inR, float* outL, float* outR, int n)
{
    const NoDenormals guard;
    // in pieces the size of the work buffers
    for (int pos = 0; pos < n; pos += kChunk)
    {
        const int m = std::min (kChunk, n - pos);
        processChunk (inL + pos, inR + pos, outL + pos, outR + pos, m);
    }
}

void Engine::processChunk (const float* inL, const float* inR, float* outL, float* outR, int n)
{
    // this chunk's settings
    const float outT = dbToGain (p[kOutput]), mixT = (float)std::clamp (p[kMix], 0.0, 1.0);
    const int modeT = std::clamp ((int)std::lround (p[kStereo]), 0, kNumStereoModes - 1);
    const bool advanced = p[kAdvanced] >= 0.5;
    atk = coeffOfMs (p[kAttack], sr);
    rel = coeffOfMs (p[kRelease], sr);
    bool works[kAllBands], anyWorks = false;
    double thresholdDb[kAllBands], rangeDb[kAllBands];
    for (int k = 0; k < kAllBands; ++k)
        works[k] = bandWorks (k, p[onParam (k)], p[rangeParam (k)]);
    retune (false, works);
    for (int k = 0; k < kAllBands; ++k)
    {
        anyWorks |= works[k];
        if (works[k] && !running[k])
        {
            resetBand (k); // starting: from silence, no cut
            running[k] = true;
        }
        thresholdDb[k] = advanced ? p[thresholdParam (k)] : smacheratr::kClarityThresholdDb;
        rangeDb[k] = p[rangeParam (k)];
    }

    // the region Drive (Advanced): faded in and out, and only running while it is in, so with it off
    // nothing here touches the sound
    const float regionMixT = advanced && p[kDrive] >= 0.5 && anyWorks ? 1.0f : 0.0f;
    const float regionGainT = dbToGain (std::clamp (p[kDriveAmount], 0.0, 36.0));
    if (regionMixT <= 0.0f && regionMix < 1e-5f)
        regionMix = 0.0f;
    else if (regionMix <= 0.0f)
    {
        // starting from silence: the oversamplers start empty, as if the region had been silent
        for (auto& c : chan)
            c.os.reset ();
        regionGain = regionGainT;
    }
    const bool regionOn = regionMix > 0.0f || regionMixT > 0.0f;

    // the stereo mode in use: which channels are worked on, whether they share a detector
    bool ms = false, linked = true, proc[2] = {true, true};
    auto modeFlags = [&] {
        ms = mode != kStereoLinked;
        linked = mode == kStereoLinked;
        proc[0] = mode != kSideOnly;
        proc[1] = mode != kMidOnly;
    };
    modeFlags ();
    // a new mode waits until Gentlr has faded out and the region Drive's oversamplers have emptied
    const int holdLen = 2 * chan[0].os.latency () + 8;

    for (int i = 0; i < n; ++i)
    {
        if (modeT != mode)
        {
            if (fx > 0.0f)
                fx = std::max (0.0f, fx - fxStep);
            else if (hold < holdLen)
                ++hold;
            else
            {
                mode = modeT;
                hold = 0;
                modeFlags ();
                for (int k = 0; k < kAllBands; ++k)
                    resetBand (k);
                for (auto& c : chan)
                    c.os.reset ();
            }
        }
        else
        {
            hold = 0;
            if (fx < 1.0f)
                fx = std::min (1.0f, fx + fxStep);
        }

        // the cuts, from the detectors (every kCtrl samples), and the bands' gains gliding to them
        if (--ctrlCountdown < 0)
        {
            ctrlCountdown = kCtrl - 1;
            for (int e = 0; e < (linked ? 1 : 2); ++e)
                for (int k = 0; k < kAllBands; ++k)
                {
                    const double level = 10.0 * std::log10 (std::max (1e-12, env[e][k]));
                    cutDb[e][k] = works[k] ? (float)smacheratr::clarityCutDb (level, thresholdDb[k], rangeDb[k]) : 0.0f;
                    gTarget[e][k] = cutDb[e][k] > 0.0f ? dbToGain (-cutDb[e][k]) : 1.0f;
                }
        }
        for (int e = 0; e < (linked ? 1 : 2); ++e)
            for (int k = 0; k < kAllBands; ++k)
                if (running[k])
                    gBand[e][k] += (gTarget[e][k] - gBand[e][k]) * smoothCut;

        if (regionOn)
        {
            regionGain += (regionGainT - regionGain) * smooth;
            regionMix += (regionMixT - regionMix) * smooth;
            gRegion[i] = regionGain;
            gRegionMix[i] = regionMix;
        }

        const float xl = inL[i], xr = inR[i];
        dryBuf[0][i] = chan[0].dryDelay.push (xl);
        dryBuf[1][i] = chan[1].dryDelay.push (xr);
        inMono[i] = 0.5f * (xl + xr); // (the input may be the output buffer)
        float v[2] = {xl, xr};
        if (ms)
        {
            v[0] = 0.5f * (xl + xr);
            v[1] = 0.5f * (xl - xr);
        }
        double power[2][kAllBands] = {};
        for (int c = 0; c < 2; ++c)
        {
            region[c][i] = 0.0f;
            if (!proc[c])
                continue;
            const int e = linked ? 0 : c;
            double d = v[c], cutBands = 0.0; // cutBands: the bands as they leave, after their cuts (the region Drive's input)
            for (int k = 0; k < kAllBands; ++k)
                if (running[k])
                {
                    const double h = chan[c].hp[k].process (d);
                    const double band = chan[c].lp[k].process (hp2On[k] ? chan[c].hp2[k].process (h) : h) * bandNorm[k];
                    power[e][k] = std::max (power[e][k], 2.0 * band * band); // a sine's peak level
                    const double g = 1.0 + (gBand[e][k] - 1.0) * fx;
                    d += (g - 1.0) * band;
                    cutBands += g * band;
                }
            v[c] = (float)d;
            if (regionOn)
                region[c][i] = (float)(fx * cutBands);
        }
        for (int e = 0; e < (linked ? 1 : 2); ++e)
            for (int k = 0; k < kAllBands; ++k)
                if (works[k])
                    env[e][k] += (power[e][k] - env[e][k]) * (power[e][k] > env[e][k] ? atk : rel);
        // back to left / right, then the delay that lines up with the region Drive
        const float yl = ms ? v[0] + v[1] : v[0], yr = ms ? v[0] - v[1] : v[1];
        wetBuf[0][i] = chan[0].wetDelay.push (yl);
        wetBuf[1][i] = chan[1].wetDelay.push (yr);
    }

    // a band that stopped working runs until its cut has let go, then rests
    for (int k = 0; k < kAllBands; ++k)
        if (running[k] && !works[k])
        {
            bool done = true;
            for (int e = 0; e < 2; ++e)
                done &= std::fabs (gBand[e][k] - 1.0f) < 1e-5f;
            if (done)
            {
                resetBand (k);
                running[k] = false;
            }
        }

    // the region Drive: the cut bands driven at 4x through the Analog curve, level matched
    if (regionOn)
    {
        for (int c = 0; c < 2; ++c)
        {
            Channel& ch = chan[c];
            ch.os.up (region[c], osBuf.data (), n);
            for (int j = 0; j < 4 * n; ++j)
            {
                const int s = j / 4;
                osBuf[(size_t)j] = (float)(gRegionMix[s] * smacheratr::clarityRegionDrive (osBuf[(size_t)j], gRegion[s]));
            }
            ch.os.down (osBuf.data (), regionOut[c], n);
        }
        for (int i = 0; i < n; ++i)
        {
            const float a = regionOut[0][i], b = regionOut[1][i];
            wetBuf[0][i] += ms ? a + b : a;
            wetBuf[1][i] += ms ? a - b : b;
        }
    }

    // Mix and Output
    for (int i = 0; i < n; ++i)
    {
        out += (outT - out) * smooth;
        mix += (mixT - mix) * smooth;
        outL[i] = (dryBuf[0][i] * (1.0f - mix) + wetBuf[0][i] * mix) * out;
        outR[i] = (dryBuf[1][i] * (1.0f - mix) + wetBuf[1][i] * mix) * out;
    }
    if (hasTail)
        tail.process (outL, outR, n);

    if (meters)
    {
        // the analyser: the input, and the output after the tail
        for (int i = 0; i < n; ++i)
            meters->scope.push (inMono[i], 0.5f * (outL[i] + outR[i]));
        // each band's cut and level: the larger of the two detectors when they work apart
        float cut[kAllBands], level[kAllBands];
        for (int k = 0; k < kAllBands; ++k)
        {
            cut[k] = 0.0f;
            level[k] = -120.0f;
            if (!works[k])
                continue;
            double pw = 0.0;
            for (int e = 0; e < (linked ? 1 : 2); ++e)
                if (linked || proc[e])
                {
                    cut[k] = std::max (cut[k], cutDb[e][k] * fx);
                    pw = std::max (pw, env[e][k]);
                }
            level[k] = (float)(10.0 * std::log10 (std::max (1e-12, pw)));
        }
        meters->bands.clarityDb.store (-cut[0], std::memory_order_relaxed);
        meters->bands.clarity2Db.store (-cut[1], std::memory_order_relaxed);
        meters->bands.clarityLevelDb.store (level[0], std::memory_order_relaxed);
        meters->bands.clarity2LevelDb.store (level[1], std::memory_order_relaxed);
        meters->bands.claritySubDb.store (-cut[kSub], std::memory_order_relaxed);
        meters->bands.claritySubLevelDb.store (level[kSub], std::memory_order_relaxed);
        meters->bands.clarityHighDb.store (-cut[kHigh], std::memory_order_relaxed);
        meters->bands.clarityHighLevelDb.store (level[kHigh], std::memory_order_relaxed);
        meters->blocks.fetch_add (1, std::memory_order_relaxed);
    }
}

} // namespace gentlr
