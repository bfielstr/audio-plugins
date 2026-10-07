#include "Engine.h"

#include "pluginkit/NoDenormals.h"

#include <algorithm>
#include <cmath>

namespace moistr {

namespace {
inline float dbToGain (double db) { return (float)std::pow (10.0, db / 20.0); }
constexpr double kOffTarget = -100.0; // a band switched off glides down to here, then is silent
constexpr uint32_t kLevelIds[kMaxBands] = {kLowLevel, kMidLevel, kHighLevel, kAirLevel};
constexpr uint32_t kMoveIds[kMaxBands] = {kLowMove, kMidMove, kHighMove, kAirMove}; // ([0] unused: Low is locked)
// the closest two crossovers come (octaves): a band is never narrower than this
constexpr double kMinSpanOct = 1.0 / 3.0;
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

void Engine::PassState::resetFilters ()
{
    split[0].reset ();
    split[1].reset ();
    resetShifter ();
}

void Engine::PassState::resetShifter ()
{
    for (int c = 0; c < 2; ++c)
    {
        hilbert[c].reset ();
        shiftHp[c].reset ();
    }
    shiftPhase = 0.0;
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
    gainSmooth = 1.0 - std::exp (-(double)kTick / (0.001 * sr)); // (1 ms: only takes the edge off a jump)
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

double Engine::cycleSeconds () const
{
    const bool sync = p[kSync] >= 0.5;
    const double beats = kSyncBeats[std::clamp ((int)std::lround (p[kSyncRate]), 0, kNumSyncRates - 1)];
    return sync ? beats * 60.0 / bpm : 1.0 / std::max (p[kRate], 1e-3);
}

void Engine::reset ()
{
    // every smoothed setting at its value
    logX[0] = std::log2 (patterns[0].lowXover);
    logX[1] = std::log2 (std::max (p[kXoverMid], 1.0));
    logX[2] = std::log2 (std::max (p[kXoverHigh], 1.0));
    for (int b = 0; b < kMaxBands; ++b)
    {
        levelDb[b] = p[kLevelIds[b]] <= kLevelOffDb ? kOffTarget : p[kLevelIds[b]];
        share[b] = b == kBandLow ? 0.0 : std::clamp (p[kMoveIds[b]], 0.0, 1.0);
    }
    move = std::clamp (p[kMovement], 0.0, 1.0);
    depth = std::max (0.0, p[kDepth]);
    logRise = std::log2 (std::clamp (p[kRise], 0.01, 100.0));
    logFall = std::log2 (std::clamp (p[kFall], 0.01, 100.0));
    airOwn = bandCount () == 4 ? 1.0 : 0.0;
    shiftHz = shiftHzPrev = std::clamp (p[kShift], -2000.0, 2000.0);
    shiftMix = shiftMixPrev = std::clamp (p[kShiftMix], 0.0, 1.0);
    shiftFade = shiftFadePrev = p[kShiftOn] >= 0.5 ? 1.0 : 0.0;
    secPerCycle = cycleSeconds ();
    theta = 0.0;
    wasPlaying = false;
    drive.reset ();
    for (int k = 0; k < kMaxPasses; ++k)
    {
        PassState& s = state[k];
        s.resetFilters ();
        s.glue.reset ();
        s.grit.reset ();
        targets (k, theta, s.gNow, s.gainNow, true);
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
    if (id >= kBandCount)
        return; // (the split's: read where they are used)
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
            default: break; // (the rest are read where they are used; the 0.18 filters' are not used)
        }
}

void Engine::setTransport (double tempo, double ppq, bool isPlaying)
{
    bpm = tempo > 1.0 ? tempo : 120.0;
    songPpq = ppq;
    playing = isPlaying;
    transportSet = true;
}

void Engine::targets (int pass, double th, double* g, double* gain, bool snap)
{
    const Pattern& pat = patterns[pass];
    PassState& s = state[pass];
    // the corners: Low X locked, the upper two drifting with Movement, each at least kMinSpanOct apart
    const double fMax = 0.45 * sr, span = std::exp2 (kMinSpanOct);
    double f[kMaxXovers];
    f[0] = std::min (std::exp2 (logX[0]), fMax / (span * span));
    f[1] = std::exp2 (logX[1] + (move > 0.0 ? move * kXoverDriftOctaves * pat.drift[1].value (th) : 0.0));
    f[2] = std::exp2 (logX[2] + (move > 0.0 ? move * kXoverDriftOctaves * pat.drift[2].value (th) : 0.0));
    f[1] = std::clamp (f[1], f[0] * span, fMax / span);
    f[2] = std::clamp (f[2], f[1] * span, fMax);
    for (int x = 0; x < kMaxXovers; ++x)
    {
        g[x] = std::tan (dsp::kPi * f[x] / sr);
        s.xoverHz[x] = f[x];
    }
    // the bands: Low at its Level; the others between their floor and their Level
    const double riseScale = std::exp2 (logRise), fallScale = std::exp2 (logFall);
    for (int b = 0; b < kMaxBands; ++b)
    {
        double db = levelDb[b], lift = 1.0;
        const double drop = b == kBandLow ? 0.0 : depth * move * share[b];
        if (drop > 0.0)
        {
            const BandMotion& m = pat.band[b];
            lift = m.lift (th, secPerCycle, m.rise * riseScale, m.fall * fallScale);
            db -= drop * (1.0 - lift);
        }
        if (snap)
            s.dbNow[b] = db;
        else
            glide (s.dbNow[b], db, gainSmooth);
        const bool off = p[kLevelIds[b]] <= kLevelOffDb && levelDb[b] <= kOffTarget + 1.0;
        gain[b] = off ? 0.0 : std::pow (10.0, s.dbNow[b] / 20.0);
        s.lift[b] = lift;
    }
    // with 3 bands Air follows High (High + Air: the whole band above Mid X)
    if (airOwn < 1.0)
    {
        gain[kBandAir] = airOwn * gain[kBandAir] + (1.0 - airOwn) * gain[kBandHigh];
        if (airOwn <= 0.0)
            s.lift[kBandAir] = s.lift[kBandHigh];
    }
    for (int b = 0; b < kMaxBands; ++b)
        s.gainDb[b] = gain[b] > 0.0 ? 20.0 * std::log10 (gain[b]) : kOffTarget;
}

void Engine::runPass (int pass, double* l, double* r, int m, double thetaEnd)
{
    PassState& s = state[pass];
    double g1[kMaxXovers], gain1[kMaxBands];
    targets (pass, thetaEnd, g1, gain1, false);
    const bool still = g1[0] == s.gNow[0] && g1[1] == s.gNow[1] && g1[2] == s.gNow[2];
    dsp::SvfCoefs c[kMaxXovers];
    if (still)
        for (int x = 0; x < kMaxXovers; ++x)
            c[x].set (g1[x], dsp::kSqrt2);
    if (shiftFade > 0.0 || shiftFadePrev > 0.0)
    {
        // the shifter on the bands above Low (the Low band, and so everything below Low X, untouched)
        const double w0 = 2.0 * dsp::kPi * shiftHzPrev / sr, w1 = 2.0 * dsp::kPi * shiftHz / sr;
        for (int i = 0; i < m; ++i)
        {
            const double t = (double)(i + 1) / m;
            if (!still)
                for (int x = 0; x < kMaxXovers; ++x)
                    c[x].set (s.gNow[x] + (g1[x] - s.gNow[x]) * t, dsp::kSqrt2);
            double gain[kMaxBands];
            for (int b = 0; b < kMaxBands; ++b)
                gain[b] = s.gainNow[b] + (gain1[b] - s.gainNow[b]) * t;
            const double fade = shiftFadePrev + (shiftFade - shiftFadePrev) * t;
            const double wet = shiftMixPrev + (shiftMix - shiftMixPrev) * t;
            const double cs = std::cos (s.shiftPhase), sn = std::sin (s.shiftPhase);
            s.shiftPhase += w0 + (w1 - w0) * t;
            if (s.shiftPhase > dsp::kPi)
                s.shiftPhase -= 2.0 * dsp::kPi;
            else if (s.shiftPhase < -dsp::kPi)
                s.shiftPhase += 2.0 * dsp::kPi;
            double* io[2] = {l + i, r + i};
            for (int ch = 0; ch < 2; ++ch)
            {
                double band[kMaxBands];
                s.split[ch].tick (*io[ch], c, band);
                const double up = gain[1] * band[1] + gain[2] * band[2] + gain[3] * band[3];
                double hi, hq;
                s.hilbert[ch].tick (up, hi, hq);
                // the single sideband, kept out of the sub region (and away from DC)
                const double shifted = s.shiftHp[ch].tick (hi * cs + hq * sn, c[0]).hp;
                const double moved = hi + (shifted - hi) * wet;
                *io[ch] = gain[0] * band[0] + up + (moved - up) * fade;
            }
        }
    }
    else
        for (int i = 0; i < m; ++i)
        {
            const double t = (double)(i + 1) / m;
            if (!still)
                for (int x = 0; x < kMaxXovers; ++x)
                    c[x].set (s.gNow[x] + (g1[x] - s.gNow[x]) * t, dsp::kSqrt2);
            double gain[kMaxBands];
            for (int b = 0; b < kMaxBands; ++b)
                gain[b] = s.gainNow[b] + (gain1[b] - s.gainNow[b]) * t;
            double band[kMaxBands];
            s.split[0].tick (l[i], c, band);
            l[i] = gain[0] * band[0] + gain[1] * band[1] + gain[2] * band[2] + gain[3] * band[3];
            s.split[1].tick (r[i], c, band);
            r[i] = gain[0] * band[0] + gain[1] * band[1] + gain[2] * band[2] + gain[3] * band[3];
        }
    for (int x = 0; x < kMaxXovers; ++x)
        s.gNow[x] = g1[x];
    for (int b = 0; b < kMaxBands; ++b)
        s.gainNow[b] = gain1[b];
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
    secPerCycle = cycleSeconds ();

    const float mixT = (float)std::clamp (p[kMix], 0.0, 1.0), outT = dbToGain (p[kOutput]);
    const bool twoPasses = std::lround (p[kPasses]) == kPasses2;
    const bool fourBands = bandCount () == 4;
    const bool shiftOn = p[kShiftOn] >= 0.5;
    const double fadeStep = (double)kTick / (0.02 * sr);
    double targetLogX[kMaxXovers], targetDb[kMaxBands], targetShare[kMaxBands];
    targetLogX[0] = std::log2 (patterns[0].lowXover);
    targetLogX[1] = std::log2 (std::max (p[kXoverMid], 1.0));
    targetLogX[2] = std::log2 (std::max (p[kXoverHigh], 1.0));
    for (int b = 0; b < kMaxBands; ++b)
    {
        targetDb[b] = p[kLevelIds[b]] <= kLevelOffDb ? kOffTarget : p[kLevelIds[b]];
        targetShare[b] = b == kBandLow ? 0.0 : std::clamp (p[kMoveIds[b]], 0.0, 1.0);
    }
    const double targetRise = std::log2 (std::clamp (p[kRise], 0.01, 100.0));
    const double targetFall = std::log2 (std::clamp (p[kFall], 0.01, 100.0));

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
        for (int x = 0; x < kMaxXovers; ++x)
            glide (logX[x], targetLogX[x], tickSmooth);
        for (int b = 0; b < kMaxBands; ++b)
        {
            glide (levelDb[b], targetDb[b], tickSmooth);
            glide (share[b], targetShare[b], tickSmooth);
        }
        glide (move, std::clamp (p[kMovement], 0.0, 1.0), tickSmooth);
        glide (depth, std::max (0.0, p[kDepth]), tickSmooth);
        glide (logRise, targetRise, tickSmooth);
        glide (logFall, targetFall, tickSmooth);
        // switching Bands fades Air between following High and its own gain over 20 ms
        airOwn = std::clamp (airOwn + (fourBands ? fadeStep : -fadeStep), 0.0, 1.0);
        // the shifter: Shift and Shift Mix glide, switching it fades over 20 ms
        shiftHzPrev = shiftHz;
        shiftMixPrev = shiftMix;
        shiftFadePrev = shiftFade;
        glide (shiftHz, std::clamp (p[kShift], -2000.0, 2000.0), tickSmooth);
        glide (shiftMix, std::clamp (p[kShiftMix], 0.0, 1.0), tickSmooth);
        shiftFade = std::clamp (shiftFade + (shiftOn ? fadeStep : -fadeStep), 0.0, 1.0);

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
        if (shiftFade <= 0.0 && shiftFadePrev > 0.0)
            for (auto& s : state)
                s.resetShifter (); // (faded out: off, and ready to start clean)

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
        meters->bands.store (fourBands ? 4 : 3, rx);
        meters->lowXover.store ((float)state[0].xoverHz[0], rx);
        for (int k = 0; k < kMaxPasses; ++k)
        {
            const PassState& s = state[k];
            for (int x = 0; x < kMaxXovers; ++x)
                meters->xover[(size_t)k][(size_t)x].store ((float)s.xoverHz[x], rx);
            for (int b = 0; b < kMaxBands; ++b)
            {
                meters->gainDb[(size_t)k][(size_t)b].store ((float)s.gainDb[b], rx);
                meters->lift[(size_t)k][(size_t)b].store ((float)s.lift[b], rx);
            }
            // (legacy)
            const float legacyFreq[kBands] = {(float)s.xoverHz[0], (float)std::sqrt (s.xoverHz[0] * s.xoverHz[1]),
                                              (float)s.xoverHz[1]};
            for (int b = 0; b < kBands; ++b)
            {
                meters->freq[(size_t)k][(size_t)b].store (legacyFreq[b], rx);
                meters->level[(size_t)k][(size_t)b].store ((float)s.gainDb[b], rx);
            }
        }
        meters->shiftHz.store (shiftFade > 0.0 ? (float)shiftHz : 0.0f, rx);
        meters->shiftAmount.store ((float)shiftFade, rx);
        meters->glueDb.store ((float)state[0].glue.gainReductionDb (), rx);
        meters->blocks.fetch_add (1, std::memory_order_release);
    }
    tail.process (yl, yr, n);
}

} // namespace moistr
