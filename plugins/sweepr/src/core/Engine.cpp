#include "Engine.h"

#include "pluginkit/NoDenormals.h"

#include <algorithm>
#include <cmath>

namespace sweepr {

namespace {
constexpr double kEmphHz = 3000.0;    // the pre-emphasis shelf's corner (+6 dB above it)
constexpr double kHystHz = 5000.0;    // how fast the magnetisation follows the curve at rest
constexpr double kHystSlope = 2.0;    // and how much faster it follows a moving field
constexpr double kRefAmp = 0.18;      // the makeup's reference: a sine this high (about -15 dBFS, -18 dBFS RMS) comes out as loud
constexpr uint32_t kSeed[kBands] = {0x5EED0A11u, 0x5EED0B22u};
inline double coefFor (double sec, double sr) { return 1.0 - std::exp (-1.0 / (std::max (1e-6, sec) * sr)); }
// glides x towards t by c, landing on it once close (so a still setting is exactly its value)
inline void glide (double& x, double t, double c, double eps = 1e-9)
{
    x += (t - x) * c;
    if (std::fabs (t - x) < eps)
        x = t;
}
inline float hermite (float xm1, float x0, float x1, float x2, float f)
{
    const float c1 = 0.5f * (x1 - xm1);
    const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
    const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
    return ((c3 * f + c2) * f + c1) * f + x0;
}
} // namespace

ParamArray defaultParams ()
{
    ParamArray p {};
    for (uint32_t i = 0; i < kNumParams; ++i)
        p[i] = paramTable ().info (i).def;
    return p;
}

double Engine::tapeMakeup (double d, double c)
{
    // what the curve does to the reference sine (after the compressor's steady gain for it): its RMS, DC
    // removed (the DC blocker takes it out), against the sine's
    const double g = std::pow (10.0, driveDb (d) / 20.0) / std::sqrt (1.0 + compAmount (c) * kRefAmp);
    const double bias = biasOf (d), tb = std::tanh (bias);
    constexpr int kN = 64;
    double sum = 0.0, sum2 = 0.0;
    for (int k = 0; k < kN; ++k)
    {
        const double y = dsp::tapeCurve (g * kRefAmp * std::sin (2.0 * dsp::kPi * (k + 0.5) / kN), bias, tb);
        sum += y;
        sum2 += y * y;
    }
    const double mean = sum / kN;
    const double rmsOut = std::sqrt (std::max (1e-12, sum2 / kN - mean * mean));
    return (kRefAmp / std::sqrt (2.0)) / rmsOut;
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
    // the wow delay: its centre far enough from 0 for the deepest wow and the interpolation
    wowCentre = (int)std::ceil (0.0006 * sr) + 2;
    wowSize = 1;
    while (wowSize < 2 * wowCentre + 8)
        wowSize *= 2;
    drySize = 1;
    while (drySize < osLatency + wowCentre + 2)
        drySize *= 2;
    for (int c = 0; c < 2; ++c)
    {
        wowBuf[c].assign ((size_t)wowSize, 0.0f);
        dryBuf[c].assign ((size_t)drySize, 0.0f);
    }
    cSmooth = coefFor (0.02, sr);
    cMod = coefFor (0.002, sr);
    cFade = 1.0 / (0.01 * sr); // (a linear fade: 10 ms)
    cAtk = coefFor (0.002, srOs);
    cRel = coefFor (0.1, srOs);
    cHyst = coefFor (1.0 / (2.0 * dsp::kPi * kHystHz), srOs);
    emphG = dsp::onePoleG (kEmphHz, srOs);
    deemphG = dsp::onePoleG (0.5 * kEmphHz, srOs);
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
        emph[c].reset ();
        deemph[c].reset ();
        bump[c].reset ();
        dip[c].reset ();
        hf[c].reset ();
        dc[c].reset ();
        mag[c] = lastH[c] = 0.0;
        std::fill (wowBuf[c].begin (), wowBuf[c].end (), 0.0f);
        std::fill (dryBuf[c].begin (), dryBuf[c].end (), 0.0f);
    }
    env = 0.0;
    wowPos = dryPos = 0;
    wowPh = driftPh = flutterPh = 0.0;
    tapeAmt = p[kTapeOn] >= 0.5 ? 1.0 : 0.0;
    drive = p[kDrive];
    speed = p[kSpeed];
    comp = p[kCompression];
    mixNow = std::clamp (p[kMix], 0.0, 1.0);
    out = (float)std::pow (10.0, p[kOutput] / 20.0);
    makeupNow = tapeMakeup (drive, comp);
    makeupFor[0] = drive;
    makeupFor[1] = comp;
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
    const double f = bumpHz (speed), db = bumpDb (speed);
    bumpC.cookbookBell (f, sr, 1.0, db);
    dipC.cookbookBell (2.6 * f, sr, 1.4, -0.4 * db);
    lpC.lowpass (std::min (rollOffHz (speed), 0.45 * sr), sr, 0.6);
}

// One chunk's controls, per input sample: the smoothed settings, the LFOs, the bells' coefficients and the
// tape's amounts.
void Engine::controlChunk (int m)
{
    const double beatStep = bpm / 60.0 / sr;
    const bool link = p[kLink] >= 0.5;
    const double fMax = 0.45 * sr;
    // the makeup at the end of the chunk's glide (the settings move slowly: interpolated across it)
    const double tapeT = p[kTapeOn] >= 0.5 ? 1.0 : 0.0;
    double dEnd = drive, cEnd = comp;
    for (int i = 0; i < m; ++i)
    {
        dEnd += (p[kDrive] - dEnd) * cSmooth;
        cEnd += (p[kCompression] - cEnd) * cSmooth;
    }
    double mkEnd = makeupNow;
    if (std::fabs (dEnd - makeupFor[0]) > 1e-7 || std::fabs (cEnd - makeupFor[1]) > 1e-7)
    {
        mkEnd = tapeMakeup (dEnd, cEnd);
        makeupFor[0] = dEnd;
        makeupFor[1] = cEnd;
    }
    const double mk0 = makeupNow;

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
            glide (s.modOct, p[bandParam (b, kLfoDepth)] * v[b], cMod, 1e-12);
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
        gainAt[i] = std::pow (10.0, driveDb (drive) / 20.0);
        biasAt[i] = biasOf (drive);
        tanhBiasAt[i] = std::tanh (biasAt[i]);
        compAt[i] = compAmount (comp);
        holdAt[i] = 0.25 * drive;
        makeupAt[i] = mk0 + (mkEnd - mk0) * (double)(i + 1) / m;
    }
    makeupNow = mkEnd;
}

void Engine::process (const float* xl, const float* xr, float* yl, float* yr, int n)
{
    const pk::NoDenormals guard;
    const float outT = (float)std::pow (10.0, p[kOutput] / 20.0);
    const float smoothOut = (float)cSmooth;
    const double cMix = cSmooth;
    const int dryDelay = osLatency + wowCentre;
    const double wowDepth = 0.0005 * sr, flutterDepth = 0.000015 * sr;
    const double wowStep = 2.0 * dsp::kPi * 0.55 / sr, driftStep = 2.0 * dsp::kPi * 0.13 / sr,
                 flutterStep = 2.0 * dsp::kPi * 6.5 / sr;

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
            for (int c = 0; c < 2; ++c)
            {
                emph[c].reset ();
                deemph[c].reset ();
                mag[c] = lastH[c] = 0.0;
            }
        }
        tapeFilters ();

        // the base rate: the bells (digital bells at the host's rate, as the EQ they are calibrated against)
        float belled[2][kChunk];
        for (int i = 0; i < m; ++i)
        {
            const float x[2] = {dryL[i], dryR[i]};
            for (int c = 0; c < 2; ++c)
                belled[c][i] = (float)band[1].svf[c].bell (band[0].svf[c].bell (x[c], coef[0][i]), coef[1][i]);
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
                // the compressor, linked: the louder channel sets the gain of both
                const double lvl = std::max (std::fabs (x[0]), std::fabs (x[1]));
                env += (lvl - env) * (lvl > env ? cAtk : cRel);
                const double gc = 1.0 / std::sqrt (1.0 + compAt[i] * env);
                for (int c = 0; c < 2; ++c)
                {
                    const double lp = emph[c].lp (x[c], emphG);
                    const double h = gainAt[i] * gc * (lp + 2.0 * (x[c] - lp));
                    const double man = dsp::tapeCurveAa (h, lastH[c], biasAt[i], tanhBiasAt[i]);
                    // the magnetisation follows the curve, faster the faster the field moves: a loop
                    const double rate = std::min (1.0, cHyst + kHystSlope * std::fabs (h - lastH[c]));
                    lastH[c] = h;
                    mag[c] += (man - mag[c]) * rate;
                    const double y = man + holdAt[i] * (mag[c] - man);
                    const double dl = deemph[c].lp (y, deemphG);
                    const double t = (dl + 0.5 * (y - dl)) * makeupAt[i];
                    x[c] += (t - x[c]) * tapeAt[i];
                }
            }
            u0[j] = (float)x[0];
            u1[j] = (float)x[1];
        }
        os[0].down (u0, wet[0].data (), m);
        os[1].down (u1, wet[1].data (), m);

        // the base rate: the tape's filters, wow, Mix and Output
        const double wowAmt = wowAmount (speed);
        for (int i = 0; i < m; ++i)
        {
            float w[2];
            for (int c = 0; c < 2; ++c)
            {
                double s = wet[c][i];
                if (tape)
                {
                    double f = s - dc[c].lp (s, dcG);
                    f = bump[c].bell (f, bumpC);
                    f = dip[c].bell (f, dipC);
                    f = hf[c].lowpass (f, lpC);
                    s += (f - s) * tapeAt[i];
                }
                wowBuf[c][(size_t)wowPos] = (float)s;
            }
            // wow and flutter: the delay moves around its centre (exactly the centre with none)
            const double mod = tapeAt[i] * wowAmt *
                               (wowDepth * (0.7 * std::sin (wowPh) + 0.3 * std::sin (driftPh)) + flutterDepth * std::sin (flutterPh));
            wowPh += wowStep;
            driftPh += driftStep;
            flutterPh += flutterStep;
            if (wowPh > 2.0 * dsp::kPi)
                wowPh -= 2.0 * dsp::kPi;
            if (driftPh > 2.0 * dsp::kPi)
                driftPh -= 2.0 * dsp::kPi;
            if (flutterPh > 2.0 * dsp::kPi)
                flutterPh -= 2.0 * dsp::kPi;
            const double d = (double)wowCentre + mod;
            const int k0 = (int)std::floor (d);
            const float fr = (float)(d - k0);
            const int mask = wowSize - 1;
            for (int c = 0; c < 2; ++c)
            {
                const float* buf = wowBuf[c].data ();
                if (fr == 0.0f)
                    w[c] = buf[(wowPos - k0) & mask];
                else
                    w[c] = hermite (buf[(wowPos - k0 + 1) & mask], buf[(wowPos - k0) & mask], buf[(wowPos - k0 - 1) & mask],
                                    buf[(wowPos - k0 - 2) & mask], fr);
            }
            wowPos = (wowPos + 1) & mask;

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
            ok = std::isfinite (mag[c]) && bump[c].finite () && hf[c].finite ();
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
