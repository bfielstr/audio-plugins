#include "Engine.h"

#include "pluginkit/NoDenormals.h"

#include <algorithm>
#include <cmath>

namespace tapr {

namespace dsp = sweepr::dsp;
namespace tape = sweepr::tape;

namespace {
inline double coefFor (double sec, double sr) { return 1.0 - std::exp (-1.0 / (std::max (1e-6, sec) * sr)); }
// glides x towards t by c, landing on it once close (so a still setting is exactly its value)
inline void glide (double& x, double t, double c, double eps = 1e-9)
{
    x += (t - x) * c;
    if (std::fabs (t - x) < eps)
        x = t;
}
inline void fade (double& amt, double target, double step)
{
    if (amt != target)
    {
        amt += target > amt ? step : -step;
        if (amt >= 1.0 || amt <= 0.0)
            amt = target;
    }
}
} // namespace

ParamArray defaultParams ()
{
    ParamArray p {};
    for (uint32_t i = 0; i < kNumParams; ++i)
        p[i] = paramTable ().info (i).def;
    return p;
}

Engine::Engine () {}

void Engine::prepare (double sampleRate, int maxBlock)
{
    sr = sampleRate > 1000.0 ? sampleRate : 48000.0;
    srOs = sr * kOversample;
    for (auto& o : os)
        o.prepare (sr, kChunk);
    osLatency = os[0].latency ();
    for (int c = 0; c < 2; ++c)
    {
        up[c].assign ((size_t)(kChunk * kOversample), 0.0f);
        wet[c].assign ((size_t)kChunk, 0.0f);
    }
    drySize = 1;
    while (drySize < osLatency + 2)
        drySize *= 2;
    for (auto& d : dryBuf)
        d.assign ((size_t)drySize, 0.0f);
    cSmooth = coefFor (0.02, sr);
    cFade = 1.0 / (0.01 * sr); // (a linear fade: 10 ms)
    cAtk = coefFor (tape::kLevelAttack, srOs);
    cRel = coefFor (tape::kLevelRelease, srOs);
    dcG = dsp::onePoleG (10.0, sr);
    tail.prepare (sr, std::max (1, maxBlock));
    for (uint32_t id = 0; id < kNumParams; ++id)
        setParam (id, p[id]);
    reset ();
}

void Engine::reset ()
{
    for (auto& o : os)
        o.reset ();
    for (int c = 0; c < 2; ++c)
    {
        shelf[c].reset ();
        dc[c].reset ();
        lastZ[c] = tape::kBias;
        for (auto& b : cutF)
            b[c].reset ();
        std::fill (dryBuf[c].begin (), dryBuf[c].end (), 0.0f);
    }
    env = 0.0;
    levelNow = 1.0;
    peakNow = 0.0;
    dryPos = 0;
    tapeAmt = p[kTapeOn] >= 0.5 ? 1.0 : 0.0;
    cutAmt = p[kCutOn] >= 0.5 ? 1.0 : 0.0;
    drive = p[kDrive];
    speed = p[kSpeed];
    comp = p[kCompression];
    for (int b = 0; b < cut::kBands; ++b)
    {
        cutGain[b] = p[cutGainParam (b)];
        cutLogF[b] = std::log (p[cutFreqParam (b)]);
    }
    mixNow = std::clamp (p[kMix], 0.0, 1.0);
    out = (float)std::pow (10.0, p[kOutput] / 20.0);
    shownSpeed = -1.0;
    tapeFilters ();
    for (int b = 0; b < cut::kBands; ++b)
        cutC[b].bell (cut::prototype (std::exp (cutLogF[b]), cutGain[b], sr), sr);
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
    // (the rest are read where they are used)
}

void Engine::tapeFilters ()
{
    if (std::fabs (speed - shownSpeed) < 1e-4)
        return;
    shownSpeed = speed;
    // the high shelf: an SVF (k the denominator's s term), its outputs mixed as the numerator
    const tape::Shelf sh = tape::shelfAt (speed);
    shelfC.k = sh.a1;
    shelfC.set (std::tan (dsp::kPi * std::min (sh.corner, 0.45 * sr) / sr));
    shelfHigh = std::pow (10.0, sh.gainDb / 20.0);
    shelfMid = sh.b1;
}

// One chunk's controls, per input sample: the tape's and the Cut's amounts and the tape's gains; the Cut's
// bells and the Speed shelf once a chunk.
void Engine::controlChunk (int m)
{
    const double tapeT = p[kTapeOn] >= 0.5 ? 1.0 : 0.0, cutT = p[kCutOn] >= 0.5 ? 1.0 : 0.0;
    for (int i = 0; i < m; ++i)
    {
        fade (tapeAmt, tapeT, cFade);
        fade (cutAmt, cutT, cFade);
        glide (drive, p[kDrive], cSmooth);
        glide (comp, p[kCompression], cSmooth);
        glide (speed, p[kSpeed], cSmooth);
        for (int b = 0; b < cut::kBands; ++b)
        {
            glide (cutGain[b], p[cutGainParam (b)], cSmooth);
            glide (cutLogF[b], std::log (p[cutFreqParam (b)]), cSmooth);
        }
        tapeAt[i] = tapeAmt;
        cutAt[i] = cutAmt;
        gainAt[i] = std::pow (10.0, tape::driveDb (drive) / 20.0);
        compAt[i] = tape::kLevelAmount * comp;
    }
    for (int b = 0; b < cut::kBands; ++b)
        cutC[b].bell (cut::prototype (std::exp (cutLogF[b]), cutGain[b], sr), sr);
    tapeFilters ();
}

// The Cut EQ on channel c of a chunk, in place, faded in by its amount.
void Engine::cutBells (float* x, int c, int m)
{
    for (int i = 0; i < m; ++i)
    {
        double y = x[i];
        for (int b = 0; b < cut::kBands; ++b)
            y = cutF[b][c].bell (y, cutC[b]);
        x[i] = (float)(x[i] + (y - x[i]) * cutAt[i]);
    }
}

void Engine::process (const float* xl, const float* xr, float* yl, float* yr, int n)
{
    const pk::NoDenormals guard;
    const float outT = (float)std::pow (10.0, p[kOutput] / 20.0);
    const float smoothOut = (float)cSmooth;
    const double cMix = cSmooth;
    const int dryDelay = osLatency;
    const double curveRest = tape::curve (tape::kBias);
    double blockPeak = 0.0;

    for (int a = 0; a < n; a += kChunk)
    {
        const int m = std::min (kChunk, n - a);
        float dryL[kChunk], dryR[kChunk];
        for (int i = 0; i < m; ++i)
        {
            dryL[i] = xl[a + i];
            dryR[i] = xr[a + i];
        }
        const bool tapeWas = tapeAmt > 0.0, cutWas = cutAmt > 0.0;
        controlChunk (m);
        const bool tapeOn = tapeWas || tapeAmt > 0.0, cutOn = cutWas || cutAmt > 0.0;
        const bool levelPost = p[kCompPlace] >= 0.5, cutPost = p[kCutPlace] >= 0.5;
        if (!tapeOn)
        {
            // fully off: the tape's state starts again from silence when it comes back
            env = 0.0;
            levelNow = 1.0;
            lastZ[0] = lastZ[1] = tape::kBias;
        }

        // the base rate: the Cut EQ before the tape (Pre)
        float in[2][kChunk];
        std::copy (dryL, dryL + m, in[0]);
        std::copy (dryR, dryR + m, in[1]);
        if (cutOn && !cutPost)
            for (int c = 0; c < 2; ++c)
                cutBells (in[c], c, m);

        // 4x: Drive, the leveller and the curve
        os[0].up (in[0], up[0].data (), m);
        os[1].up (in[1], up[1].data (), m);
        float* u0 = up[0].data ();
        float* u1 = up[1].data ();
        double peak = 0.0;
        for (int j = 0; j < m * kOversample; ++j)
        {
            const int i = j / kOversample;
            double x[2] = {u0[j], u1[j]};
            if (tapeOn)
            {
                double z[2] = {gainAt[i] * x[0], gainAt[i] * x[1]};
                if (!levelPost)
                {
                    // Pre (Scream's): the leveller, linked (the louder channel sets the gain of both), into the curve
                    const double lvl = std::max (std::fabs (z[0]), std::fabs (z[1]));
                    env += (lvl - env) * (lvl > env ? cAtk : cRel);
                    levelNow = 1.0 / (1.0 + compAt[i] * env);
                    z[0] *= levelNow;
                    z[1] *= levelNow;
                }
                double y[2];
                for (int c = 0; c < 2; ++c)
                {
                    // the curve (anti-aliased), its bias taken out again
                    const double zb = z[c] + tape::kBias;
                    peak = std::max (peak, std::fabs (z[c]));
                    y[c] = tape::curveAa (zb, lastZ[c]) - curveRest;
                    lastZ[c] = zb;
                }
                if (levelPost)
                {
                    // Post: the leveller on what the curve made
                    const double lvl = std::max (std::fabs (y[0]), std::fabs (y[1]));
                    env += (lvl - env) * (lvl > env ? cAtk : cRel);
                    levelNow = 1.0 / (1.0 + compAt[i] * env);
                    y[0] *= levelNow;
                    y[1] *= levelNow;
                }
                for (int c = 0; c < 2; ++c)
                    x[c] += (y[c] - x[c]) * tapeAt[i];
            }
            u0[j] = (float)x[0];
            u1[j] = (float)x[1];
        }
        blockPeak = std::max (blockPeak, peak);
        os[0].down (u0, wet[0].data (), m);
        os[1].down (u1, wet[1].data (), m);

        // the base rate: the tape's DC blocker and shelf (Speed), the Cut EQ after the tape (Post)
        for (int c = 0; c < 2; ++c)
        {
            float* w = wet[c].data ();
            if (tapeOn)
                for (int i = 0; i < m; ++i)
                {
                    const double s = w[i];
                    double f = s - dc[c].lp (s, dcG);
                    double lp;
                    const double bp = shelf[c].tick (f, shelfC, lp);
                    f = shelfHigh * (f - shelfC.k * bp - lp) + shelfMid * bp + lp;
                    w[i] = (float)(s + (f - s) * tapeAt[i]);
                }
            if (cutOn && cutPost)
                cutBells (w, c, m);
        }

        // Mix and Output
        for (int i = 0; i < m; ++i)
        {
            // the dry signal, as late as the wet one
            const int dmask = drySize - 1;
            dryBuf[0][(size_t)dryPos] = dryL[i];
            dryBuf[1][(size_t)dryPos] = dryR[i];
            const float dl = dryBuf[0][(size_t)((dryPos - dryDelay) & dmask)], dr = dryBuf[1][(size_t)((dryPos - dryDelay) & dmask)];
            dryPos = (dryPos + 1) & dmask;

            glide (mixNow, std::clamp (p[kMix], 0.0, 1.0), cMix);
            out += (outT - out) * smoothOut;
            if (std::fabs (outT - out) < 1e-6f)
                out = outT;
            float l = dl, r = dr;
            if (mixNow > 0.0)
            {
                l = (float)std::clamp (dl + (wet[0][(size_t)i] - dl) * mixNow, -64.0, 64.0);
                r = (float)std::clamp (dr + (wet[1][(size_t)i] - dr) * mixNow, -64.0, 64.0);
            }
            yl[a + i] = l * out;
            yr[a + i] = r * out;
        }
        // (never: a NaN or an overflow in the filters starts them again)
        bool ok = std::isfinite (yl[a + m - 1]) && std::isfinite (yr[a + m - 1]) && std::isfinite (env);
        for (int c = 0; ok && c < 2; ++c)
        {
            ok = shelf[c].finite ();
            for (int b = 0; ok && b < cut::kBands; ++b)
                ok = cutF[b][c].finite ();
        }
        if (!ok)
        {
            reset ();
            for (int i = 0; i < m; ++i)
                yl[a + i] = yr[a + i] = 0.0f;
        }
    }

    peakNow = blockPeak;
    if (meters)
    {
        constexpr auto rx = std::memory_order_relaxed;
        meters->sampleRate.store ((float)sr, rx);
        meters->levellerDb.store ((float)(20.0 * std::log10 (std::max (1e-6, levelNow))), rx);
        meters->curvePeak.store ((float)peakNow, rx);
        meters->blocks.fetch_add (1, std::memory_order_release);
    }
    tail.process (yl, yr, n);
}

} // namespace tapr
