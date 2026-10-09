#include "Sweep.h"

#include "Movement.h"

#include <algorithm>
#include <cmath>

namespace moistr {

namespace {
constexpr double kTwoPi = 2.0 * dsp::kPi;
// the per-filter parameters: bells A .. H, then the shelf (Sweep::kShelfIdx)
constexpr uint32_t rateId (int b) { return b < kNumBells ? bellId (b, kBellRate) : kShelfRate; }
constexpr uint32_t lowId (int b) { return b < kNumBells ? bellId (b, kBellLow) : kShelfLow; }
constexpr uint32_t highId (int b) { return b < kNumBells ? bellId (b, kBellHigh) : kShelfHigh; }
constexpr uint32_t qId (int b) { return b < kNumBells ? bellId (b, kBellWidth) : kShelfQ; }
// a bell's gain to glide to: its Gain while on, 0 dB (flat) while off
inline double bellGainTarget (const double* p, int b) { return p[bellOnId (b)] >= 0.5 ? p[bellId (b, kBellGain)] : 0.0; }
// the Curve's drive offset (dB): Hard 0, Soft 20 log10 (0.7)
inline double curveTarget (const double* p)
{
    static const double soft = 20.0 * std::log10 (kSoftCurve);
    return std::lround (p[kSweepCurve]) == kCurveSoft ? soft : 0.0;
}
// the orbit's smooth random curves: how far Wander 100 % moves the angle (radians), the radius (from 1, the
// most) and the centre (of the range); the curves' rates in cycles of Shelf Rate (slow, at most 0.45: the
// angle never runs backwards, 1.0 x 2 pi x 0.45 < 2 pi)
constexpr double kWanderAngle = 1.0, kWanderRadius = 0.4, kWanderCentre = 0.15;
constexpr double kCurveRateMin = 0.09, kCurveRateMax = 0.45;
constexpr double kCurveWeights[3] = {1.0, 0.7, 0.45};
constexpr double kOrbitStart = dsp::kPi; // the circle starts at Low, half way up its gain
constexpr double kOrbitFadeSec = 0.1;      // a new Seed's orbit fades in over this

inline double clampSr (double hz, double sr) { return std::clamp (hz, 1.0, 0.45 * sr); }
inline void glide (double& x, double t, double c)
{
    x += (t - x) * c;
    if (std::fabs (t - x) < 1e-9)
        x = t;
}
} // namespace

double shelfCeilingDb (double hz, double minDb, double maxDb, double tilt)
{
    if (tilt <= 0.0 || hz <= kTiltFromHz)
        return maxDb;
    const double t = std::clamp (std::log (hz / kTiltFromHz) / std::log (kTiltToHz / kTiltFromHz), 0.0, 1.0);
    return maxDb - std::clamp (tilt, 0.0, 1.0) * kTiltDepth * std::max (0.0, maxDb - minDb) * t * t * (3.0 - 2.0 * t);
}

void ShelfOrbit::setSeed (int s)
{
    seed = s;
    Rng rng (0x5357454550ull * 2654435761ull + (uint64_t)s * 0x9E3779B97F4A7C15ull); // ('SWEEP')
    for (int c = 0; c < 4; ++c)
        for (int k = 0; k < 3; ++k)
        {
            // each curve's three rates spread over the range (log), jittered: incommensurate
            const double u = (k + rng.range (0.15, 0.85)) / 3.0;
            rate[c][k] = kCurveRateMin * std::pow (kCurveRateMax / kCurveRateMin, u);
            phase[c][k] = rng.range (0.0, kTwoPi);
        }
}

double ShelfOrbit::curve (int which, double th) const
{
    double s = 0.0;
    for (int k = 0; k < 3; ++k)
        s += kCurveWeights[k] * std::sin (kTwoPi * rate[which][k] * th + phase[which][k]);
    return s / (kCurveWeights[0] + kCurveWeights[1] + kCurveWeights[2]);
}

void ShelfOrbit::at (double th, double wander, double& u, double& v) const
{
    const double w = std::clamp (wander, 0.0, 1.0);
    double angle = kOrbitStart + kTwoPi * (th - std::floor (th)), radius = 1.0, cu = 0.0, cv = 0.0;
    if (w > 0.0)
    {
        angle += w * kWanderAngle * curve (0, th);
        radius -= w * kWanderRadius * 0.5 * (1.0 + curve (1, th));
        cu = w * kWanderCentre * curve (2, th);
        cv = w * kWanderCentre * curve (3, th);
    }
    // (scaled so the drifting centre never takes it out of the range: no clamping, no corners)
    const double scale = 1.0 / (1.0 + 2.0 * kWanderCentre * w);
    u = 0.5 + (cu + 0.5 * radius * std::cos (angle)) * scale;
    v = 0.5 + (cv + 0.5 * radius * std::sin (angle)) * scale;
}

double Sweep::compensation (double driveDb, double avgDb, double inRms)
{
    const double g = std::pow (10.0, driveDb / 20.0), level = inRms * std::pow (10.0, avgDb / 20.0);
    const double a = std::sqrt (2.0) * level * g;
    // the RMS of tanh (a sin) over a quarter cycle (midpoints)
    constexpr int kN = 32;
    double s = 0.0;
    for (int i = 0; i < kN; ++i)
    {
        const double y = std::tanh (a * std::sin (0.5 * dsp::kPi * (i + 0.5) / kN));
        s += y * y;
    }
    const double rms = std::sqrt (s / kN);
    return rms > 1e-12 ? inRms / rms : 1.0;
}

double guardedBellDb (double gainDb, double centre, double guardHz)
{
    const double t = std::clamp (std::log2 (centre / guardHz), 0.0, 1.0);
    return gainDb * t * t * (3.0 - 2.0 * t);
}

double Sweep::bellsAverageDb (const double* lo, const double* hi, const double* gainDb, const double* q, int n, double guardHz)
{
    // the analog peaking EQ's magnitude: |(1 - w^2 + j w A / Q) / (1 - w^2 + j w / (A Q))|, w = f / centre
    constexpr int kSteps = 16;
    double sum = 0.0, weights = 0.0;
    for (int f = 0; f < 3; ++f)
    {
        double power = 0.0;
        for (int i = 0; i < kSteps; ++i)
        {
            const double m = 0.5 - 0.5 * std::cos (dsp::kPi * (i + 0.5) / kSteps); // (the sweep's places, as often as it is there)
            double gain = 1.0;
            for (int b = 0; b < n; ++b)
            {
                if (gainDb[b] == 0.0)
                    continue; // (flat: its factor would be exactly 1)
                const double centre = lo[b] * std::pow (hi[b] / lo[b], m), w = kAvgRefHz[f] / centre;
                const double a = std::pow (10.0, (guardHz > 0.0 ? guardedBellDb (gainDb[b], centre, guardHz) : gainDb[b]) / 40.0);
                const double re = 1.0 - w * w, num = w * a / q[b], den = w / (a * q[b]);
                gain *= (re * re + num * num) / (re * re + den * den);
            }
            if (guardHz > 0.0)
            {
                // (Guard Bells: below the corner the input, above it the bells; the pair's sides add up in phase)
                const double r8 = std::pow (kAvgRefHz[f] / guardHz, 8.0), low = 1.0 / (1.0 + r8), amp = low + (1.0 - low) * std::sqrt (gain);
                gain = amp * amp;
            }
            power += gain;
        }
        sum += kAvgRefWeight[f] * power / kSteps;
        weights += kAvgRefWeight[f];
    }
    return 10.0 * std::log10 (sum / weights);
}

void Sweep::prepare (double sampleRate)
{
    sr = sampleRate;
    tickSmooth = 1.0 - std::exp (-(double)kTick / (0.03 * sr));
    fadeStep = (double)kTick / (0.02 * sr);
    levelCoef = 1.0 - std::exp (-(double)kTick / (kLevelSec * sr));
    boostCoef = 1.0 - std::exp (-(double)kTick / (kSubSec * sr));
    satLowCoef = 1.0 - std::exp (-(double)kTick / (kSatLowSec * sr));
}

void Sweep::reset (const double* p)
{
    for (int b = 0; b < kFilters; ++b)
    {
        logLo[b] = std::log2 (std::max (p[lowId (b)], 1.0));
        logHi[b] = std::log2 (std::max (p[highId (b)], 1.0));
        logQ[b] = std::log2 (std::max (p[qId (b)], 0.01));
        theta[b] = 0.0;
        for (auto& f : filt[b])
            f.reset ();
    }
    for (int b = 0; b < kNumBells; ++b)
    {
        gainDb[b] = bellGainTarget (p, b);
        phaseDeg[b] = p[bellId (b, kBellPhase)];
        bellRun[b] = gainDb[b] != 0.0;
    }
    shelfMin = p[kShelfMin];
    shelfMax = p[kShelfMax];
    wander = std::clamp (p[kShelfWander], 0.0, 1.0);
    tilt = std::clamp (p[kShelfTilt], 0.0, 1.0);
    driveDb = std::clamp (p[kSweepDrive], 0.0, kSweepDriveMax);
    curveDb = curveTarget (p);
    fade = p[kSweep] >= 0.5 ? 1.0 : 0.0;
    shelfFade = p[kShelf] >= 0.5 ? 1.0 : 0.0;
    subFade = p[kCleanSub] >= 0.5 ? 1.0 : 0.0;
    boostFade = p[kSubBoost] >= 0.5 ? 1.0 : 0.0;
    toneFade = p[kToneOn] >= 0.5 ? 1.0 : 0.0;
    guardFade = p[kSubGuard] >= 0.5 && p[kGuardBells] >= 0.5 ? 1.0 : 0.0;
    logGuard = std::log2 (std::clamp (p[kSubGuardFreq], kGuardFreqMin, kGuardFreqMax));
    guardGPrev = guardGNow = std::tan (dsp::kPi * clampSr (std::exp2 (logGuard), sr) / sr);
    for (int ch = 0; ch < 2; ++ch)
    {
        guardIn[ch].reset ();
        guardOut[ch].reset ();
    }
    satLow = 1.0;
    satLowIn = satLowOut = 0.0;
    steadyNow = false;
    logSplit = std::log2 (std::clamp (p[kSplitFreq], 10.0, 20000.0));
    splitLevelDb = std::clamp (p[kSplitLevel], -60.0, 24.0);
    splitDriveDb = std::clamp (p[kSplitDrive], 0.0, kSplitDriveMax);
    logBoost = std::log2 (std::clamp (p[kSubFreq], 10.0, 20000.0));
    boostLevel = std::clamp (p[kSubLevel], 0.0, 1.0);
    logTone = std::log2 (std::clamp (p[kTone], 10.0, 40000.0));
    for (int ch = 0; ch < 2; ++ch)
    {
        sat[ch].reset ();
        satHigh[ch].reset ();
        splitSat[ch].reset ();
        split[ch].reset ();
        tone[ch].reset ();
        satBare[ch].reset ();
        toneClean[ch].reset ();
        toneBare[ch].reset ();
        for (auto& f : boostLp[ch])
            f.reset ();
    }
    compKey[0] = -1.0;
    levelMs = kSweepRefRms * kSweepRefRms;
    boostLowMs = boostOutMs = 0.0;
    boostPrimed = false;
    boostNow = boostPrev = 0.0;
    orbit.setSeed (std::clamp ((int)std::lround (p[kSeed]), kMinSeed, kMaxSeed));
    orbitFade = 1.0;
    targets (p, true);
}

void Sweep::beginBlock (const double* p, bool playing, bool relocate, double songPpq, double bpm)
{
    lastBpm = bpm > 1.0 ? bpm : 120.0;
    if (!playing || lockOn)
        return; // (Loop Lock: the clocks come from the motion clock, tick by tick)
    for (int b = 0; b < kFilters; ++b)
    {
        if (b < kNumBells && p[bellId (b, kBellSync)] >= 0.5)
            theta[b] = songPpq / kSyncBeats[std::clamp ((int)std::lround (p[bellId (b, kBellSyncRate)]), 0, kNumSyncRates - 1)];
        else if (relocate)
            theta[b] = songPpq * 60.0 / lastBpm * std::clamp (p[rateId (b)], 1e-3, 100.0);
    }
}

void Sweep::targets (const double* p, bool snap)
{
    for (int b = 0; b < kFilters; ++b)
        prev[b] = now[b];
    // the bells
    for (int b = 0; b < kNumBells; ++b)
    {
        const double m = 0.5 - 0.5 * std::cos (kTwoPi * (phaseOf (b) + phaseDeg[b] / 360.0));
        hz[b] = clampSr (std::exp2 (logLo[b] + (logHi[b] - logLo[b]) * m), sr);
        const double q = std::exp2 (logQ[b]);
        double gDb = gainDb[b];
        if (guardFade > 0.0 && gDb != 0.0) // (Guard Bells: the gain scaled down near and below the guard's corner)
            gDb += (guardedBellDb (gDb, hz[b], std::exp2 (logGuard)) - gDb) * guardFade;
        const double a = std::pow (10.0, gDb / 40.0);
        Coefs& c = now[b];
        c.g = std::tan (dsp::kPi * hz[b] / sr);
        c.k = 1.0 / (q * a);
        c.m1 = c.k * (a * a - 1.0);
    }
    // the shelf on its orbit
    {
        double u, v;
        orbit.at (phaseOf (kShelfIdx), wander, u, v);
        if (orbitFade < 1.0)
        {
            // (a new Seed: from the old orbit to the new one over kOrbitFadeSec, a raised cosine)
            double uo, vo;
            oldOrbit.at (phaseOf (kShelfIdx), wander, uo, vo);
            const double w = 0.5 - 0.5 * std::cos (dsp::kPi * orbitFade);
            u = uo + (u - uo) * w;
            v = vo + (v - vo) * w;
        }
        uNow = u;
        vNow = v;
        hz[kShelfIdx] = clampSr (std::exp2 (logLo[kShelfIdx] + (logHi[kShelfIdx] - logLo[kShelfIdx]) * u), sr);
        const double lo = std::min (shelfMin, shelfMax), hi = std::max (shelfMin, shelfMax);
        ceilingNow = shelfCeilingDb (hz[kShelfIdx], lo, hi, tilt);
        shelfGainNow = lo + (ceilingNow - lo) * v;
        const double a = std::pow (10.0, shelfGainNow / 40.0);
        Coefs& c = now[kShelfIdx];
        c.g = std::tan (dsp::kPi * hz[kShelfIdx] / sr) * std::sqrt (a);
        c.k = 1.0 / std::exp2 (logQ[kShelfIdx]);
        c.m0 = a * a;
        c.m1 = c.k * (1.0 - a) * a;
        c.m2 = 1.0 - a * a;
    }
    // the saturator: its drive (on its Curve) and make-up (worked out again only when they change)
    gPrev = gNow;
    compPrev = compNow;
    cleanPrev = cleanNow;
    barePrev = bareNow;
    const double drive = driveDb + curveDb; // (Hard: + 0, exactly the drive)
    gNow = std::pow (10.0, drive / 20.0);
    // (the level in steps of 0.05 dB: worked out again only when it has moved)
    const double levelKey = std::round (10.0 * std::log10 (levelMs) / 0.05);
    double key[kKeySize];
    key[0] = drive;
    key[1] = levelKey;
    for (int b = 0; b < kNumBells; ++b)
    {
        key[2 + 4 * b] = logLo[b];
        key[3 + 4 * b] = logHi[b];
        key[4 + 4 * b] = gainDb[b];
        key[5 + 4 * b] = logQ[b];
    }
    key[kKeySize - 1] = guardFade > 0.0 ? guardFade + logGuard : 0.0; // (Guard Bells)
    if (!std::equal (key, key + kKeySize, compKey))
    {
        std::copy (key, key + kKeySize, compKey);
        double lo[kNumBells], hi[kNumBells], q[kNumBells];
        for (int b = 0; b < kNumBells; ++b)
        {
            lo[b] = std::exp2 (logLo[b]);
            hi[b] = std::exp2 (logHi[b]);
            q[b] = std::exp2 (logQ[b]);
        }
        avgNow = bellsAverageDb (lo, hi, gainDb, q, kNumBells);
        if (guardFade > 0.0)
            avgNow += (bellsAverageDb (lo, hi, gainDb, q, kNumBells, std::exp2 (logGuard)) - avgNow) * guardFade;
        compNow = compensation (drive, avgNow, std::sqrt (levelMs));
        bareNow = compensation (drive, 0.0, std::sqrt (levelMs));
        cleanNow = std::pow (10.0, -avgNow / 20.0);
    }
    // Clean Sub, Sub Boost and Tone: their corners and levels
    splitGPrev = splitGNow;
    boostGPrev = boostGNow;
    toneGPrev = toneGNow;
    splitLevelPrev = splitLevelNow;
    boostPrev = boostNow;
    splitGNow = std::tan (dsp::kPi * clampSr (std::exp2 (logSplit), sr) / sr);
    boostGNow = std::tan (dsp::kPi * clampSr (std::exp2 (logBoost), sr) / sr);
    toneGNow = std::tan (dsp::kPi * clampSr (std::exp2 (logTone), sr) / sr);
    splitLevelNow = std::pow (10.0, splitLevelDb / 20.0);
    // (Sub Boost at Sub Level 1: its lows as loud, in RMS, as the saturated signal)
    const double unit = boostPrimed && boostLowMs > 1e-20 ? std::min (std::sqrt (boostOutMs / boostLowMs), kSubMaxGain) : 0.0;
    boostNow = unit * boostLevel;
    if (snap)
    {
        for (int b = 0; b < kFilters; ++b)
            prev[b] = now[b];
        gPrev = gNow;
        compPrev = compNow;
        cleanPrev = cleanNow;
        barePrev = bareNow;
        splitGPrev = splitGNow;
        boostGPrev = boostGNow;
        toneGPrev = toneGNow;
        splitLevelPrev = splitLevelNow;
        boostPrev = boostNow;
    }
}

void Sweep::tick (const double* p, double* l, double* r, int m, SweepTaps* taps)
{
    if (taps)
        for (int i = 0; i < m; ++i)
        {
            // (the stage not run: both are its input)
            taps->clean[0][i] = taps->bare[0][i] = l[i];
            taps->clean[1][i] = taps->bare[1][i] = r[i];
        }
    // the clocks run on (off too, so the sweep is where it should be when it comes back)
    for (int b = 0; lockOn && b < kFilters; ++b)
    {
        // (Loop Lock: a pure function of the motion clock)
        if (b < kNumBells && p[bellId (b, kBellSync)] >= 0.5)
            theta[b] = lockBeats / kSyncBeats[std::clamp ((int)std::lround (p[bellId (b, kBellSyncRate)]), 0, kNumSyncRates - 1)];
        else
            theta[b] = lockBeats * 60.0 / lockBpm * std::clamp (p[rateId (b)], 1e-3, 100.0);
    }
    for (int b = 0; !lockOn && b < kFilters; ++b)
    {
        double perSample;
        if (b < kNumBells && p[bellId (b, kBellSync)] >= 0.5)
            perSample = lastBpm / 60.0 / kSyncBeats[std::clamp ((int)std::lround (p[bellId (b, kBellSyncRate)]), 0, kNumSyncRates - 1)] / sr;
        else
            perSample = std::clamp (p[rateId (b)], 1e-3, 100.0) / sr;
        theta[b] += perSample * m;
        if (theta[b] > 1e6)
            theta[b] -= std::floor (theta[b]); // (the bells repeat every cycle; the orbit's curves wrap far later)
    }
    const bool on = p[kSweep] >= 0.5;
    steadyNow = false;
    if (!on && fade <= 0.0)
        return;
    const int seed = std::clamp ((int)std::lround (p[kSeed]), kMinSeed, kMaxSeed);
    if (orbit.seed != seed)
    {
        oldOrbit = orbit;
        orbit.setSeed (seed);
        orbitFade = 0.0;
    }
    if (orbitFade < 1.0)
        orbitFade = std::min (1.0, orbitFade + (double)m / (kOrbitFadeSec * sr));
    // the settings glide
    for (int b = 0; b < kFilters; ++b)
    {
        glide (logLo[b], std::log2 (std::max (p[lowId (b)], 1.0)), tickSmooth);
        glide (logHi[b], std::log2 (std::max (p[highId (b)], 1.0)), tickSmooth);
        glide (logQ[b], std::log2 (std::max (p[qId (b)], 0.01)), tickSmooth);
    }
    for (int b = 0; b < kNumBells; ++b)
    {
        glide (gainDb[b], bellGainTarget (p, b), tickSmooth);
        glide (phaseDeg[b], p[bellId (b, kBellPhase)], tickSmooth);
    }
    glide (shelfMin, p[kShelfMin], tickSmooth);
    glide (shelfMax, p[kShelfMax], tickSmooth);
    glide (wander, std::clamp (p[kShelfWander], 0.0, 1.0), tickSmooth);
    glide (tilt, std::clamp (p[kShelfTilt], 0.0, 1.0), tickSmooth);
    glide (driveDb, std::clamp (p[kSweepDrive], 0.0, kSweepDriveMax), tickSmooth);
    glide (curveDb, curveTarget (p), tickSmooth);
    glide (logSplit, std::log2 (std::clamp (p[kSplitFreq], 10.0, 20000.0)), tickSmooth);
    glide (splitLevelDb, std::clamp (p[kSplitLevel], -60.0, 24.0), tickSmooth);
    glide (splitDriveDb, std::clamp (p[kSplitDrive], 0.0, kSplitDriveMax), tickSmooth);
    glide (logBoost, std::log2 (std::clamp (p[kSubFreq], 10.0, 20000.0)), tickSmooth);
    glide (boostLevel, std::clamp (p[kSubLevel], 0.0, 1.0), tickSmooth);
    glide (logTone, std::log2 (std::clamp (p[kTone], 10.0, 40000.0)), tickSmooth);
    // Guard Bells: no bell moves the lows below Sub Guard Freq (with Sub Guard on)
    const double guard0 = guardFade;
    guardFade = std::clamp (guardFade + (p[kSubGuard] >= 0.5 && p[kGuardBells] >= 0.5 ? fadeStep : -fadeStep), 0.0, 1.0);
    const bool guardRun = guardFade > 0.0 || guard0 > 0.0;
    {
        const double target = std::log2 (std::clamp (p[kSubGuardFreq], kGuardFreqMin, kGuardFreqMax));
        if (guard0 <= 0.0)
            logGuard = target;
        else
            glide (logGuard, target, tickSmooth);
        guardGPrev = guardGNow;
        guardGNow = std::tan (dsp::kPi * clampSr (std::exp2 (logGuard), sr) / sr);
        if (guard0 <= 0.0)
            guardGPrev = guardGNow;
    }
    // the input's level (both channels, this tick), held through silence
    bool heard = false;
    {
        double e = 0.0;
        for (int i = 0; i < m; ++i)
            e += l[i] * l[i] + r[i] * r[i];
        e /= 2.0 * m;
        heard = e > kLevelGateRms * kLevelGateRms;
        if (heard)
            levelMs = std::clamp (levelMs + (e - levelMs) * levelCoef * m / kTick, kLevelMinRms * kLevelMinRms, kLevelMaxRms * kLevelMaxRms);
    }
    targets (p, false);
    const double fade0 = fade, shelf0 = shelfFade, sub0 = subFade, boost0 = boostFade, tone0 = toneFade;
    auto step = [&] (double& f, bool to) { f = std::clamp (f + (to ? fadeStep : -fadeStep), 0.0, 1.0); };
    step (fade, on);
    step (shelfFade, p[kShelf] >= 0.5);
    step (subFade, p[kCleanSub] >= 0.5);
    step (boostFade, p[kSubBoost] >= 0.5);
    step (toneFade, p[kToneOn] >= 0.5);
    const bool shelfRun = shelfFade > 0.0 || shelf0 > 0.0;
    const bool splitRun = subFade > 0.0 || sub0 > 0.0, wholeRun = subFade < 1.0 || sub0 < 1.0;
    const bool boostRun = boostFade > 0.0 || boost0 > 0.0, toneRun = toneFade > 0.0 || tone0 > 0.0;
    // a bell at 0 dB at both ends of the tick is flat (x + 0 x band-pass): not run, and starts clean when it comes back
    int run[kNumBells], runs = 0;
    for (int b = 0; b < kNumBells; ++b)
    {
        const bool was = bellRun[b];
        bellRun[b] = prev[b].m1 != 0.0 || now[b].m1 != 0.0;
        if (bellRun[b])
            run[runs++] = b;
        else if (was)
            for (auto& s : filt[b])
                s.reset ();
    }
    // Split Drive: its gain, and how far it is blended in (0 dB: clean, not run)
    const bool splitDriving = splitRun && splitDriveDb > 0.0;
    const double splitDriveG = std::pow (10.0, splitDriveDb / 20.0), splitDriveMix = std::min (1.0, splitDriveDb / kSplitDriveBlendDb);
    double lowEnergy = 0.0, outEnergy = 0.0, satInE = 0.0, satOutE = 0.0;
    for (int i = 0; i < m; ++i)
    {
        const double t = (double)(i + 1) / m;
        dsp::SvfCoefs c[kFilters];
        double m1[kFilters];
        for (int k = 0; k < runs; ++k)
        {
            const int b = run[k];
            c[b].set (prev[b].g + (now[b].g - prev[b].g) * t, prev[b].k + (now[b].k - prev[b].k) * t);
            m1[b] = prev[b].m1 + (now[b].m1 - prev[b].m1) * t;
        }
        constexpr int s = kShelfIdx;
        c[s].set (prev[s].g + (now[s].g - prev[s].g) * t, prev[s].k + (now[s].k - prev[s].k) * t);
        m1[s] = prev[s].m1 + (now[s].m1 - prev[s].m1) * t;
        const double m0 = prev[s].m0 + (now[s].m0 - prev[s].m0) * t, m2 = prev[s].m2 + (now[s].m2 - prev[s].m2) * t;
        const double g = gPrev + (gNow - gPrev) * t, comp = compPrev + (compNow - compPrev) * t;
        const double cleanGain = taps ? cleanPrev + (cleanNow - cleanPrev) * t : 0.0, bareComp = taps ? barePrev + (bareNow - barePrev) * t : 0.0;
        const double f = fade0 + (fade - fade0) * t, sf = shelf0 + (shelfFade - shelf0) * t;
        const double subF = sub0 + (subFade - sub0) * t, boostF = boost0 + (boostFade - boost0) * t, toneF = tone0 + (toneFade - tone0) * t;
        dsp::SvfCoefs cSplit, cBoost, cTone;
        if (splitRun)
            cSplit.set (splitGPrev + (splitGNow - splitGPrev) * t, dsp::kSqrt2);
        if (boostRun)
            cBoost.set (boostGPrev + (boostGNow - boostGPrev) * t, dsp::kSqrt2);
        if (toneRun)
            cTone.set (toneGPrev + (toneGNow - toneGPrev) * t, dsp::kSqrt2);
        const double splitLevel = splitLevelPrev + (splitLevelNow - splitLevelPrev) * t;
        dsp::SvfCoefs cGuard[2];
        double gb = 0.0;
        if (guardRun)
        {
            const double gg = guardGPrev + (guardGNow - guardGPrev) * t;
            dsp::Lr8Split::coefs (gg, cGuard);
            gb = guard0 + (guardFade - guard0) * t;
        }

        const double boost = (boostPrev + (boostNow - boostPrev) * t) * boostF;
        double* io[2] = {l + i, r + i};
        for (int ch = 0; ch < 2; ++ch)
        {
            const double x = *io[ch];
            double y = x;
            for (int k = 0; k < runs; ++k)
            {
                const int b = run[k];
                y += m1[b] * filt[b][ch].tick (y, c[b]).bp;
            }
            double below = 0.0;
            if (guardRun)
            {
                // Guard Bells: the bells' output above Sub Guard Freq, the input below it
                below = guardIn[ch].low (x, cGuard);
                const double guarded = below + guardOut[ch].high (y, cGuard);
                y += (guarded - y) * gb;
            }

            if (shelfRun)
            {
                const dsp::Svf::Out o = filt[s][ch].tick (y, c[s]);
                const double shelved = m0 * y + m1[s] * o.bp + m2 * o.lp;
                y += (shelved - y) * sf;
            }
            const double pre = y; // (the saturator's input: Sub Boost's lows come from here)
            if (taps)
            {
                double clean = pre * cleanGain, bare = satBare[ch].tick (g * x) * bareComp;
                if (toneRun)
                {
                    clean += (toneClean[ch].tick (clean, cTone).lp - clean) * toneF;
                    bare += (toneBare[ch].tick (bare, cTone).lp - bare) * toneF;
                }
                taps->clean[ch][i] = x + (clean - x) * f;
                taps->bare[ch][i] = x + (bare - x) * f;
            }
            // the saturator: all of it, or (Clean Sub) the band above Split Freq with the lows around it
            double whole = 0.0, parted = 0.0, partedHeld = 0.0;
            if (wholeRun)
                whole = sat[ch].tick (g * y) * comp;
            if (splitRun)
            {
                double low, high;
                split[ch].tick (y, cSplit, low, high);
                if (splitDriving)
                    low += (splitSat[ch].tick (splitDriveG * low) / splitDriveG - low) * splitDriveMix;
                parted = satHigh[ch].tick (g * high) * comp + low * g * comp * splitLevel;
                partedHeld = high * satLow + low * g * comp * splitLevel; // (Sub Guard's: Clean Sub's lows as they go around)
            }
            y = !splitRun ? whole : !wholeRun ? parted : whole + (parted - whole) * subF;
            double held = 0.0;
            if (guardRun)
            {
                // Sub Guard's tap: the saturator's input at its gain on the lows alone (Clean Sub: its lows as they go around)
                const double sat1 = std::tanh (g * below) * comp;
                satInE += below * below;
                satOutE += sat1 * sat1;
                held = !splitRun ? pre * satLow : !wholeRun ? partedHeld : pre * satLow + (partedHeld - pre * satLow) * subF;
            }
            if (boostRun)
            {
                // Sub Boost: the lows before the saturator, added after it (at the saturated signal's RMS x Sub Level)
                const double low = boostLp[ch][1].tick (boostLp[ch][0].tick (pre, cBoost).lp, cBoost).lp;
                lowEnergy += low * low;
                outEnergy += y * y;
                y += low * boost;
                held += low * boost;
            }
            if (toneRun)
                y += (tone[ch].tick (y, cTone).lp - y) * toneF;
            *io[ch] = x + (y - x) * f;
            if (guardRun)
                steady[ch][i] = x + (held - x) * f;
        }
    }
    if (guardRun)
    {
        steadyNow = true;
        // the saturator's gain on the lows alone: the slow RMS of its output against its input, held through silence
        if (satInE > 2.0 * m * kLevelGateRms * kLevelGateRms)
        {
            satLowIn += (satInE / (2.0 * m) - satLowIn) * satLowCoef * m / kTick;
            satLowOut += (satOutE / (2.0 * m) - satLowOut) * satLowCoef * m / kTick;
            if (satLowIn > 1e-20)
                satLow = std::sqrt (satLowOut / satLowIn);
        }
    }
    // Sub Boost's level match: the slow RMS of its lows and of the saturated signal, held through silence
    if (boostRun && heard)
    {
        const double el = lowEnergy / (2.0 * m), eo = outEnergy / (2.0 * m);
        if (!boostPrimed)
            boostHeard = 0.0;
        boostHeard += m;
        boostPrimed = true;
        const double c = std::max (boostCoef * m / kTick, (double)m / boostHeard); // (a plain mean at first)
        boostLowMs += (el - boostLowMs) * c;
        boostOutMs += (eo - boostOutMs) * c;
    }
    // what stopped running starts clean when it comes back
    if (shelfFade <= 0.0 && shelf0 > 0.0)
        for (auto& f : filt[kShelfIdx])
            f.reset ();
    for (int ch = 0; ch < 2; ++ch)
    {
        if (subFade <= 0.0 && sub0 > 0.0)
        {
            split[ch].reset ();
            satHigh[ch].reset ();
            splitSat[ch].reset ();
        }
        if (subFade >= 1.0 && sub0 < 1.0)
            sat[ch].reset ();
        if (boostFade <= 0.0 && boost0 > 0.0)
            for (auto& f : boostLp[ch])
                f.reset ();
        if (toneFade <= 0.0 && tone0 > 0.0)
        {
            tone[ch].reset ();
            toneClean[ch].reset ();
            toneBare[ch].reset ();
        }
    }
    if (boostFade <= 0.0 && boost0 > 0.0)
        boostPrimed = false;
    if (guardFade <= 0.0 && guard0 > 0.0)
        for (int ch = 0; ch < 2; ++ch)
        {
            guardIn[ch].reset ();
            guardOut[ch].reset ();
        }

    if (fade <= 0.0)
    {
        // (faded out: off, and ready to start clean)
        for (auto& b : filt)
            for (auto& f : b)
                f.reset ();
        for (int ch = 0; ch < 2; ++ch)
        {
            sat[ch].reset ();
            satHigh[ch].reset ();
            splitSat[ch].reset ();
            split[ch].reset ();
            tone[ch].reset ();
            satBare[ch].reset ();
            toneClean[ch].reset ();
            toneBare[ch].reset ();
            for (auto& f : boostLp[ch])
                f.reset ();
            guardIn[ch].reset ();
            guardOut[ch].reset ();
        }
        boostPrimed = false;
    }
}

} // namespace moistr
