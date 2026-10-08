#include "Sweep.h"

#include "Movement.h"

#include <algorithm>
#include <cmath>

namespace moistr {

namespace {
constexpr double kTwoPi = 2.0 * dsp::kPi;
constexpr uint32_t kRateIds[3] = {kARate, kBRate, kShelfRate};
constexpr uint32_t kLowIds[3] = {kALow, kBLow, kShelfLow};
constexpr uint32_t kHighIds[3] = {kAHigh, kBHigh, kShelfHigh};
constexpr uint32_t kQIds[3] = {kAWidth, kBWidth, kShelfQ};
constexpr uint32_t kSyncIds[2] = {kASync, kBSync};
constexpr uint32_t kSyncRateIds[2] = {kASyncRate, kBSyncRate};
constexpr uint32_t kGainIds[2] = {kAGain, kBGain};
constexpr uint32_t kPhaseIds[2] = {kAPhase, kBPhase};
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

double Sweep::bellsAverageDb (const double* lo, const double* hi, const double* gainDb, const double* q)
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
            for (int b = 0; b < 2; ++b)
            {
                const double centre = lo[b] * std::pow (hi[b] / lo[b], m), w = kAvgRefHz[f] / centre, a = std::pow (10.0, gainDb[b] / 40.0);
                const double re = 1.0 - w * w, num = w * a / q[b], den = w / (a * q[b]);
                gain *= (re * re + num * num) / (re * re + den * den);
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
}

void Sweep::reset (const double* p)
{
    for (int b = 0; b < 3; ++b)
    {
        logLo[b] = std::log2 (std::max (p[kLowIds[b]], 1.0));
        logHi[b] = std::log2 (std::max (p[kHighIds[b]], 1.0));
        logQ[b] = std::log2 (std::max (p[kQIds[b]], 0.01));
        theta[b] = 0.0;
        for (auto& f : filt[b])
            f.reset ();
    }
    for (int b = 0; b < 2; ++b)
    {
        gainDb[b] = p[kGainIds[b]];
        phaseDeg[b] = p[kPhaseIds[b]];
    }
    shelfMin = p[kShelfMin];
    shelfMax = p[kShelfMax];
    wander = std::clamp (p[kShelfWander], 0.0, 1.0);
    tilt = std::clamp (p[kShelfTilt], 0.0, 1.0);
    driveDb = std::clamp (p[kSweepDrive], 0.0, kSweepDriveMax);
    fade = p[kSweep] >= 0.5 ? 1.0 : 0.0;
    shelfFade = p[kShelf] >= 0.5 ? 1.0 : 0.0;
    sat[0].reset ();
    sat[1].reset ();
    compKey[0] = -1.0;
    levelMs = kSweepRefRms * kSweepRefRms;
    orbit.setSeed (std::clamp ((int)std::lround (p[kSeed]), kMinSeed, kMaxSeed));
    orbitFade = 1.0;
    targets (p, true);
}

void Sweep::beginBlock (const double* p, bool playing, bool relocate, double songPpq, double bpm)
{
    lastBpm = bpm > 1.0 ? bpm : 120.0;
    if (!playing)
        return;
    for (int b = 0; b < 3; ++b)
    {
        if (b < 2 && p[kSyncIds[b]] >= 0.5)
            theta[b] = songPpq / kSyncBeats[std::clamp ((int)std::lround (p[kSyncRateIds[b]]), 0, kNumSyncRates - 1)];
        else if (relocate)
            theta[b] = songPpq * 60.0 / lastBpm * std::clamp (p[kRateIds[b]], 1e-3, 100.0);
    }
}

void Sweep::targets (const double* p, bool snap)
{
    for (int b = 0; b < 3; ++b)
        prev[b] = now[b];
    // the bells
    for (int b = 0; b < 2; ++b)
    {
        const double m = 0.5 - 0.5 * std::cos (kTwoPi * (theta[b] + phaseDeg[b] / 360.0));
        hz[b] = clampSr (std::exp2 (logLo[b] + (logHi[b] - logLo[b]) * m), sr);
        const double a = std::pow (10.0, gainDb[b] / 40.0), q = std::exp2 (logQ[b]);
        Coefs& c = now[b];
        c.g = std::tan (dsp::kPi * hz[b] / sr);
        c.k = 1.0 / (q * a);
        c.m1 = c.k * (a * a - 1.0);
    }
    // the shelf on its orbit
    {
        double u, v;
        orbit.at (theta[2], wander, u, v);
        if (orbitFade < 1.0)
        {
            // (a new Seed: from the old orbit to the new one over kOrbitFadeSec, a raised cosine)
            double uo, vo;
            oldOrbit.at (theta[2], wander, uo, vo);
            const double w = 0.5 - 0.5 * std::cos (dsp::kPi * orbitFade);
            u = uo + (u - uo) * w;
            v = vo + (v - vo) * w;
        }
        uNow = u;
        vNow = v;
        hz[2] = clampSr (std::exp2 (logLo[2] + (logHi[2] - logLo[2]) * u), sr);
        const double lo = std::min (shelfMin, shelfMax), hi = std::max (shelfMin, shelfMax);
        ceilingNow = shelfCeilingDb (hz[2], lo, hi, tilt);
        shelfGainNow = lo + (ceilingNow - lo) * v;
        const double a = std::pow (10.0, shelfGainNow / 40.0);
        Coefs& c = now[2];
        c.g = std::tan (dsp::kPi * hz[2] / sr) * std::sqrt (a);
        c.k = 1.0 / std::exp2 (logQ[2]);
        c.m0 = a * a;
        c.m1 = c.k * (1.0 - a) * a;
        c.m2 = 1.0 - a * a;
    }
    // the saturator: its drive and make-up (worked out again only when they change)
    gPrev = gNow;
    compPrev = compNow;
    gNow = std::pow (10.0, driveDb / 20.0);
    // (the level in steps of 0.05 dB: worked out again only when it has moved)
    const double levelKey = std::round (10.0 * std::log10 (levelMs) / 0.05);
    const double key[10] = {driveDb, logLo[0], logHi[0], logLo[1], logHi[1], gainDb[0], gainDb[1], logQ[0], logQ[1], levelKey};
    if (!std::equal (key, key + 10, compKey))
    {
        std::copy (key, key + 10, compKey);
        const double lo[2] = {std::exp2 (logLo[0]), std::exp2 (logLo[1])}, hi[2] = {std::exp2 (logHi[0]), std::exp2 (logHi[1])};
        const double q[2] = {std::exp2 (logQ[0]), std::exp2 (logQ[1])};
        avgNow = bellsAverageDb (lo, hi, gainDb, q);
        compNow = compensation (driveDb, avgNow, std::sqrt (levelMs));
    }
    if (snap)
    {
        for (int b = 0; b < 3; ++b)
            prev[b] = now[b];
        gPrev = gNow;
        compPrev = compNow;
    }
}

void Sweep::tick (const double* p, double* l, double* r, int m)
{
    // the clocks run on (off too, so the sweep is where it should be when it comes back)
    for (int b = 0; b < 3; ++b)
    {
        double perSample;
        if (b < 2 && p[kSyncIds[b]] >= 0.5)
            perSample = lastBpm / 60.0 / kSyncBeats[std::clamp ((int)std::lround (p[kSyncRateIds[b]]), 0, kNumSyncRates - 1)] / sr;
        else
            perSample = std::clamp (p[kRateIds[b]], 1e-3, 100.0) / sr;
        theta[b] += perSample * m;
        if (theta[b] > 1e6)
            theta[b] -= std::floor (theta[b]); // (the bells repeat every cycle; the orbit's curves wrap far later)
    }
    const bool on = p[kSweep] >= 0.5;
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
    for (int b = 0; b < 3; ++b)
    {
        glide (logLo[b], std::log2 (std::max (p[kLowIds[b]], 1.0)), tickSmooth);
        glide (logHi[b], std::log2 (std::max (p[kHighIds[b]], 1.0)), tickSmooth);
        glide (logQ[b], std::log2 (std::max (p[kQIds[b]], 0.01)), tickSmooth);
    }
    for (int b = 0; b < 2; ++b)
    {
        glide (gainDb[b], p[kGainIds[b]], tickSmooth);
        glide (phaseDeg[b], p[kPhaseIds[b]], tickSmooth);
    }
    glide (shelfMin, p[kShelfMin], tickSmooth);
    glide (shelfMax, p[kShelfMax], tickSmooth);
    glide (wander, std::clamp (p[kShelfWander], 0.0, 1.0), tickSmooth);
    glide (tilt, std::clamp (p[kShelfTilt], 0.0, 1.0), tickSmooth);
    glide (driveDb, std::clamp (p[kSweepDrive], 0.0, kSweepDriveMax), tickSmooth);
    // the input's level (both channels, this tick), held through silence
    {
        double e = 0.0;
        for (int i = 0; i < m; ++i)
            e += l[i] * l[i] + r[i] * r[i];
        e /= 2.0 * m;
        if (e > kLevelGateRms * kLevelGateRms)
            levelMs = std::clamp (levelMs + (e - levelMs) * levelCoef * m / kTick, kLevelMinRms * kLevelMinRms, kLevelMaxRms * kLevelMaxRms);
    }
    targets (p, false);
    const double fade0 = fade, shelf0 = shelfFade;
    fade = std::clamp (fade + (on ? fadeStep : -fadeStep), 0.0, 1.0);
    shelfFade = std::clamp (shelfFade + (p[kShelf] >= 0.5 ? fadeStep : -fadeStep), 0.0, 1.0);
    const bool shelfRun = shelfFade > 0.0 || shelf0 > 0.0;
    for (int i = 0; i < m; ++i)
    {
        const double t = (double)(i + 1) / m;
        dsp::SvfCoefs c[3];
        double m1[3];
        for (int b = 0; b < 3; ++b)
        {
            c[b].set (prev[b].g + (now[b].g - prev[b].g) * t, prev[b].k + (now[b].k - prev[b].k) * t);
            m1[b] = prev[b].m1 + (now[b].m1 - prev[b].m1) * t;
        }
        const double m0 = prev[2].m0 + (now[2].m0 - prev[2].m0) * t, m2 = prev[2].m2 + (now[2].m2 - prev[2].m2) * t;
        const double g = gPrev + (gNow - gPrev) * t, comp = compPrev + (compNow - compPrev) * t;
        const double f = fade0 + (fade - fade0) * t, sf = shelf0 + (shelfFade - shelf0) * t;
        double* io[2] = {l + i, r + i};
        for (int ch = 0; ch < 2; ++ch)
        {
            const double x = *io[ch];
            double y = x + m1[0] * filt[0][ch].tick (x, c[0]).bp;
            y += m1[1] * filt[1][ch].tick (y, c[1]).bp;
            if (shelfRun)
            {
                const dsp::Svf::Out o = filt[2][ch].tick (y, c[2]);
                const double shelved = m0 * y + m1[2] * o.bp + m2 * o.lp;
                y += (shelved - y) * sf;
            }
            y = sat[ch].tick (g * y) * comp;
            *io[ch] = x + (y - x) * f;
        }
    }
    if (shelfFade <= 0.0 && shelf0 > 0.0)
        for (auto& s : filt[2])
            s.reset ();
    if (fade <= 0.0)
    {
        // (faded out: off, and ready to start clean)
        for (auto& b : filt)
            for (auto& s : b)
                s.reset ();
        sat[0].reset ();
        sat[1].reset ();
    }
}

} // namespace moistr
