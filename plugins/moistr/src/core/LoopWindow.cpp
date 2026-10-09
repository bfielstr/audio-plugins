#include "LoopWindow.h"

#include "ParaSplit.h"
#include "Sweep.h"

#include <algorithm>
#include <cmath>

namespace moistr {

namespace {
constexpr double kTwoPi = 6.28318530717958647692;
const char* const kBellNames[kNumBells] = {"A", "B", "C", "D", "E", "F", "G", "H"};

int choiceOf (const double* p, uint32_t id, int n) { return std::clamp ((int)std::lround (p[id]), 0, n - 1); }
// a rate's period in beats: a synced one's beats, else 1 / Hz at the tempo
double periodBeats (bool sync, double syncBeats, double hz, double bpm) { return sync ? syncBeats : bpm / 60.0 / std::max (hz, 1e-3); }
bool bellOn (const double* p, int b) { return p[kSweep] >= 0.5 && p[bellOnId (b)] >= 0.5 && p[bellId (b, kBellGain)] != 0.0; }
double bellPeriod (const double* p, int b, double bpm)
{
    return periodBeats (p[bellId (b, kBellSync)] >= 0.5, kSyncBeats[choiceOf (p, bellId (b, kBellSyncRate), kNumSyncRates)], p[bellId (b, kBellRate)], bpm);
}
double bandCycle (const double* p, double bpm) { return periodBeats (p[kSync] >= 0.5, kSyncBeats[choiceOf (p, kSyncRate, kNumSyncRates)], p[kRate], bpm); }
bool bandsMove (const double* p) { return p[kMovement] > 0.0 && (p[kMidMove] > 0.0 || p[kHighMove] > 0.0 || p[kAirMove] > 0.0 || p[kLowPush] > 0.0 || p[kLowDip] > 0.0); }
int patternLoop (const Pattern* pat)
{
    int l = 1;
    for (int b = 0; pat && b < kMaxBands; ++b)
        l = std::max (l, pat->band[b].loop);
    return l;
}
double sceneLength (const double* p, const Scene& s)
{
    const int lc = choiceOf (p, kSceneLength, kNumGestureLengths);
    return lc == 0 ? s.length : kGestureLengthBeats[lc];
}

// every active modulator's period, to `add (name, beats)` (name: a static string, or the bell's / slot's index); with
// Drift, each period divided by its modulator's speed factor
template <typename Add0>
void collectParts (const double* p, double bpm, const Scene* scene, const Pattern* a, const Pattern* b, const MotionDrift* drift, Add0&& add0)
{
    bpm = bpm > 1.0 ? bpm : 120.0;
    auto partOf = [] (const char* name, int k) {
        const std::string n = name;
        return n == "Bell" ? kDriftBell + k : n == "High Shelf" ? (int)kDriftShelf : n == "Bands" ? (int)kDriftBands : n == "Gesture" ? (int)kDriftGesture
               : n == "Wobble" ? (int)kDriftWobble : n == "PARA" ? (int)kDriftPara : -1;
    };
    auto add = [&] (const char* name, int k, double beats) {
        const int part = drift && drift->on ? partOf (name, k) : -1;
        add0 (name, k, part >= 0 ? beats / drift->factor[part] : beats);
    };
    for (int k = 0; k < kNumBells; ++k)
        if (bellOn (p, k))
            add ("Bell", k, bellPeriod (p, k, bpm));
    if (p[kSweep] >= 0.5 && p[kShelf] >= 0.5)
        add ("High Shelf", -1, periodBeats (false, 0.0, p[kShelfRate], bpm));
    if (bandsMove (p) || p[kLiquid] > 0.0)
        add ("Bands", -1, bandCycle (p, bpm) * std::max (patternLoop (a), patternLoop (b)));
    if (scene && scene->count > 0 && p[kSceneAmount] > 0.0)
    {
        const double len = sceneLength (p, *scene), speed = kGestureSpeeds[choiceOf (p, kSceneSpeed, kNumGestureSpeeds)];
        if (std::lround (p[kSceneMode]) != kModeWalk)
            add ("Gesture", -1, len);
        else if (speed > 0.0)
            add ("Gesture", -1, 2.0 * len / speed);
    }
    for (int g = 0; g < kNumGestureSlots; ++g)
        if (std::lround (p[gestureId (g, kGestureTarget)]) != kTargetOff)
        {
            const int lc = choiceOf (p, gestureId (g, kGestureLength), kNumGestureLengths);
            const int ch = (int)std::lround (p[gestureId (g, kGestureChoice)]);
            add ("Slot", g, lc != 0 ? kGestureLengthBeats[lc] : ch >= 0 && ch < kNumFactoryGestures ? factoryGesture (ch).length : 4.0);
        }
    if (p[kWobbleAmount] > 0.0)
        add ("Wobble", -1, 1.0 / std::clamp (p[kWobbleRate], kWobbleRateMin, kWobbleRateMax));
    if (p[kParaOn] >= 0.5)
        add ("PARA", -1, kParaRateBeats[choiceOf (p, kParaRate, kNumParaRates)]);
}
} // namespace

std::vector<WindowPart> windowParts (const double* p, double bpm, const Scene* scene, const Pattern* a, const Pattern* b, const MotionDrift* drift)
{
    std::vector<WindowPart> out;
    collectParts (p, bpm, scene, a, b, drift, [&] (const char* name, int k, double beats) {
        std::string n = name;
        if (k >= 0)
            n += std::string (" ") + (n == "Bell" ? std::string (kBellNames[k]) : std::to_string (k + 1));
        out.push_back ({n, beats});
    });
    return out;
}

double windowBeats (const double* p, double bpm, const Scene* scene, const Pattern* a, const Pattern* b, const MotionDrift* drift)
{
    double w = 0.0;
    collectParts (p, bpm, scene, a, b, drift, [&] (const char*, int, double beats) { w = std::max (w, beats); });
    return w > 0.0 ? std::min (w, kLoopMaxWindowBeats) : 4.0;
}

MotionDrift makeDrift (int seed, double startDrift, double speedDrift)
{
    MotionDrift d;
    if (seed <= 0)
        return d;
    d.on = true;
    const double start = std::clamp (startDrift, 0.0, 1.0), speed = std::clamp (speedDrift, 0.0, kSpeedDriftMax);
    for (int i = 0; i < kDriftParts; ++i)
    {
        Rng rng (0xD71F7ull * (uint64_t)seed + 0x9E3779B97F4A7C15ull * (uint64_t)(i + 1));
        d.offset[i] = start * rng.uniform ();
        d.factor[i] = 1.0 + speed * (2.0 * rng.uniform () - 1.0);
    }
    return d;
}

std::vector<MotionCurve> motionCurves (const double* p, double bpm, const Scene* scene, const Pattern* a, double tau, const MotionDrift* drift)
{
    std::vector<MotionCurve> out;
    bpm = bpm > 1.0 ? bpm : 120.0;
    // (a phase in cycles with Drift: x factor + offset)
    auto ph = [&] (int part, double cycles) { return drift && drift->on ? cycles * drift->factor[part] + drift->offset[part] : cycles; };
    for (int k = 0; k < kNumBells; ++k)
        if (bellOn (p, k))
        {
            const double th = ph (kDriftBell + k, tau / bellPeriod (p, k, bpm)) + p[bellId (k, kBellPhase)] / 360.0;
            out.push_back ({std::string ("Bell ") + kBellNames[k], 0.5 - 0.5 * std::cos (kTwoPi * th)});
        }
    if (p[kSweep] >= 0.5 && p[kShelf] >= 0.5)
    {
        ShelfOrbit o;
        o.setSeed (std::clamp ((int)std::lround (p[kSeed]), kMinSeed, kMaxSeed));
        double u, v;
        o.at (ph (kDriftShelf, tau / periodBeats (false, 0.0, p[kShelfRate], bpm)), std::clamp (p[kShelfWander], 0.0, 1.0), u, v);
        out.push_back ({"High Shelf", u});
    }
    if (a && bandsMove (p))
    {
        const double cycle = bandCycle (p, bpm), th = ph (kDriftBands, tau / cycle), sec = cycle * 60.0 / bpm;
        const double speed = std::clamp (p[kSpeed], 1.0, 16.0), density = std::clamp (p[kDensity], 0.25, 8.0);
        static const char* const names[kMaxBands] = {"Low", "Mid", "High", "Air"};
        static constexpr uint32_t moves[kMaxBands] = {kLowMove, kMidMove, kHighMove, kAirMove};
        for (int b = kBandMid; b < kMaxBands; ++b)
        {
            if (p[moves[b]] <= 0.0 || (b == kBandAir && std::lround (p[kBandCount]) != kBands4))
                continue;
            const BandMotion& m = a->band[b];
            out.push_back ({names[b], m.lift (th, sec, std::max (m.rise * p[kRise] / speed, kMinRampSec), std::max (m.fall * p[kFall] / speed, kMinRampSec),
                                             density, m.mask)});
        }
    }
    if (scene && scene->count > 0 && p[kSceneAmount] > 0.0)
    {
        const int sc = choiceOf (p, kSceneSpeed, kNumGestureSpeeds);
        const int mode = std::lround (p[kSceneMode]) == kModeWalk ? kModeWalk : kModeLoop;
        const double len = sceneLength (p, *scene);
        const double pos = gesturePosition (mode, ph (kDriftGesture, tau / len) * len, len, kGestureSpeeds[sc], std::clamp (p[kScenePosition], 0.0, 1.0));
        for (int i = 0; i < scene->count && i < kMaxSceneLanes; ++i)
            out.push_back ({kTargetNames[std::clamp (scene->lane[i].target, 0, kNumTargets - 1)], scene->lane[i].curve.at (pos * scene->length)});
    }
    if (p[kWobbleAmount] > 0.0)
        out.push_back ({"Wobble", 0.5 + 0.5 * std::cos (kTwoPi * ph (kDriftWobble, tau * std::clamp (p[kWobbleRate], kWobbleRateMin, kWobbleRateMax)))});
    if (p[kParaOn] >= 0.5)
    {
        const ParaSplit::Shape s = ParaSplit::shapeAt (p, ph (kDriftPara, tau / kParaRateBeats[choiceOf (p, kParaRate, kNumParaRates)]));
        out.push_back ({"Split LP", s.lpLevel});
        out.push_back ({"Split HP", s.hpPos});
    }
    return out;
}

double regionLength (double start, double end, double window, int lengthChoice, int shape, double bpm)
{
    if (lengthChoice < kLoopNatural)
        return kLoopLengthBeats[std::clamp (lengthChoice, 0, kNumLoopLengths - 1)];
    // Natural: the region at its own speed (Bounce: there and back), Wrap's glide back after it, up to the next 1/16 beat
    const double d = std::max (end - start, kLoopMinRegion) * window;
    const double need = shape == kLoopBounce ? 2.0 * d : d + kLoopNaturalGlideSec * bpm / 60.0;
    return std::max (kLoopNaturalGrid, std::ceil (need / kLoopNaturalGrid - 1e-9) * kLoopNaturalGrid);
}

double regionMotion (double song, double start, double end, double window, int lengthChoice, int shape, double bpm)
{
    bpm = bpm > 1.0 ? bpm : 120.0;
    const double len = regionLength (start, end, window, lengthChoice, shape, bpm);
    const double v = song / len, u = v - std::floor (v);
    const double s = start * window, d = std::max (end - start, kLoopMinRegion) * window;
    if (lengthChoice >= kLoopNatural)
        return naturalMotion (u * len, len, start, end, window, shape, bpm);
    double f;
    if (shape == kLoopBounce)
        f = u < 0.5 ? 2.0 * u : 2.0 - 2.0 * u;
    else
    {
        // Wrap: forward over the region, then back to its start over its last part (a 16th of it, 12 .. 60 ms; a raised
        // cosine), so the motion clock never jumps
        const double sec = len * 60.0 / bpm, back = std::clamp (sec / 16.0, 0.012, 0.06);
        const double e = std::min (back / sec, 0.25);
        f = u < 1.0 - e ? u / (1.0 - e) : 0.5 + 0.5 * std::cos (3.14159265358979323846 * (u - (1.0 - e)) / e);
    }
    return s + d * f;
}

double naturalMotion (double t, double len, double start, double end, double window, int shape, double bpm)
{
    bpm = bpm > 1.0 ? bpm : 120.0;
    const double s = start * window, d = std::max (end - start, kLoopMinRegion) * window;
    t = std::clamp (t, 0.0, len);
    if (shape == kLoopBounce)
    {
        // (forward, back, then held at the start for what is left of the 16th)
        const double h = std::min (d, 0.5 * len);
        return s + (t < h ? t : t < 2.0 * h ? 2.0 * h - t : 0.0);
    }
    const double g = std::min (kLoopNaturalGlideSec * bpm / 60.0, 0.5 * len), back = len - g, fwd = std::min (d, back);
    if (t < fwd)
        return s + t;
    if (t < back)
        return s + fwd; // (held for less than a 16th of a beat)
    return s + fwd * (0.5 + 0.5 * std::cos (3.14159265358979323846 * (t - back) / g));
}

LoopRegion slideRegion (LoopRegion r, double delta)
{
    const double len = std::clamp (r.end - r.start, kLoopMinRegion, 1.0);
    const double s = std::clamp (r.start + delta, 0.0, 1.0 - len);
    return {s, s + len};
}

LoopRegion moveHandle (LoopRegion r, bool endHandle, double to)
{
    if (endHandle)
        r.end = std::clamp (to, r.start + kLoopMinRegion, 1.0);
    else
        r.start = std::clamp (to, 0.0, r.end - kLoopMinRegion);
    return r;
}

} // namespace moistr
