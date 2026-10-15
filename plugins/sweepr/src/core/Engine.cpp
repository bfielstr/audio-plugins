#include "Engine.h"

#include "pluginkit/NoDenormals.h"

#include <algorithm>
#include <cmath>

namespace sweepr {

namespace {
constexpr uint32_t kSeed[kBands] = {0x5EED0A11u, 0x5EED0B22u, 0x5EED0C33u};
inline double coefFor (double sec, double sr) { return 1.0 - std::exp (-1.0 / (std::max (1e-6, sec) * sr)); }
// glides x towards t by c, landing on it once close (so a still setting is exactly its value)
inline void glide (double& x, double t, double c, double eps = 1e-9)
{
    x += (t - x) * c;
    if (std::fabs (t - x) < eps)
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
    cMod = coefFor (0.002, sr);
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
    for (int b = 0; b < kBands; ++b)
    {
        Band& s = band[b];
        s.logF = std::log (p[bandParam (b, kFreq)]);
        s.logQ = std::log (p[bandParam (b, kQ)]);
        s.gainDb = p[bandParam (b, kGain)];
        s.modOct = 0.0;
        s.freeCycles = 0.0;
        s.freeIndex = 0;
        for (auto& f : s.svf)
            f.reset ();
        fNow[b] = p[bandParam (b, kFreq)];
        lfoVal[b] = 0.0;
    }
    for (auto& o : os)
        o.reset ();
    for (int c = 0; c < 2; ++c)
    {
        shelf[c].reset ();
        dc[c].reset ();
        lastZ[c] = tape::kBias;
        std::fill (dryBuf[c].begin (), dryBuf[c].end (), 0.0f);
    }
    env = 0.0;
    dryPos = 0;
    tapeAmt = p[kTapeOn] >= 0.5 ? 1.0 : 0.0;
    drive = p[kDrive];
    speed = p[kSpeed];
    comp = p[kCompression];
    mixNow = std::clamp (p[kMix], 0.0, 1.0);
    out = (float)std::pow (10.0, p[kOutput] / 20.0);
    shownSpeed = -1.0;
    tapeFilters ();
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

void Engine::setTransport (double tempo, double ppq, bool isPlaying)
{
    if (tempo > 1.0)
        bpm = tempo;
    playing = isPlaying;
    if (isPlaying)
        beatPos = ppq; // locked to the song: the same position always gives the same phase
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

// One chunk's controls, per input sample: the smoothed settings, the LFOs, the bells' coefficients and the
// tape's amounts.
void Engine::controlChunk (int m)
{
    const double beatStep = bpm / 60.0 / sr;
    const bool link = p[kLink] >= 0.5;
    const double fMax = 0.45 * sr;
    const double tapeT = p[kTapeOn] >= 0.5 ? 1.0 : 0.0;

    for (int i = 0; i < m; ++i)
    {
        double v[kBands];
        for (int b = 0; b < kBands; ++b)
        {
            const int lead = (b == 1 && link) ? 0 : b;
            const double phase = p[bandParam (b, kLfoPhase)] / 360.0;
            double cycles;
            if (p[bandParam (lead, kLfoSync)] >= 0.5)
                cycles = beatPos / divisionBeats ((int)std::lround (p[bandParam (lead, kLfoDiv)]));
            else
                cycles = (double)band[lead].freeIndex + band[lead].freeCycles;
            v[b] = dsp::lfoShape ((int)std::lround (p[bandParam (lead, kLfoShape)]), cycles + phase, kSeed[lead]);
        }
        for (int b = 0; b < kBands; ++b)
        {
            Band& s = band[b];
            glide (s.logF, std::log (p[bandParam (b, kFreq)]), cSmooth);
            glide (s.logQ, std::log (p[bandParam (b, kQ)]), cSmooth);
            glide (s.gainDb, p[bandParam (b, kGain)], cSmooth);
            const double depth = p[lfoOnParam (b)] >= 0.5 ? p[bandParam (b, kLfoDepth)] : 0.0;
            glide (s.modOct, depth * v[b], cMod, 1e-12);
            const double f = std::clamp (std::exp (s.logF + s.modOct * dsp::kLn2), 10.0, fMax);
            coef[b][i].bell (bell::prototype (f, std::exp (s.logQ), s.gainDb, sr), sr);
            if (i == m - 1)
            {
                fNow[b] = f;
                lfoVal[b] = v[b];
            }
            // the free LFO runs on
            s.freeCycles += p[bandParam (b, kLfoRate)] / sr;
            if (s.freeCycles >= 1.0)
            {
                const double w = std::floor (s.freeCycles);
                s.freeCycles -= w;
                s.freeIndex += (int64_t)w;
            }
        }
        beatPos += beatStep;

        // the tape
        if (tapeAmt != tapeT)
        {
            tapeAmt += tapeT > tapeAmt ? cFade : -cFade;
            if (tapeAmt >= 1.0 || tapeAmt <= 0.0)
                tapeAmt = tapeT;
        }
        glide (drive, p[kDrive], cSmooth);
        glide (comp, p[kCompression], cSmooth);
        glide (speed, p[kSpeed], cSmooth);
        tapeAt[i] = tapeAmt;
        gainAt[i] = std::pow (10.0, tape::driveDb (drive) / 20.0);
        compAt[i] = tape::kLevelAmount * comp;
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

    for (int a = 0; a < n; a += kChunk)
    {
        const int m = std::min (kChunk, n - a);
        float dryL[kChunk], dryR[kChunk];
        for (int i = 0; i < m; ++i)
        {
            dryL[i] = xl[a + i];
            dryR[i] = xr[a + i];
        }
        const bool tapeWas = tapeAmt > 0.0;
        controlChunk (m);
        const bool tape = tapeWas || tapeAmt > 0.0;
        if (!tape)
        {
            // fully off: the tape's state starts again from silence when it comes back
            env = 0.0;
            lastZ[0] = lastZ[1] = tape::kBias;
        }
        tapeFilters ();

        // the base rate: the bells (digital bells at the host's rate, as the EQ they are calibrated against)
        float belled[2][kChunk];
        for (int i = 0; i < m; ++i)
        {
            const float x[2] = {dryL[i], dryR[i]};
            for (int c = 0; c < 2; ++c)
            {
                double y = x[c];
                for (int b = 0; b < kBands; ++b)
                    y = band[b].svf[c].bell (y, coef[b][i]);
                belled[c][i] = (float)y;
            }
        }

        // 4x: the tape's curve
        os[0].up (belled[0], up[0].data (), m);
        os[1].up (belled[1], up[1].data (), m);
        float* u0 = up[0].data ();
        float* u1 = up[1].data ();
        for (int j = 0; j < m * kOversample; ++j)
        {
            const int i = j / kOversample;
            double x[2] = {u0[j], u1[j]};
            if (tape)
            {
                // Drive, then the leveller, linked (the louder channel sets the gain of both)
                const double lvl = gainAt[i] * std::max (std::fabs (x[0]), std::fabs (x[1]));
                env += (lvl - env) * (lvl > env ? cAtk : cRel);
                const double g = gainAt[i] / (1.0 + compAt[i] * env);
                for (int c = 0; c < 2; ++c)
                {
                    // the curve (anti-aliased), its bias taken out again
                    const double z = g * x[c] + tape::kBias;
                    const double y = tape::curveAa (z, lastZ[c]) - curveRest;
                    lastZ[c] = z;
                    x[c] += (y - x[c]) * tapeAt[i];
                }
            }
            u0[j] = (float)x[0];
            u1[j] = (float)x[1];
        }
        os[0].down (u0, wet[0].data (), m);
        os[1].down (u1, wet[1].data (), m);

        // the base rate: the tape's DC blocker and shelf (Speed), Mix and Output
        for (int i = 0; i < m; ++i)
        {
            float w[2];
            for (int c = 0; c < 2; ++c)
            {
                double s = wet[c][i];
                if (tape)
                {
                    double f = s - dc[c].lp (s, dcG);
                    double lp;
                    const double bp = shelf[c].tick (f, shelfC, lp);
                    f = shelfHigh * (f - shelfC.k * bp - lp) + shelfMid * bp + lp;
                    s += (f - s) * tapeAt[i];
                }
                w[c] = (float)s;
            }
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
                l = (float)std::clamp (dl + (w[0] - dl) * mixNow, -64.0, 64.0);
                r = (float)std::clamp (dr + (w[1] - dr) * mixNow, -64.0, 64.0);
            }
            yl[a + i] = l * out;
            yr[a + i] = r * out;
        }
        // (never: a NaN or an overflow in the filters starts them again)
        bool ok = std::isfinite (yl[a + m - 1]) && std::isfinite (yr[a + m - 1]) && std::isfinite (env);
        for (int b = 0; ok && b < kBands; ++b)
            ok = band[b].svf[0].finite () && band[b].svf[1].finite ();
        for (int c = 0; ok && c < 2; ++c)
            ok = shelf[c].finite ();
        if (!ok)
        {
            reset ();
            for (int i = 0; i < m; ++i)
                yl[a + i] = yr[a + i] = 0.0f;
        }
    }

    if (meters)
    {
        constexpr auto rx = std::memory_order_relaxed;
        meters->sampleRate.store ((float)sr, rx);
        for (int b = 0; b < kBands; ++b)
        {
            meters->freqHz[(size_t)b].store ((float)fNow[b], rx);
            meters->lfo[(size_t)b].store ((float)lfoVal[b], rx);
        }
        meters->blocks.fetch_add (1, std::memory_order_release);
    }
    tail.process (yl, yr, n);
}

} // namespace sweepr
