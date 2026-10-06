#include "Engine.h"

#include "pluginkit/NoDenormals.h"

#include <algorithm>
#include <cmath>

namespace moistr {

namespace {
inline float dbToGain (double db) { return (float)std::pow (10.0, db / 20.0); }
constexpr double kOffTarget = -100.0; // a band switched off glides down to here, then is silent
// the second stage's damping for the low- and high-pass at 24 dB (a Butterworth stage: the first one's
// resonance stays as it is)
constexpr double kButterworthK = 1.41421356237309504880;
constexpr uint32_t kFreqIds[kBands] = {kLowFreq, kMidFreq, kHighFreq};
constexpr uint32_t kResIds[kBands] = {kLowRes, kMidRes, kHighRes};
constexpr uint32_t kLevelIds[kBands] = {kLowLevel, kMidLevel, kHighLevel};
constexpr uint32_t kMoveIds[kBands] = {kLowMove, kMidMove, kHighMove};
// glides x towards t by c, landing on it once close (so a still setting is exactly its value)
inline void glide (double& x, double t, double c)
{
    x += (t - x) * c;
    if (std::fabs (t - x) < 1e-9)
        x = t;
}
} // namespace

ParamArray defaultParams ()
{
    ParamArray p {};
    for (uint32_t i = 0; i < kNumParams; ++i)
        p[i] = paramTable ().info (i).def;
    return p;
}

double Engine::setFrequency (const ParamArray& p, int band)
{
    const double f = p[kFreqIds[band]];
    const double gap = std::clamp (p[kGap], -1.0, 1.0);
    if (band == 1)
        return f * std::exp2 (-gap);
    if (band == 2)
        return f * std::exp2 (gap);
    return f;
}

void Engine::PassState::resetFilters ()
{
    for (int b = 0; b < kBands; ++b)
        for (int c = 0; c < 2; ++c)
        {
            stage1[b][c].reset ();
            stage2[b][c].reset ();
        }
}

Engine::Engine ()
{
    drive.maxDb = 18.0;
    for (auto& s : state)
        s.grit.maxDb = 24.0;
    applyPattern ();
}

void Engine::prepare (double sampleRate, int maxBlock)
{
    sr = sampleRate;
    tickSmooth = 1.0 - std::exp (-(double)kTick / (0.03 * sr));
    smooth = (float)(1.0 - std::exp (-1.0 / (0.02 * sr)));
    for (auto& s : state)
        s.glue.prepare (sr);
    tail.prepare (sr, std::max (1, maxBlock));
    for (uint32_t id = 0; id < kNumParams; ++id)
        setParam (id, p[id]);
    reset ();
}

void Engine::applyPattern ()
{
    const int seed = std::clamp ((int)std::lround (p[kSeed]), kMinSeed, kMaxSeed);
    for (int k = 0; k < kMaxPasses; ++k)
        patterns[k] = makePattern (seed, k);
}

void Engine::reset ()
{
    // every smoothed setting at its value
    for (int b = 0; b < kBands; ++b)
    {
        logFreq[b] = std::log2 (setFrequency (p, b));
        levelDb[b] = p[kLevelIds[b]] <= kLevelOffDb ? kOffTarget : p[kLevelIds[b]];
        res[b] = p[kResIds[b]];
    }
    move = std::clamp (p[kMovement], 0.0, 1.0);
    levelMove = p[kLevelMove];
    theta = 0.0;
    wasPlaying = false;
    drive.reset ();
    for (int k = 0; k < kMaxPasses; ++k)
    {
        PassState& s = state[k];
        s.resetFilters ();
        s.glue.reset ();
        s.grit.reset ();
        double g[kBands], gain[kBands];
        targets (k, theta, g, gain);
        for (int b = 0; b < kBands; ++b)
        {
            s.gNow[b] = g[b];
            s.gainNow[b] = gain[b];
        }
    }
    pass2 = std::lround (p[kPasses]) == kPasses2 ? 1.0 : 0.0;
    mix = (float)std::clamp (p[kMix], 0.0, 1.0);
    out = dbToGain (p[kOutput]);
    quiet = (int)sr;
    tail.reset ();
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
        tail.setParam (smacheratr::kTailExt2First + (id - kTailExt2Base), plain);
    else if (id >= kTailExtBase)
        tail.setParam (pk::kTailFields + (id - kTailExtBase), plain);
    else if (id >= kTailBase)
        tail.setParam (id - kTailBase, plain);
    else
        switch (id)
        {
            case kDrive: drive.setAmount (plain); break;
            case kGlue:
                for (auto& s : state)
                    s.glue.setAmount (plain);
                break;
            case kGrit:
                for (auto& s : state)
                    s.grit.setAmount (plain);
                break;
            case kSeed: applyPattern (); break;
            default: break; // (the rest are read where they are used)
        }
}

void Engine::setTransport (double tempo, double ppq, bool isPlaying)
{
    bpm = tempo > 1.0 ? tempo : 120.0;
    songPpq = ppq;
    playing = isPlaying;
    transportSet = true;
}

void Engine::targets (int pass, double th, double* g, double* gain)
{
    const Pattern& pat = patterns[pass];
    PassState& s = state[pass];
    const double fMax = 0.45 * sr;
    for (int b = 0; b < kBands; ++b)
    {
        const BandPattern& bp = pat.band[b];
        const double amount = move * std::clamp (p[kMoveIds[b]], 0.0, 1.0) * bp.depth;
        double oct = 0.0, db = levelDb[b];
        if (amount > 0.0)
        {
            oct = amount * (kSwingOctaves * bp.freq.value (th) + kOffsetOctaves * bp.offset);
            db += amount * levelMove * bp.level.value (th);
        }
        const double f = std::clamp (std::exp2 (logFreq[b] + oct), 20.0, fMax);
        g[b] = std::tan (dsp::kPi * f / sr);
        const bool off = p[kLevelIds[b]] <= kLevelOffDb && levelDb[b] <= kOffTarget + 1.0;
        gain[b] = off ? 0.0 : std::pow (10.0, db / 20.0);
        s.freqNow[b] = f;
        s.levelDbNow[b] = off ? kOffTarget : db;
    }
}

void Engine::runPass (int pass, double* l, double* r, int m, double thetaEnd)
{
    PassState& s = state[pass];
    double g1[kBands], gain1[kBands], k1[kBands], k2[kBands];
    targets (pass, thetaEnd, g1, gain1);
    for (int b = 0; b < kBands; ++b)
    {
        k1[b] = 1.0 / dsp::qOf (res[b]);
        k2[b] = b == 1 ? k1[b] : kButterworthK;
    }
    const bool steep = std::lround (p[kSlope]) == kSlope24;
    dsp::SvfCoefs c1[kBands], c2[kBands];
    for (int i = 0; i < m; ++i)
    {
        const double t = (double)(i + 1) / m;
        double gain[kBands];
        for (int b = 0; b < kBands; ++b)
        {
            const double g = s.gNow[b] + (g1[b] - s.gNow[b]) * t;
            c1[b].set (g, k1[b]);
            c2[b].set (g, k2[b]);
            gain[b] = s.gainNow[b] + (gain1[b] - s.gainNow[b]) * t;
        }
        double* io[2] = {l + i, r + i};
        for (int c = 0; c < 2; ++c)
        {
            const double x = *io[c];
            // Low: low-pass
            const double lo1 = s.stage1[0][c].tick (x, c1[0]).lp;
            const double lo2 = s.stage2[0][c].tick (lo1, c2[0]).lp;
            // Mid: band-pass (k x the band output: 0 dB at its centre)
            const double mi1 = c1[1].k * s.stage1[1][c].tick (x, c1[1]).bp;
            const double mi2 = c2[1].k * s.stage2[1][c].tick (mi1, c2[1]).bp;
            // High: high-pass
            const double hi1 = s.stage1[2][c].tick (x, c1[2]).hp;
            const double hi2 = s.stage2[2][c].tick (hi1, c2[2]).hp;
            *io[c] = gain[0] * (steep ? lo2 : lo1) + gain[1] * (steep ? mi2 : mi1) + gain[2] * (steep ? hi2 : hi1);
        }
    }
    for (int b = 0; b < kBands; ++b)
    {
        s.gNow[b] = g1[b];
        s.gainNow[b] = gain1[b];
    }
    s.glue.process (l, r, m);
    s.grit.process (l, r, m);
}

void Engine::process (const float* xl, const float* xr, float* yl, float* yr, int n)
{
    const pk::NoDenormals guard;
    const bool sync = p[kSync] >= 0.5;
    const double beats = kSyncBeats[std::clamp ((int)std::lround (p[kSyncRate]), 0, kNumSyncRates - 1)];
    const double rate = p[kRate];
    // where the movement is: on the song's timeline while the host plays (synced: locked to it; free: set
    // from it when playback starts or jumps, then running at Rate), else running on its own
    if (playing)
    {
        if (sync)
            theta = songPpq / beats;
        else if (!wasPlaying || std::fabs (songPpq - expectPpq) > 1e-3)
            theta = songPpq * 60.0 / bpm * rate;
        expectPpq = songPpq + n * bpm / 60.0 / sr;
    }
    wasPlaying = playing;
    const double dTheta = sync ? bpm / 60.0 / beats / sr : rate / sr;

    const float mixT = (float)std::clamp (p[kMix], 0.0, 1.0), outT = dbToGain (p[kOutput]);
    const bool twoPasses = std::lround (p[kPasses]) == kPasses2;
    const double fadeStep = (double)kTick / (0.02 * sr);
    double targetLogF[kBands], targetDb[kBands];
    for (int b = 0; b < kBands; ++b)
    {
        targetLogF[b] = std::log2 (setFrequency (p, b));
        targetDb[b] = p[kLevelIds[b]] <= kLevelOffDb ? kOffTarget : p[kLevelIds[b]];
    }

    float dryL[kTick], dryR[kTick];
    double wl[kTick], wr[kTick], ql[kTick], qr[kTick];
    for (int a = 0; a < n; a += kTick)
    {
        const int m = std::min (kTick, n - a);
        float peak = 0.0f;
        for (int i = 0; i < m; ++i)
        {
            dryL[i] = xl[a + i];
            dryR[i] = xr[a + i];
            wl[i] = dryL[i];
            wr[i] = dryR[i];
            peak = std::max (peak, std::max (std::fabs (dryL[i]), std::fabs (dryR[i])));
        }
        quiet = peak > 1e-6f ? 0 : std::min (quiet + m, (int)sr * 10);
        // the settings glide
        for (int b = 0; b < kBands; ++b)
        {
            glide (logFreq[b], targetLogF[b], tickSmooth);
            glide (levelDb[b], targetDb[b], tickSmooth);
            glide (res[b], p[kResIds[b]], tickSmooth);
        }
        glide (move, std::clamp (p[kMovement], 0.0, 1.0), tickSmooth);
        glide (levelMove, p[kLevelMove], tickSmooth);

        drive.process (wl, wr, m);
        const double thetaEnd = theta + dTheta * m;
        const bool runSecond = twoPasses || pass2 > 0.0;
        runPass (0, wl, wr, m, thetaEnd);
        if (runSecond)
        {
            // the second pass takes the first one's result; switching Passes crossfades over 20 ms
            std::copy (wl, wl + m, ql);
            std::copy (wr, wr + m, qr);
            runPass (1, ql, qr, m, thetaEnd);
            const double from = pass2;
            pass2 = std::clamp (pass2 + (twoPasses ? fadeStep : -fadeStep), 0.0, 1.0);
            for (int i = 0; i < m; ++i)
            {
                const double f = from + (pass2 - from) * (double)(i + 1) / m;
                wl[i] += (ql[i] - wl[i]) * f;
                wr[i] += (qr[i] - wr[i]) * f;
            }
            if (pass2 <= 0.0)
            {
                state[1].resetFilters ();
                state[1].glue.reset ();
                state[1].grit.reset ();
            }
        }
        theta = thetaEnd;

        for (int i = 0; i < m; ++i)
        {
            mix += (mixT - mix) * smooth;
            out += (outT - out) * smooth;
            if (std::fabs (mixT - mix) < 1e-6f)
                mix = mixT;
            if (std::fabs (outT - out) < 1e-6f)
                out = outT;
            const float wetL = (float)wl[i], wetR = (float)wr[i];
            yl[a + i] = (dryL[i] + (wetL - dryL[i]) * mix) * out;
            yr[a + i] = (dryR[i] + (wetR - dryR[i]) * mix) * out;
        }
    }

    if (meters)
    {
        constexpr auto rx = std::memory_order_relaxed;
        meters->active.store (quiet < (int)(0.5 * sr), rx);
        meters->passes.store (twoPasses ? 2 : 1, rx);
        for (int k = 0; k < kMaxPasses; ++k)
            for (int b = 0; b < kBands; ++b)
            {
                meters->freq[(size_t)k][(size_t)b].store ((float)state[k].freqNow[b], rx);
                meters->level[(size_t)k][(size_t)b].store ((float)state[k].levelDbNow[b], rx);
            }
        meters->glueDb.store ((float)state[0].glue.gainReductionDb (), rx);
        meters->blocks.fetch_add (1, std::memory_order_release);
    }
    tail.process (yl, yr, n);
}

} // namespace moistr
