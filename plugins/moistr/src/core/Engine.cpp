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
// Drop Out: from this fall (dB) the floor's gain curves down to silence at kDropToDb
constexpr double kDropFromDb = 30.0, kDropToDb = 48.0;
constexpr double kSilentDb = -120.0; // (Drop Out: at or under this a band is silent)
constexpr double kPatternFadeSec = 0.1; // changing Seed, Seed B or Density crossfades over this
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
    resetLiquid ();
}

void Engine::PassState::resetLiquid ()
{
    for (int c = 0; c < 2; ++c)
    {
        liq1[c].reset ();
        liq2[c].reset ();
    }
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
    applyPattern (false);
}

void Engine::prepare (double sampleRate, int maxBlock)
{
    sr = sampleRate;
    sweep.prepare (sr);
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

void Engine::applyPattern (bool fade)
{
    const int seed = std::clamp ((int)std::lround (p[kSeed]), kMinSeed, kMaxSeed);
    const int seedB = std::clamp ((int)std::lround (p[kSeedB]), kMinSeed, kMaxSeed);
    const double density = std::clamp (p[kDensity], 0.25, 8.0);
    if (seed == cur.seed && seedB == cur.seedB && density == cur.density)
        return;
    if (fade && running)
    {
        // the patterns now fade out. Already crossfading: the side with the larger weight now fades out from
        // that weight (the other is dropped: at most half a step, then the gains' smoothing)
        if (xfade >= 1.0)
        {
            old = cur;
            xfade = 0.0;
        }
        else if (xfade >= 0.5)
        {
            old = cur;
            xfade = 1.0 - xfade;
        }
    }
    else
        xfade = 1.0;
    if (seed != cur.seed || seedB != cur.seedB)
        for (int k = 0; k < kMaxPasses; ++k)
        {
            cur.a[k] = makePattern (seed, k);
            cur.b[k] = makePattern (seedB, k, seed); // (Seed's Low crossover: the low end stays put)
        }
    cur.seed = seed;
    cur.seedB = seedB;
    cur.density = density;
}

double Engine::setLift (const PatternSet& ps, int pass, int band, double th, bool dips) const
{
    auto one = [&] (const Pattern& pat) {
        const BandMotion& m = pat.band[band];
        return m.lift (th, secPerCycle, std::max (m.rise * riseScale, kMinRampSec), std::max (m.fall * fallScale, kMinRampSec),
                       ps.density, dips ? pat.lowDipMask : m.mask);
    };
    if (blend <= 0.0)
        return one (ps.a[pass]);
    if (blend >= 1.0)
        return one (ps.b[pass]);
    return blendLifts (one (ps.a[pass]), one (ps.b[pass]), blend);
}

double Engine::liftAt (int pass, int band, double th, bool dips) const
{
    const double now = setLift (cur, pass, band, th, dips);
    if (xfade >= 1.0)
        return now;
    const double was = setLift (old, pass, band, th, dips), w = 0.5 - 0.5 * std::cos (dsp::kPi * xfade);
    return was + (now - was) * w;
}

double Engine::driftAt (int pass, int x, double th) const
{
    auto one = [&] (const PatternSet& ps) {
        const double a = ps.a[pass].drift[x].value (th);
        if (blend <= 0.0)
            return a;
        const double c = ps.b[pass].drift[x].value (th);
        return blend >= 1.0 ? c : a + (c - a) * blend;
    };
    const double now = one (cur);
    if (xfade >= 1.0)
        return now;
    const double was = one (old), w = 0.5 - 0.5 * std::cos (dsp::kPi * xfade);
    return was + (now - was) * w;
}

void Engine::liquidAt (double th, double& pos, double& logRatio) const
{
    // (one path for both passes: pass 0's)
    auto one = [&] (const PatternSet& ps, double& q, double& lr) {
        ps.a[0].liquid.at (th, ps.density, q, lr);
        if (blend <= 0.0)
            return;
        double qb, lrb;
        ps.b[0].liquid.at (th, ps.density, qb, lrb);
        if (blend >= 1.0)
        {
            q = qb;
            lr = lrb;
            return;
        }
        q += (qb - q) * blend;
        lr += (lrb - lr) * blend;
    };
    one (cur, pos, logRatio);
    if (xfade >= 1.0)
        return;
    double q, lr;
    one (old, q, lr);
    const double w = 0.5 - 0.5 * std::cos (dsp::kPi * xfade);
    pos = q + (pos - q) * w;
    logRatio = lr + (logRatio - lr) * w;
}

void Engine::liquidTargets (double th, bool snap)
{
    liqPrev = liqNow;
    if (liquid <= 0.0)
        liqNow.a1 = liqNow.a2 = 0.0; // (off: not run; the frequencies stay where they were)
    else
    {
        double pos, logRatio;
        liquidAt (th, pos, logRatio);
        if (link > 0.0) // (with Link F1 rises as the bands open)
            pos += link * kLinkFollow * (state[0].sharedLift - pos);
        double lo = std::exp2 (logLiqLo), hi = std::exp2 (logLiqHi);
        if (lo > hi)
            std::swap (lo, hi);
        const double fMax = std::min (0.45 * sr, 12000.0);
        liqF1 = std::min (lo * std::pow (hi / lo, std::clamp (pos, 0.0, 1.0)), fMax);
        liqF2 = std::min (liqF1 * std::exp2 (logRatio), fMax);
        liqNow.g1 = std::tan (dsp::kPi * liqF1 / sr);
        liqNow.g2 = std::tan (dsp::kPi * liqF2 / sr);
        liqNow.k = 1.0 / (kLiquidQMin * std::pow (kLiquidQMax / kLiquidQMin, liqRes));
        liqNow.a1 = std::pow (10.0, liquid * kLiquidMaxDb / 20.0) - 1.0;
        liqNow.a2 = std::pow (10.0, liquid * kLiquidF2Db / 20.0) - 1.0;
    }
    if (snap || liqPrev.a1 <= 0.0)
    {
        // (from off: the filters start where they are now, fading in by their gains alone)
        const double a1 = liqPrev.a1, a2 = liqPrev.a2;
        liqPrev = liqNow;
        if (!snap)
        {
            liqPrev.a1 = a1;
            liqPrev.a2 = a2;
        }
    }
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
    logX[0] = std::log2 (cur.a[0].lowXover);
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
    blend = std::clamp (p[kSeedBlend], 0.0, 1.0);
    logSpeed = std::log2 (std::clamp (p[kSpeed], 1.0, 16.0));
    lowPush = std::clamp (p[kLowPush], 0.0, 12.0);
    lowDip = std::clamp (p[kLowDip], 0.0, 6.0);
    dropOut = p[kDropOut] >= 0.5 ? 1.0 : 0.0;
    xfade = 1.0;
    running = false;
    airOwn = bandCount () == 4 ? 1.0 : 0.0;
    shiftHz = shiftHzPrev = std::clamp (p[kShift], -2000.0, 2000.0);
    shiftMix = shiftMixPrev = std::clamp (p[kShiftMix], 0.0, 1.0);
    shiftFade = shiftFadePrev = p[kShiftOn] >= 0.5 ? 1.0 : 0.0;
    link = std::clamp (p[kLink], 0.0, 1.0);
    liquid = std::clamp (p[kLiquid], 0.0, 1.0);
    liqRes = std::clamp (p[kLiquidRes], 0.0, 1.0);
    logLiqLo = std::log2 (std::clamp (p[kLiquidLow], 20.0, 20000.0));
    logLiqHi = std::log2 (std::clamp (p[kLiquidHigh], 20.0, 20000.0));
    secPerCycle = cycleSeconds ();
    theta = 0.0;
    wasPlaying = false;
    sweep.reset (p.data ());
    drive.reset ();
    for (int k = 0; k < kMaxPasses; ++k)
    {
        PassState& s = state[k];
        s.resetFilters ();
        s.glue.reset ();
        s.grit.reset ();
        targets (k, theta, s.gNow, s.gainNow, true);
    }
    liqNow = {};
    liquidTargets (theta, true);
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
    if ((id == kDensity || id == kSpeed) && std::fabs (plain - 1.0) < 1e-6)
        plain = 1.0; // (x1 exactly: the default from a normalized value, so the movement is 0.21's bit for bit)
    p[id] = plain;
    if (id == kSeedB || id == kDensity)
        applyPattern (true);
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
            case kSeed: applyPattern (true); break;
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
    PassState& s = state[pass];
    // the corners: Low X locked, the upper two drifting with Movement, each at least kMinSpanOct apart
    const double fMax = 0.45 * sr, span = std::exp2 (kMinSpanOct);
    double f[kMaxXovers];
    f[0] = std::min (std::exp2 (logX[0]), fMax / (span * span));
    f[1] = std::exp2 (logX[1] + (move > 0.0 ? move * kXoverDriftOctaves * driftAt (pass, 1, th) : 0.0));
    f[2] = std::exp2 (logX[2] + (move > 0.0 ? move * kXoverDriftOctaves * driftAt (pass, 2, th) : 0.0));
    f[1] = std::clamp (f[1], f[0] * span, fMax / span);
    f[2] = std::clamp (f[2], f[1] * span, fMax);
    for (int x = 0; x < kMaxXovers; ++x)
    {
        g[x] = std::tan (dsp::kPi * f[x] / sr);
        s.xoverHz[x] = f[x];
    }
    // the bands: Low at its Level (pushed up and dipped by its own events with Low Push / Low Dip); the
    // others between their floor and their Level
    const double speed = std::exp2 (logSpeed);
    riseScale = std::exp2 (logRise) / speed;
    fallScale = std::exp2 (logFall) / speed;
    // Link: the moving bands' lifts pulled towards the lead's (the Mid band's)
    s.sharedLift = link > 0.0 ? liftAt (pass, kBandMid, th, false) : 0.0;
    for (int b = 0; b < kMaxBands; ++b)
    {
        double db = levelDb[b], lift = 1.0;
        if (b == kBandLow)
        {
            const double up = move * lowPush, down = move * lowDip;
            if (up > 0.0)
                db += up * liftAt (pass, b, th, false);
            if (down > 0.0)
                db -= down * liftAt (pass, b, th, true);
        }
        else
        {
            const double drop = depth * move * share[b];
            if (drop > 0.0)
            {
                if (link <= 0.0)
                    lift = liftAt (pass, b, th, false);
                else
                {
                    const double own = b == kBandMid ? s.sharedLift : liftAt (pass, b, th, false);
                    lift = own + link * (s.sharedLift - own);
                }
                db -= drop * (1.0 - lift);
                if (dropOut > 0.0 && drop > kDropFromDb)
                {
                    // Drop Out: the floor's gain x (1 - cut), the cut rising smoothly to 1 at 48 dB
                    const double t = std::min ((drop - kDropFromDb) / (kDropToDb - kDropFromDb), 1.0);
                    const double keep = 1.0 - dropOut * t * t * (3.0 - 2.0 * t) * (1.0 - lift);
                    db += keep > 1e-6 ? 20.0 * std::log10 (keep) : kSilentDb;
                }
            }
        }
        if (snap)
            s.dbNow[b] = db;
        else
            glide (s.dbNow[b], db, gainSmooth);
        const bool off = (p[kLevelIds[b]] <= kLevelOffDb && levelDb[b] <= kOffTarget + 1.0) || (dropOut > 0.0 && s.dbNow[b] <= kSilentDb);
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
    if (pass == 0)
        liquidTargets (thetaEnd, false); // (one resonance for both passes)
    const bool still = g1[0] == s.gNow[0] && g1[1] == s.gNow[1] && g1[2] == s.gNow[2];
    dsp::SvfCoefs c[kMaxXovers];
    if (still)
        for (int x = 0; x < kMaxXovers; ++x)
            c[x].set (g1[x], dsp::kSqrt2);
    const bool shifting = shiftFade > 0.0 || shiftFadePrev > 0.0;
    const bool liquidOn = liqNow.a1 > 0.0 || liqPrev.a1 > 0.0;
    if (shifting || liquidOn)
    {
        // Liquid and the shifter on the bands above Low (the Low band, and so everything below Low X, untouched)
        const LiquidCoefs& la = liqPrev;
        const LiquidCoefs& lb = liqNow;
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
            double fade = 0.0, wet = 0.0, cs = 1.0, sn = 0.0;
            if (shifting)
            {
                fade = shiftFadePrev + (shiftFade - shiftFadePrev) * t;
                wet = shiftMixPrev + (shiftMix - shiftMixPrev) * t;
                cs = std::cos (s.shiftPhase);
                sn = std::sin (s.shiftPhase);
                s.shiftPhase += w0 + (w1 - w0) * t;
                if (s.shiftPhase > dsp::kPi)
                    s.shiftPhase -= 2.0 * dsp::kPi;
                else if (s.shiftPhase < -dsp::kPi)
                    s.shiftPhase += 2.0 * dsp::kPi;
            }
            dsp::SvfCoefs lc1, lc2;
            double a1 = 0.0, a2 = 0.0;
            if (liquidOn)
            {
                const double k = la.k + (lb.k - la.k) * t;
                lc1.set (la.g1 + (lb.g1 - la.g1) * t, k);
                lc2.set (la.g2 + (lb.g2 - la.g2) * t, k);
                // (k x the band-pass: unity at the peak, so the peak is 1 + a)
                a1 = (la.a1 + (lb.a1 - la.a1) * t) * k;
                a2 = (la.a2 + (lb.a2 - la.a2) * t) * k;
            }
            double* io[2] = {l + i, r + i};
            for (int ch = 0; ch < 2; ++ch)
            {
                double band[kMaxBands];
                s.split[ch].tick (*io[ch], c, band);
                double up = gain[1] * band[1] + gain[2] * band[2] + gain[3] * band[3];
                if (liquidOn)
                {
                    up += a1 * s.liq1[ch].tick (up, lc1).bp;
                    up += a2 * s.liq2[ch].tick (up, lc2).bp;
                }
                if (!shifting)
                {
                    *io[ch] = gain[0] * band[0] + up;
                    continue;
                }
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
    running = true;
    const bool sync = p[kSync] >= 0.5;
    const double beats = kSyncBeats[std::clamp ((int)std::lround (p[kSyncRate]), 0, kNumSyncRates - 1)];
    const double rate = p[kRate];
    const bool relocate = playing && (!wasPlaying || std::fabs (songPpq - expectPpq) > 1e-3);
    sweep.beginBlock (p.data (), playing, relocate, songPpq, bpm);
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
    targetLogX[0] = std::log2 (cur.a[0].lowXover);
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
        glide (blend, std::clamp (p[kSeedBlend], 0.0, 1.0), tickSmooth);
        glide (logSpeed, std::log2 (std::clamp (p[kSpeed], 1.0, 16.0)), tickSmooth);
        glide (lowPush, std::clamp (p[kLowPush], 0.0, 12.0), tickSmooth);
        glide (lowDip, std::clamp (p[kLowDip], 0.0, 6.0), tickSmooth);
        dropOut = std::clamp (dropOut + (p[kDropOut] >= 0.5 ? fadeStep : -fadeStep), 0.0, 1.0);
        if (xfade < 1.0)
            xfade = std::min (1.0, xfade + (double)m / (kPatternFadeSec * sr));
        // switching Bands fades Air between following High and its own gain over 20 ms
        airOwn = std::clamp (airOwn + (fourBands ? fadeStep : -fadeStep), 0.0, 1.0);
        // the shifter: Shift and Shift Mix glide, switching it fades over 20 ms
        shiftHzPrev = shiftHz;
        shiftMixPrev = shiftMix;
        shiftFadePrev = shiftFade;
        glide (shiftHz, std::clamp (p[kShift], -2000.0, 2000.0), tickSmooth);
        glide (shiftMix, std::clamp (p[kShiftMix], 0.0, 1.0), tickSmooth);
        shiftFade = std::clamp (shiftFade + (shiftOn ? fadeStep : -fadeStep), 0.0, 1.0);
        glide (link, std::clamp (p[kLink], 0.0, 1.0), tickSmooth);
        glide (liquid, std::clamp (p[kLiquid], 0.0, 1.0), tickSmooth);
        glide (liqRes, std::clamp (p[kLiquidRes], 0.0, 1.0), tickSmooth);
        glide (logLiqLo, std::log2 (std::clamp (p[kLiquidLow], 20.0, 20000.0)), tickSmooth);
        glide (logLiqHi, std::log2 (std::clamp (p[kLiquidHigh], 20.0, 20000.0)), tickSmooth);

        sweep.tick (p.data (), wl, wr, m); // (off: not run)
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
        if (liqNow.a1 <= 0.0 && liqPrev.a1 > 0.0)
            for (auto& s : state)
                s.resetLiquid ();

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
        const bool liq = liqNow.a1 > 0.0;
        meters->liquidHz.store (liq ? (float)liqF1 : 0.0f, rx);
        meters->liquidF2Hz.store (liq ? (float)liqF2 : 0.0f, rx);
        meters->liquidAmount.store ((float)liquid, rx);
        meters->glueDb.store ((float)state[0].glue.gainReductionDb (), rx);
        for (int b = 0; b <= kNumBells; ++b)
            meters->sweepHz[(size_t)b].store ((float)(b < kNumBells ? sweep.bellHz (b) : sweep.shelfHz ()), rx);
        for (int b = 0; b < kNumBells; ++b)
            meters->bellDb[(size_t)b].store ((float)sweep.bellDb (b), rx);
        meters->shelfDb.store ((float)sweep.shelfDb (), rx);
        meters->shelfCeiling.store ((float)sweep.shelfCeiling (), rx);
        meters->sweepAmount.store ((float)sweep.amount (), rx);
        meters->shelfAmount.store ((float)sweep.shelfAmount (), rx);
        meters->blocks.fetch_add (1, std::memory_order_release);
    }
    tail.process (yl, yr, n);
}

} // namespace moistr
