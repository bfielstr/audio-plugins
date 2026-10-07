#include "Engine.h"

#include "multidyn/src/core/Ott.h"

#include "pluginkit/NoDenormals.h"

#include <algorithm>
#include <cmath>

namespace smeezr {

namespace {
inline float dbToGain (double db) { return (float)std::pow (10.0, db / 20.0); }
inline double toDb (double ms) { return 10.0 * std::log10 (ms + 1e-30); }
constexpr double kEdgeLo = 20.0, kEdgeHi = 20000.0;
constexpr double kSilentMs = 1e-8;    // below -80 dBFS in total: the pink gains glide back to 0 dB
constexpr double kGateFull = -30.0;   // a band this far below the loudest still gets its full boost
constexpr double kGateNone = -40.0;   // and this far below, none
constexpr double kOttTimePercent = 100.0; // OTT's Time knob at its default
constexpr double kMinGainDb = -60.0, kMaxGainDb = 50.0;
// glides x towards t by c, landing on it once close (so a still setting is exactly its value)
inline void glide (double& x, double t, double c)
{
    x += (t - x) * c;
    if (std::fabs (t - x) < 1e-9)
        x = t;
}
inline double coef (int m, double sec, double sr) { return 1.0 - std::exp (-(double)m / (std::max (1e-4, sec) * sr)); }
} // namespace

ParamArray defaultParams ()
{
    ParamArray p {};
    for (uint32_t i = 0; i < kNumParams; ++i)
        p[i] = paramTable ().info (i).def;
    return p;
}

double Engine::pinkAmount (double u)
{
    const double t = std::clamp (2.0 * u, 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

double Engine::ottDepth (double u)
{
    const double t = std::clamp (2.0 * u - 1.0, 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

double Engine::crossover (int i) { return kEdgeLo * std::pow (kEdgeHi / kEdgeLo, (double)(i + 1) / kBands); }

double Engine::bandLevelDb (int b) const { return toDb (msSlow[b]); }

Engine::Engine () { setTimes (); }

void Engine::setTimes ()
{
    const double scale = std::lround (p[kSpeed]) == kSpeedSlow ? 4.0 : 1.0;
    for (int b = 0; b < kBands; ++b)
    {
        // 250 ms in the lowest band down to 60 ms in the highest (geometric), times the speed's scale
        rmsSec[b] = scale * 0.25 * std::pow (0.06 / 0.25, (double)b / (kBands - 1));
        atkSec[b] = 0.5 * rmsSec[b];
        relSec[b] = 1.5 * rmsSec[b];
        cRms[b] = coef (kTick, rmsSec[b], sr);
        cAtk[b] = coef (kTick, atkSec[b], sr);
        cRel[b] = coef (kTick, relSec[b], sr);
    }
    for (int k = 0; k < kGroups; ++k)
    {
        cOttAtk[k] = coef (kTick, multidyn::ott::kAttack[k], sr);
        cOttRel[k] = coef (kTick, multidyn::ott::releaseSec (k, kOttTimePercent), sr);
    }
}

void Engine::prepare (double sampleRate, int maxBlock)
{
    sr = sampleRate > 1000.0 ? sampleRate : 48000.0;
    for (int i = 0; i < kXovers; ++i)
        xc[i].set (crossover (i), sr);
    detLoC.set (kEdgeLo, sr);
    detHiC.set (kEdgeHi, sr);
    knobSmooth = coef (kTick, 0.03, sr);
    engageStep = (double)kTick / (0.02 * sr);
    smooth = (float)(1.0 - std::exp (-1.0 / (0.02 * sr)));
    tail.prepare (sr, std::max (1, maxBlock));
    for (uint32_t id = 0; id < kNumParams; ++id)
        setParam (id, p[id]);
    setTimes ();
    reset ();
}

void Engine::reset ()
{
    for (int i = 0; i < kXovers; ++i)
        for (int c = 0; c < 2; ++c)
        {
            split[i][c].reset ();
            ap[i][c].reset ();
        }
    for (int c = 0; c < 2; ++c)
    {
        detLo[c].reset ();
        detHi[c].reset ();
    }
    for (int b = 0; b < kBands; ++b)
    {
        msSlow[b] = 0.0;
        gPink[b] = 0.0;
        for (int j = 0; j < kBands; ++j)
            msAt[b][j] = 0.0;
    }
    for (int k = 0; k < kGroups; ++k)
    {
        env[k] = 0.0;
        gOtt[k] = 0.0;
    }
    knob = std::clamp (p[kSqueeze], 0.0, 1.0);
    mixNow = std::clamp (p[kMix], 0.0, 1.0);
    const double d = ottDepth (knob);
    for (int b = 0; b < kBands; ++b)
    {
        // (the OTT's makeup at the depth set: what an envelope at silence gives, as a start)
        const double g = d > 0.0 ? std::pow (10.0, multidyn::ott::makeupShape (group (b), d) * multidyn::ott::kMakeup[group (b)] / 20.0) : 1.0;
        gNow[b] = 1.0 + mixNow * (g - 1.0);
    }
    engage = knob > 0.0 ? 1.0 : 0.0;
    out = dbToGain (p[kOutput]);
    target = -120.0;
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
    else if (id == kSpeed)
        setTimes ();
    // (the rest are read where they are used)
}

// One tick's gains from the bands' mean squares over it (m samples): the pink stage, then the OTT stage on
// its result. gainOut: each band's linear gain with Mix in.
void Engine::tickGains (const double* ms, int m, double* gainOut)
{
    const bool full = m == kTick;
    // the knob glides, then its two stages
    glide (knob, std::clamp (p[kSqueeze], 0.0, 1.0), full ? knobSmooth : coef (m, 0.03, sr));
    if (p[kSqueeze] <= 0.0 && knob < 1e-5)
        knob = 0.0;
    const double pink = pinkAmount (knob), depth = ottDepth (knob);

    // the pink stage: the levels, the target and the gains towards it. Every band's power is also measured
    // over every other band's window (msAt[b][j]: band j over band b's), so a band is compared with the total
    // measured the way its own level is: a swell or a fade (the lows' windows lagging behind the highs')
    // does not read as a change of balance.
    double loudest = -300.0, level[kBands];
    for (int b = 0; b < kBands; ++b)
    {
        const double c = full ? cRms[b] : coef (m, rmsSec[b], sr);
        for (int j = 0; j < kBands; ++j)
        {
            msAt[b][j] += (ms[j] - msAt[b][j]) * c;
            if (msAt[b][j] < 1e-30)
                msAt[b][j] = 0.0;
        }
        msSlow[b] = msAt[b][b];
        level[b] = toDb (msSlow[b]);
        loudest = std::max (loudest, level[b]);
    }
    // the bands with something in them (a band 30 dB under the loudest counts fully, 40 dB under not at all):
    // the target is their equal share of the power, and only they may be boosted
    double allow[kBands], counted = 0.0;
    for (int b = 0; b < kBands; ++b)
    {
        allow[b] = std::clamp ((level[b] - loudest - kGateNone) / (kGateFull - kGateNone), 0.0, 1.0);
        counted += allow[b];
    }
    counted = std::max (1.0, counted);
    // (the loudness and the display: every band over the middle band's window)
    constexpr int kRef = kBands / 2;
    double total = 0.0;
    for (int j = 0; j < kBands; ++j)
        total += msAt[kRef][j];
    target = toDb (total / counted);
    double want[kBands] {};
    if (total > kSilentMs && pink > 0.0)
    {
        double after = 0.0;
        for (int b = 0; b < kBands; ++b)
        {
            double row = 0.0;
            for (int j = 0; j < kBands; ++j)
                row += msAt[b][j];
            want[b] = std::clamp (pink * (toDb (row / counted) - level[b]), -pink * kMaxCutDb, pink * maxBoostDb (b) * allow[b]);
        }
        for (int j = 0; j < kBands; ++j)
            after += msAt[kRef][j] * std::pow (10.0, want[j] / 10.0);
        // every gain shifted together so the total power stays (a raise only as far as the band may be boosted:
        // a gated band still never goes up)
        const double shift = std::clamp (toDb (total) - toDb (after), -kMaxCutDb, kMaxCutDb);
        for (int b = 0; b < kBands; ++b)
            want[b] += shift > 0.0 ? shift * allow[b] : shift;
    }
    for (int b = 0; b < kBands; ++b)
    {
        const bool down = want[b] < gPink[b];
        glide (gPink[b], want[b], full ? (down ? cAtk[b] : cRel[b]) : coef (m, down ? atkSec[b] : relSec[b], sr));
    }

    // the OTT stage: each group's power after the pink gains, enveloped, into OTT's gain law
    for (int k = 0; k < kGroups; ++k)
    {
        double x2 = 0.0;
        for (int b = 0; b < kBands; ++b)
            if (group (b) == k)
                x2 += ms[b] * std::pow (10.0, gPink[b] / 10.0);
        const bool up = x2 > env[k];
        const double c = full ? (up ? cOttAtk[k] : cOttRel[k])
                              : coef (m, up ? multidyn::ott::kAttack[k] : multidyn::ott::releaseSec (k, kOttTimePercent), sr);
        env[k] += (x2 - env[k]) * c;
        if (!(env[k] >= 1e-30) || !(env[k] < 1e30))
            env[k] = 0.0;
        gOtt[k] = depth > 0.0 ? multidyn::ott::gainDb (k, toDb (std::max (env[k], 1e-20)), depth, 1.0, 1.0, 0.0, 0.0) : 0.0;
    }

    glide (mixNow, std::clamp (p[kMix], 0.0, 1.0), full ? knobSmooth : coef (m, 0.03, sr));
    for (int b = 0; b < kBands; ++b)
    {
        const double db = std::clamp (gPink[b] + gOtt[group (b)], kMinGainDb, kMaxGainDb);
        gainOut[b] = 1.0 + mixNow * (std::pow (10.0, db / 20.0) - 1.0);
    }
}

void Engine::process (const float* xl, const float* xr, float* yl, float* yr, int n)
{
    const pk::NoDenormals guard;
    const float outT = dbToGain (p[kOutput]);
    const bool on = p[kSqueeze] > 0.0;

    float dryL[kTick], dryR[kTick];
    double band[kBands][2][kTick];
    for (int a = 0; a < n; a += kTick)
    {
        const int m = std::min (kTick, n - a);
        float peak = 0.0f;
        double ms[kBands] {};
        for (int i = 0; i < m; ++i)
        {
            dryL[i] = xl[a + i];
            dryR[i] = xr[a + i];
            peak = std::max (peak, std::max (std::fabs (dryL[i]), std::fabs (dryR[i])));
            for (int c = 0; c < 2; ++c)
            {
                double r = c == 0 ? dryL[i] : dryR[i];
                for (int s = 0; s < kXovers; ++s)
                {
                    double lo, hi;
                    split[s][c].tick (r, xc[s], lo, hi);
                    band[s][c][i] = lo;
                    r = hi;
                }
                band[kBands - 1][c][i] = r;
                // the detectors (the edge bands measured inside 20 Hz .. 20 kHz)
                for (int b = 1; b < kBands - 1; ++b)
                    ms[b] += band[b][c][i] * band[b][c][i];
                const double lo = detLo[c].tick (band[0][c][i], detLoC).hp;
                const double hi = detHi[c].tick (band[kBands - 1][c][i], detHiC).lp;
                ms[0] += lo * lo;
                ms[kBands - 1] += hi * hi;
            }
        }
        for (int b = 0; b < kBands; ++b)
            ms[b] *= 0.5 / m; // (the mean square of left and right)
        quiet = peak > 1e-6f ? 0 : std::min (quiet + m, (int)sr * 10);

        double g1[kBands];
        tickGains (ms, m, g1);
        const double e0 = engage;
        engage = std::clamp (engage + (on || knob > 0.0 ? engageStep : -engageStep), 0.0, 1.0);

        for (int i = 0; i < m; ++i)
        {
            const double t = (double)(i + 1) / m;
            double g[kBands];
            for (int b = 0; b < kBands; ++b)
                g[b] = gNow[b] + (g1[b] - gNow[b]) * t;
            // the bands summed, each lower one through the all-passes of the splits above it
            double wet[2];
            for (int c = 0; c < 2; ++c)
            {
                double acc = g[0] * band[0][c][i];
                for (int s = 1; s < kXovers; ++s)
                    acc = ap[s][c].tick (acc, xc[s]) + g[s] * band[s][c][i];
                wet[c] = acc + g[kBands - 1] * band[kBands - 1][c][i];
            }
            out += (outT - out) * smooth;
            if (std::fabs (outT - out) < 1e-6f)
                out = outT;
            const double eg = e0 + (engage - e0) * t;
            if (eg <= 0.0)
            {
                // untouched: the input itself (bit for bit at Output 0 dB)
                yl[a + i] = dryL[i] * out;
                yr[a + i] = dryR[i] * out;
            }
            else
            {
                const double l = std::clamp (dryL[i] + (wet[0] - dryL[i]) * eg, -64.0, 64.0);
                const double r = std::clamp (dryR[i] + (wet[1] - dryR[i]) * eg, -64.0, 64.0);
                yl[a + i] = (float)l * out;
                yr[a + i] = (float)r * out;
            }
        }
        for (int b = 0; b < kBands; ++b)
            gNow[b] = g1[b];
        // (never: a NaN or an overflow in the filters starts them again)
        bool ok = std::isfinite (yl[a + m - 1]) && std::isfinite (yr[a + m - 1]);
        for (int s = 0; ok && s < kXovers; ++s)
            ok = split[s][0].finite () && split[s][1].finite () && ap[s][0].finite () && ap[s][1].finite ();
        if (!ok)
            reset ();
    }

    if (meters)
    {
        constexpr auto rx = std::memory_order_relaxed;
        meters->active.store (quiet < (int)(0.5 * sr), rx);
        meters->pink.store ((float)pinkAmount (knob), rx);
        meters->ott.store ((float)ottDepth (knob), rx);
        meters->targetDb.store ((float)std::max (-120.0, target), rx);
        for (int b = 0; b < kBands; ++b)
        {
            meters->levelDb[(size_t)b].store ((float)std::max (-120.0, bandLevelDb (b)), rx);
            meters->pinkDb[(size_t)b].store ((float)gPink[b], rx);
            meters->ottDb[(size_t)b].store ((float)gOtt[group (b)], rx);
        }
        meters->blocks.fetch_add (1, std::memory_order_release);
    }
    tail.process (yl, yr, n);
}

} // namespace smeezr
