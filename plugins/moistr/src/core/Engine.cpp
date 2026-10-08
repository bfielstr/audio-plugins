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
    flatGesture.flat (1.0);
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
        if (gestActive && targeted (kTargetLiquid)) // (a gesture moving it)
            pos = pulled (kTargetLiquid, std::clamp (pos, 0.0, 1.0));
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
    blend = blendBase = std::clamp (p[kSeedBlend], 0.0, 1.0);
    logSpeed = std::log2 (std::clamp (p[kSpeed], 1.0, 16.0));
    lowPush = std::clamp (p[kLowPush], 0.0, 12.0);
    lowDip = std::clamp (p[kLowDip], 0.0, 6.0);
    dropOut = p[kDropOut] >= 0.5 ? 1.0 : 0.0;
    xfade = 1.0;
    running = false;
    airOwn = bandCount () == 4 ? 1.0 : 0.0;
    shiftHz = shiftHzPrev = shiftBase = std::clamp (p[kShift], -2000.0, 2000.0);
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
    // the gestures at the start, every one at its value
    gBeats = gBeatsNow = 0.0;
    logWobRate = std::log2 (std::clamp (p[kWobbleRate], kWobbleRateMin, kWobbleRateMax));
    wobBase = std::clamp (p[kWobbleAmount], 0.0, 1.0);
    wobPhase = wobPhasePrev = 0.0;
    closeNow = closePrev = {};
    pullDirt = pullDirtPrev = pullBells = pullBellsPrev = 0.0;
    for (int c = 0; c < 2; ++c)
    {
        closeLp[c].reset ();
        auxSplit[c].reset ();
    }
    gestureTick (0, 0.0, true);
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
    if (tempo > 1.0)
        gBpm = tempo; // (the gestures run on at the last tempo the host gave)
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
    f[1] = std::exp2 (logX[1] + gestX[1] + (move > 0.0 ? move * kXoverDriftOctaves * driftAt (pass, 1, th) : 0.0));
    f[2] = std::exp2 (logX[2] + gestX[2] + (move > 0.0 ? move * kXoverDriftOctaves * driftAt (pass, 2, th) : 0.0));
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
            // (the Low band's events with Seed Blend's own value: a gesture on it moves the other bands only)
            const double keep = blend;
            blend = blendBase;
            if (up > 0.0)
                db += up * liftAt (pass, b, th, false);
            if (down > 0.0)
                db -= down * liftAt (pass, b, th, true);
            blend = keep;
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
        if (pass == 0 && gLevel[b] != 1.0)
            gain[b] *= gLevel[b]; // (a gesture on the band's level)
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

const Gesture& Engine::gestureOf (int g) const
{
    const int choice = std::clamp ((int)std::lround (p[gestureId (g, kGestureChoice)]), 0, kUserGesture);
    if (choice == kUserGesture)
        return user[g] ? *user[g] : flatGesture;
    return factoryGesture (choice);
}

double Engine::pulled (int target, double base) const
{
    // (the 0.27 slots in order, then the one gesture's lanes)
    for (const SlotRun& r : slot)
        if (r.target == target && r.k > 0.0)
            base += (r.shaped - base) * r.k;
    if (target == kTargetMidX || target == kTargetHighX)
        return base; // (the lanes: pulledX)
    for (const SlotRun& r : lane)
        if (r.target == target && r.k > 0.0)
            base += (r.shaped - base) * r.k;
    return base;
}

bool Engine::slotTargeted (int target) const
{
    for (const SlotRun& r : slot)
        if (r.target == target && r.k > 0.0)
            return true;
    return false;
}

double Engine::pulledX (int target, double x) const
{
    for (const SlotRun& r : lane)
        if (r.target == target && r.k > 0.0)
            x += (r.shaped - x) * r.k;
    return x;
}

bool Engine::targeted (int target) const
{
    for (const SlotRun& r : slot)
        if (r.target == target && r.k > 0.0)
            return true;
    for (const SlotRun& r : lane)
        if (r.target == target && r.k > 0.0)
            return true;
    return false;
}

void Engine::gestureTick (int m, double beats, bool snap)
{
    // the slots: each at its place in its gesture, its value smoothed; a change of Target fades it out first
    gBeatsNow = beats;
    const double intensity = std::clamp (p[kIntensity], 0.0, 1.0), fadeStep = (double)std::max (m, 1) / (0.02 * sr);
    bool pulling = false;
    for (int g = 0; g < kNumGestureSlots; ++g)
    {
        SlotRun& r = slot[g];
        const int want = std::clamp ((int)std::lround (p[gestureId (g, kGestureTarget)]), 0, kNumTargets - 1);
        const double depth = std::clamp (p[gestureId (g, kGestureDepth)], -1.0, 1.0);
        const double k = want == kTargetOff ? 0.0 : std::fabs (depth) * intensity;
        if (snap)
        {
            r.target = want;
            r.k = k;
            r.primed = false;
        }
        else if (want != r.target)
        {
            r.k = std::max (0.0, r.k - fadeStep);
            if (r.k <= 0.0 || r.target == kTargetOff)
            {
                r.k = 0.0;
                r.target = want;
                r.primed = false;
            }
        }
        else
            glide (r.k, k, tickSmooth);
        if (r.target == kTargetOff)
        {
            r.k = 0.0;
            continue;
        }
        const Gesture& ge = gestureOf (g);
        const int lengthChoice = std::clamp ((int)std::lround (p[gestureId (g, kGestureLength)]), 0, kNumGestureLengths - 1);
        const double length = lengthChoice == 0 ? ge.length : kGestureLengthBeats[lengthChoice];
        const int speedChoice = std::clamp ((int)std::lround (p[gestureId (g, kGestureSpeed)]), 0, kNumGestureSpeeds - 1);
        const int mode = std::lround (p[gestureId (g, kGestureMode)]) == kModeWalk ? kModeWalk : kModeLoop;
        r.pos = gesturePosition (mode, beats, length, kGestureSpeeds[speedChoice], std::clamp (p[gestureId (g, kGesturePosition)], 0.0, 1.0));
        const double raw = ge.at (r.pos * ge.length);
        if (!r.primed)
        {
            r.s = raw;
            r.primed = true;
        }
        else
        {
            // Smooth: a one-pole from 2 ms (0) to a 1/16 beat (1)
            const double smooth = std::clamp (p[gestureId (g, kGestureSmooth)], 0.0, 1.0);
            const double longest = std::max (kSmoothMaxBeats * 60.0 / gBpm, kSmoothMinSec);
            const double tau = kSmoothMinSec + (longest - kSmoothMinSec) * smooth;
            r.s += (raw - r.s) * (1.0 - std::exp (-(double)m / (tau * sr)));
        }
        r.shaped = depth >= 0.0 ? r.s : 1.0 - r.s;
        pulling = pulling || r.k > 0.0;
    }
    // the one gesture (0.28): every lane at the same place on one clock, pulling its target by Amount; a change of
    // gesture fades the old one out first (20 ms), then the new one in
    {
        const int choice = std::clamp ((int)std::lround (p[kScene]), 0, kSceneUser);
        const Scene* want = choice == kSceneNone ? nullptr : choice == kSceneUser ? userScene : &factoryScene (choice - 1);
        const double amount = want ? std::clamp (p[kSceneAmount], 0.0, 1.0) : 0.0;
        bool fresh = false;
        if (snap)
        {
            scene = want;
            sceneK = amount;
            fresh = true;
        }
        else if (want != scene)
        {
            sceneK = std::max (0.0, sceneK - fadeStep);
            if (sceneK <= 0.0 || !scene)
            {
                sceneK = 0.0;
                scene = want;
                fresh = true;
            }
        }
        else
            glide (sceneK, amount, tickSmooth);
        const int lanes = scene ? std::clamp (scene->count, 0, kMaxSceneLanes) : 0;
        if (lanes > 0)
        {
            const int lengthChoice = std::clamp ((int)std::lround (p[kSceneLength]), 0, kNumGestureLengths - 1);
            const double length = lengthChoice == 0 ? scene->length : kGestureLengthBeats[lengthChoice];
            const int speedChoice = std::clamp ((int)std::lround (p[kSceneSpeed]), 0, kNumGestureSpeeds - 1);
            const int mode = std::lround (p[kSceneMode]) == kModeWalk ? kModeWalk : kModeLoop;
            scenePosNow = gesturePosition (mode, beats, length, kGestureSpeeds[speedChoice], std::clamp (p[kScenePosition], 0.0, 1.0));
            const double at = scenePosNow * scene->length;
            const double smooth = std::clamp (p[kSceneSmooth], 0.0, 1.0);
            const double longest = std::max (kSmoothMaxBeats * 60.0 / gBpm, kSmoothMinSec);
            const double tau = kSmoothMinSec + (longest - kSmoothMinSec) * smooth;
            const double follow = 1.0 - std::exp (-(double)m / (tau * sr));
            const double open = std::min (p[kToneOn] >= 0.5 ? std::clamp (p[kTone], kToneMin, kToneMax) : kCloseOpenHz, 0.45 * sr);
            for (int i = 0; i < lanes; ++i)
            {
                const SceneLane& sl = scene->lane[i];
                SlotRun& r = lane[i];
                r.target = std::clamp (sl.target, 0, kNumTargets - 1);
                r.k = r.target == kTargetOff ? 0.0 : sceneK;
                const double raw = sl.curve.at (at);
                if (fresh || !r.primed)
                {
                    r.s = raw;
                    r.primed = true;
                }
                else
                    r.s += (raw - r.s) * follow;
                double lo = sl.lo, hi = sl.hi;
                if (sl.closeHz)
                {
                    lo = targetNorm (kTargetClose, sl.hzLo, open);
                    hi = targetNorm (kTargetClose, sl.hzHi, open);
                }
                r.shaped = lo + (hi - lo) * r.s;
                r.pos = scenePosNow;
                pulling = pulling || r.k > 0.0;
            }
        }
        for (int i = lanes; i < kMaxSceneLanes; ++i)
        {
            lane[i].target = kTargetOff;
            lane[i].k = 0.0;
            lane[i].primed = false;
        }
    }
    // Wobble's own settings glide; it runs while it or a gesture on it is up
    if (snap)
    {
        logWobRate = std::log2 (std::clamp (p[kWobbleRate], kWobbleRateMin, kWobbleRateMax));
        wobBase = std::clamp (p[kWobbleAmount], 0.0, 1.0);
    }
    else
    {
        glide (logWobRate, std::log2 (std::clamp (p[kWobbleRate], kWobbleRateMin, kWobbleRateMax)), tickSmooth);
        glide (wobBase, std::clamp (p[kWobbleAmount], 0.0, 1.0), tickSmooth);
    }
    const bool wobbling = wobBase > 0.0 || wobAmt > 0.0;
    gestActive = pulling || wobbling || closeNow.mix > 0.0 || pullDirt > 0.0 || pullBells > 0.0;
    // what they do this tick (all at rest while none runs: the engine is then 0.26's, bit for bit)
    for (int b = 0; b < kMaxBands; ++b)
        gLevel[b] = 1.0;
    gestX[1] = gestX[2] = 0.0;
    if (!gestActive)
    {
        wobPhasePrev = wobPhase;
        wobAmtPrev = wobAmt = 0.0;
        wobGain = 1.0;
        return;
    }
    static constexpr int kLevelTargets[kMaxBands] = {kTargetOff, kTargetMidLevel, kTargetHighLevel, kTargetAirLevel};
    for (int b = kBandMid; b < kMaxBands; ++b)
        if (targeted (kLevelTargets[b]))
        {
            // 1: at its Level; down a dB scale to -kGestureLevelDb, fading to silence over the last part
            const double v = std::clamp (pulled (kLevelTargets[b], 1.0), 0.0, 1.0);
            gLevel[b] = v >= 1.0 ? 1.0 : std::pow (10.0, -kGestureLevelDb * (1.0 - v) / 20.0) * std::min (1.0, v * kLevelFadeShare);
        }
    // the controls a gesture moves over their range
    auto overRange = [this] (int target, uint32_t id, double plain) {
        return paramTable ().toPlain (id, pulled (target, paramTable ().toNormalized (id, plain)));
    };
    // (Mid X and High X: the slots over the controls' range as in 0.27, then the lanes in log2 Hz, so a lane may
    // take Mid X below the control's range, down to a third of an octave above the locked Low X: targets)
    if (targeted (kTargetMidX))
    {
        const double x = slotTargeted (kTargetMidX) ? std::log2 (overRange (kTargetMidX, kXoverMid, std::exp2 (logX[1]))) : logX[1];
        gestX[1] = pulledX (kTargetMidX, x) - logX[1];
    }
    if (targeted (kTargetHighX))
    {
        const double x = slotTargeted (kTargetHighX) ? std::log2 (overRange (kTargetHighX, kXoverHigh, std::exp2 (logX[2]))) : logX[2];
        gestX[2] = pulledX (kTargetHighX, x) - logX[2];
    }
    if (targeted (kTargetSeedBlend))
        blend = std::clamp (pulled (kTargetSeedBlend, blendBase), 0.0, 1.0);
    if (targeted (kTargetShift))
        shiftHz = overRange (kTargetShift, kShift, shiftBase);
    // Wobble: the phase runs on by the rate x the beats of this tick (the integral of the rate: no jumps)
    {
        const double lo = std::log2 (kWobbleRateMin), hi = std::log2 (kWobbleRateMax);
        double n = (logWobRate - lo) / (hi - lo);
        if (targeted (kTargetWobbleRate))
            n = std::clamp (pulled (kTargetWobbleRate, n), 0.0, 1.0);
        wobRate = std::exp2 (lo + (hi - lo) * n);
        wobAmtPrev = wobAmt;
        wobAmt = std::clamp (pulled (kTargetWobbleAmount, wobBase), 0.0, 1.0);
        if (snap)
            wobAmtPrev = wobAmt;
        wobPhasePrev = wobPhase;
        wobPhase += wobRate * (double)m * gBpm / 60.0 / sr;
        if (wobPhase >= 1.0)
        {
            const double whole = std::floor (wobPhase);
            wobPhase -= whole;
            wobPhasePrev -= whole;
        }
        wobGain = 1.0 - wobAmt * (0.5 - 0.5 * std::cos (2.0 * dsp::kPi * wobPhase));
    }
    // Close: from Tone's corner (open) down kCloseOctaves, its Q rising with it
    closePrev = closeNow;
    {
        const double c = 1.0 - std::clamp (pulled (kTargetClose, 1.0), 0.0, 1.0);
        if (c > 0.0)
        {
            const double open = std::min (p[kToneOn] >= 0.5 ? std::clamp (p[kTone], kToneMin, kToneMax) : kCloseOpenHz, 0.45 * sr);
            closeHzNow = std::max (open * std::exp2 (-kCloseOctaves * c), 30.0);
            closeNow.g = std::tan (dsp::kPi * closeHzNow / sr);
            const double q = dsp::kSqrt2 * 0.5 * std::pow (kCloseQ * dsp::kSqrt2, c); // (1 / sqrt 2 .. kCloseQ)
            closeNow.k = 1.0 / q;
            closeNow.mix = std::min (1.0, 20.0 * c);
        }
        else
            closeNow.mix = 0.0;
        if (snap || closePrev.mix <= 0.0)
        {
            // (from open: the filter starts where it is now, fading in by its mix alone)
            const double mix = closePrev.mix;
            closePrev = closeNow;
            if (!snap)
                closePrev.mix = mix;
        }
    }
    // Dirt and Bells: how far the bands above Low go to the clean and bare taps (together at most all the way)
    pullDirtPrev = pullDirt;
    pullBellsPrev = pullBells;
    pullDirt = 1.0 - std::clamp (pulled (kTargetDirt, 1.0), 0.0, 1.0);
    pullBells = 1.0 - std::clamp (pulled (kTargetBells, 1.0), 0.0, 1.0);
    if (pullDirt + pullBells > 1.0)
    {
        const double sum = pullDirt + pullBells;
        pullDirt /= sum;
        pullBells /= sum;
    }
    if (snap)
    {
        pullDirtPrev = pullDirt;
        pullBellsPrev = pullBells;
    }
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
    // the gestures' paths on the bands above Low (pass 1 only): the taps' second split, Close, Wobble
    const bool first = pass == 0 && gestActive;
    const bool auxOn = first && (pullDirt > 0.0 || pullDirtPrev > 0.0 || pullBells > 0.0 || pullBellsPrev > 0.0);
    const bool closeOn = first && (closeNow.mix > 0.0 || closePrev.mix > 0.0);
    const bool wobbleOn = first && (wobAmt > 0.0 || wobAmtPrev > 0.0);
    if (shifting || liquidOn || auxOn || closeOn || wobbleOn)
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
            double wob = 1.0, closeMix = 0.0;
            dsp::SvfCoefs cc;
            if (wobbleOn)
            {
                const double ph = wobPhasePrev + (wobPhase - wobPhasePrev) * t, a = wobAmtPrev + (wobAmt - wobAmtPrev) * t;
                wob = 1.0 - a * (0.5 - 0.5 * std::cos (2.0 * dsp::kPi * ph));
            }
            if (closeOn)
            {
                cc.set (closePrev.g + (closeNow.g - closePrev.g) * t, closePrev.k + (closeNow.k - closePrev.k) * t);
                closeMix = closePrev.mix + (closeNow.mix - closePrev.mix) * t;
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
                if (auxOn)
                {
                    // (Dirt, Bells: the difference to the taps, split with the same corners, on the bands above Low)
                    double z[kMaxBands];
                    auxSplit[ch].tick (aux[ch][i], c, z);
                    band[1] += z[1];
                    band[2] += z[2];
                    band[3] += z[3];
                }
                double up = gain[1] * band[1] + gain[2] * band[2] + gain[3] * band[3];
                if (liquidOn)
                {
                    up += a1 * s.liq1[ch].tick (up, lc1).bp;
                    up += a2 * s.liq2[ch].tick (up, lc2).bp;
                }
                if (closeOn)
                    up += (closeLp[ch].tick (up, cc).lp - up) * closeMix;
                if (wobbleOn)
                    up *= wob;
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
    // the gestures' clock: the song position while the host plays, else running on at the last tempo; Wobble's
    // phase set from it when playback starts (so a render from the same place is the same)
    if (playing)
    {
        gBeats = songPpq;
        if (!wasPlaying)
        {
            const double ph = songPpq * wobRate;
            wobPhase = wobPhasePrev = ph - std::floor (ph);
        }
    }
    const double beatsPerSample = gBpm / 60.0 / sr;
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
        glide (blendBase, std::clamp (p[kSeedBlend], 0.0, 1.0), tickSmooth);
        blend = blendBase; // (a gesture may move it: gestureTick)
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
        glide (shiftBase, std::clamp (p[kShift], -2000.0, 2000.0), tickSmooth);
        shiftHz = shiftBase; // (a gesture may move it: gestureTick)
        glide (shiftMix, std::clamp (p[kShiftMix], 0.0, 1.0), tickSmooth);
        shiftFade = std::clamp (shiftFade + (shiftOn ? fadeStep : -fadeStep), 0.0, 1.0);
        glide (link, std::clamp (p[kLink], 0.0, 1.0), tickSmooth);
        glide (liquid, std::clamp (p[kLiquid], 0.0, 1.0), tickSmooth);
        glide (liqRes, std::clamp (p[kLiquidRes], 0.0, 1.0), tickSmooth);
        glide (logLiqLo, std::log2 (std::clamp (p[kLiquidLow], 20.0, 20000.0)), tickSmooth);
        glide (logLiqHi, std::log2 (std::clamp (p[kLiquidHigh], 20.0, 20000.0)), tickSmooth);

        // the gestures at this tick's end
        gestureTick (m, gBeats + (double)(a + m) * beatsPerSample, false);
        const bool tapping = gestActive && (pullDirt > 0.0 || pullDirtPrev > 0.0 || pullBells > 0.0 || pullBellsPrev > 0.0);
        sweep.tick (p.data (), wl, wr, m, tapping ? &taps : nullptr); // (off: not run)
        if (tapping)
        {
            // Dirt and Bells: what takes the stage's output to the clean and bare taps (split in pass 1)
            for (int i = 0; i < m; ++i)
            {
                const double t = (double)(i + 1) / m;
                const double pd = pullDirtPrev + (pullDirt - pullDirtPrev) * t, pb = pullBellsPrev + (pullBells - pullBellsPrev) * t;
                aux[0][i] = pd * (taps.clean[0][i] - wl[i]) + pb * (taps.bare[0][i] - wl[i]);
                aux[1][i] = pd * (taps.clean[1][i] - wr[i]) + pb * (taps.bare[1][i] - wr[i]);
            }
        }
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
        // (the gestures' filters, once they are off: ready to start clean)
        if (closeNow.mix <= 0.0 && closePrev.mix > 0.0)
        {
            closeLp[0].reset ();
            closeLp[1].reset ();
            closePrev.mix = 0.0;
        }
        if (tapping && pullDirt <= 0.0 && pullBells <= 0.0)
        {
            auxSplit[0].reset ();
            auxSplit[1].reset ();
            pullDirtPrev = pullBellsPrev = 0.0;
        }

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

    gBeats += (double)n * beatsPerSample; // (while the host plays, the next block's song position replaces it)

    if (meters)
    {
        constexpr auto rx = std::memory_order_relaxed;
        for (int g = 0; g < kNumGestureSlots; ++g)
        {
            meters->gesturePos[(size_t)g].store ((float)slot[g].pos, rx);
            meters->gestureValue[(size_t)g].store ((float)slot[g].s, rx);
            meters->gesturePull[(size_t)g].store ((float)slot[g].k, rx);
        }
        meters->wobbleGain.store ((float)wobGain, rx);
        meters->scenePos.store ((float)scenePosNow, rx);
        meters->scenePull.store ((float)sceneK, rx);
        for (int i = 0; i < kMaxSceneLanes; ++i)
            meters->laneValue[(size_t)i].store ((float)lane[i].s, rx);
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
