#include "Engine.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace multidyn {

namespace {
constexpr float kMaxBoostDb = 36.0f, kMaxCutDb = -80.0f;
inline bool on (double v) { return v >= 0.5; }
inline float dbToGain (double db) { return (float)std::pow (10.0, db / 20.0); }
inline float gainToDb (float g) { return 20.0f * std::log10 (std::max (g, 1e-6f)); }

double kneeGain (double amount, double slope, bool softKnee, double kneeDb)
{
    // amount: dB beyond the threshold (positive = beyond), slope: dB of gain per dB beyond
    if (!softKnee)
        return amount > 0.0 ? amount * slope : 0.0;
    const double h = kneeDb * 0.5;
    if (amount <= -h)
        return 0.0;
    if (amount >= h)
        return amount * slope;
    return slope * (amount + h) * (amount + h) / (2.0 * kneeDb);
}
} // namespace

ParamArray defaultParams ()
{
    ParamArray p {};
    for (uint32_t i = 0; i < kNumParams; ++i)
        p[i] = paramTable ().info (i).def;
    return p;
}

double aboveGainDb (double x, double thresh, double ratio, bool softKnee, double kneeDb)
{
    return kneeGain (x - thresh, 1.0 / std::max (0.01, ratio) - 1.0, softKnee, kneeDb);
}

double belowGainDb (double x, double thresh, double ratio, bool softKnee, double kneeDb)
{
    // Below the threshold the output slope is 1/ratio: ratio > 1 lifts quiet material (upward
    // compression), ratio < 1 pushes it down (downward expansion).
    return kneeGain (thresh - x, 1.0 - 1.0 / std::max (0.01, ratio), softKnee, kneeDb);
}

void Engine::prepare (double sampleRate, int)
{
    sr = sampleRate;
    // Level detection. Peak: instant attack, 30 ms release (holds the peak between cycles, so
    // the gain doesn't collapse at zero crossings). RMS: 20 ms mean-square window, slower to
    // react to short transients. Character: 50 ms RMS window, 60 ms peak release.
    rmsCoef = (float)std::exp (-1.0 / (0.020 * sr));
    rmsCoefC = (float)std::exp (-1.0 / (0.050 * sr));
    peakCoef = (float)std::exp (-1.0 / (0.030 * sr));
    peakCoefC = (float)std::exp (-1.0 / (0.060 * sr));
    meterFall = (float)std::exp (-1.0 / (0.300 * sr));    // meter decay
    smooth = (float)(1.0 - std::exp (-1.0 / (0.010 * sr))); // parameter smoothing
    // pre-limiter: the gain reaches most of its reduction over the look-ahead, releases in 50 ms
    look = std::clamp ((int)std::lround (0.001 * sr), 1, kMaxLookahead);
    limAtk = (float)std::exp (-4.0 / look);
    limPeakDecay = (float)std::exp (-1.0 / (2.0 * look)); // the input peak is held about twice the look-ahead
    limRel = (float)std::exp (-1.0 / (0.050 * sr));
    sat.setParam (smacheratr::kHiQuality, 1.0);
    sat.setParam (smacheratr::kColorOn, 0.0);
    sat.setParam (smacheratr::kDcFilter, 0.0);
    sat.setParam (smacheratr::kOutput, 0.0);
    sat.prepare (sr, 512);
    reset ();
}

void Engine::reset ()
{
    for (int j = 0; j < kMaxBands - 1; ++j)
    {
        split[j].reset ();
        scSplit[j].reset ();
        for (int b = 0; b < kMaxBands - 1; ++b)
        {
            ap[b][j].reset ();
            scAp[b][j].reset ();
        }
    }
    for (int b = 0; b < kNumBands; ++b)
    {
        bands[b] = BandState {};
        bands[b].inGain = dbToGain (p[bandParam (b, kBandInput)]);
        bands[b].outGain = dbToGain (p[bandParam (b, kBandOutput)]);
        meters[b] = BandMeter {};
    }
    outGain = dbToGain (p[kOutput]);
    scGain = dbToGain (p[kScGain]);
    syncSaturator ();
    sat.reset ();
    updateFilters (true);
}

void Engine::syncSaturator ()
{
    sat.setParam (smacheratr::kCurve, p[kSatCurve]);
    sat.setParam (smacheratr::kDrive, p[kSatDrive]);
    sat.setParam (smacheratr::kPostClip, p[kSatPostClip]);
    sat.setParam (smacheratr::kDryWet, on (p[kSatOn]) ? p[kSatMix] : 0.0);
}

int Engine::bandCount () const { return std::clamp ((int)std::lround (p[kBands]) + 1, 1, kMaxBands); }

void Engine::updateFilters (bool force)
{
    const int n = bandCount ();
    const float fsr = (float)sr;
    const float top = 0.45f * fsr;
    float prev = 0.0f;
    for (int j = 0; j < kMaxBands - 1; ++j)
    {
        float t = (float)p[kXover1 + j];
        if (j < n - 1 && j > 0)
            t = std::max (t, prev * 1.2f); // keep every band open and in order
        t = std::min (t, top / std::pow (1.2f, (float)(kMaxBands - 2 - j)));
        prev = t;
        // glide crossover changes in the log domain to avoid zipper noise
        const float next =
            force || xf[j] <= 0.0f ? t : std::exp (std::log (xf[j]) + 0.35f * (std::log (t) - std::log (xf[j])));
        if (force || std::fabs (next - xf[j]) > 1e-3f * xf[j])
        {
            xf[j] = next;
            split[j].setup (next, fsr);
            scSplit[j].setup (next, fsr);
            for (int b = 0; b < kMaxBands - 1; ++b)
            {
                ap[b][j].setup (next, fsr);
                scAp[b][j].setup (next, fsr);
            }
        }
    }
}

void Engine::splitBands (float x, int c, int n, Lr4Split* sp, Allpass2 (*aps)[kMaxBands - 1], float* out)
{
    float rest = x;
    for (int j = 0; j < n - 1; ++j)
    {
        float lo, hi;
        sp[j].tick (rest, c, lo, hi);
        out[j] = lo;
        rest = hi;
    }
    out[n - 1] = rest;
    for (int b = 0; b + 2 < n; ++b)
        for (int j = b + 1; j < n - 1; ++j)
            out[b] = aps[b][j].tick (out[b], c);
}

void Engine::process (const float* inL, const float* inR, const float* scL, const float* scR, float* outL,
                      float* outR, int n)
{
    updateFilters (false);
    const int nBands = bandCount ();
    const bool character = std::lround (p[kMode]) == kCharacter;
    const bool softKnee = on (p[kSoftKnee]);
    const double kneeDb = character ? kCharacterKneeDb : kKneeDb;
    const bool rms = std::lround (p[kDetector]) == kRms;
    const float rmsC = character ? rmsCoefC : rmsCoef, peakC = character ? peakCoefC : peakCoef;
    const bool preLimit = on (p[kPreLimit]);
    const double ceilingOffset = p[kPreLimitCeiling]; // dB above each band's Above threshold
    const double amount = std::clamp (p[kAmount], 0.0, 1.0);
    const double timeScale = std::max (0.01, p[kTime]);
    const bool scActive = on (p[kScOn]) && scL != nullptr;
    const float scMix = scActive ? (float)std::clamp (p[kScMix], 0.0, 1.0) : 0.0f;
    const bool listen = on (p[kScListen]) && scActive;
    const float outTarget = dbToGain (p[kOutput]), scTarget = dbToGain (p[kScGain]);
    syncSaturator ();

    bool used[kNumBands], active[kNumBands], audible[kNumBands];
    bool anySolo = false;
    for (int b = 0; b < kNumBands; ++b)
    {
        used[b] = bandUsed (b);
        active[b] = on (p[bandParam (b, kBandActive)]);
        anySolo |= used[b] && on (p[bandParam (b, kBandSolo)]);
    }
    float inTarget[kNumBands], outTargetB[kNumBands], atk[kNumBands], rel[kNumBands], relSlow[kNumBands], ceiling[kNumBands];
    double ta[kNumBands], ra[kNumBands], tb[kNumBands], rb[kNumBands];
    for (int b = 0; b < kNumBands; ++b)
    {
        audible[b] = used[b] && (!anySolo || on (p[bandParam (b, kBandSolo)]));
        inTarget[b] = dbToGain (p[bandParam (b, kBandInput)]);
        outTargetB[b] = dbToGain (p[bandParam (b, kBandOutput)]);
        // Attack/Release are the time to (almost) complete the change - about 95 % - as in
        // "time to reach maximum compression", so the envelope time constant is a third of it.
        const double attackMs = std::max (0.01, p[bandParam (b, kAttack)] * timeScale / 3.0);
        const double releaseMs = std::max (0.1, p[bandParam (b, kRelease)] * timeScale / 3.0);
        atk[b] = (float)std::exp (-1.0 / (attackMs * 0.001 * sr));
        rel[b] = (float)std::exp (-1.0 / (releaseMs * 0.001 * sr));
        relSlow[b] = (float)std::exp (-1.0 / (3.0 * releaseMs * 0.001 * sr)); // Character, deep gain changes
        ta[b] = p[bandParam (b, kAboveThresh)];
        ceiling[b] = dbToGain (ta[b] + ceilingOffset);
        ra[b] = p[bandParam (b, kAboveRatio)];
        tb[b] = p[bandParam (b, kBelowThresh)];
        rb[b] = p[bandParam (b, kBelowRatio)];
    }
    // skip the gain computers when nothing would happen (ratio 1:1 everywhere or amount 0)
    bool neutral[kNumBands];
    for (int b = 0; b < kNumBands; ++b)
        neutral[b] = amount <= 0.0 || (std::fabs (ra[b] - 1.0) < 1e-6 && std::fabs (rb[b] - 1.0) < 1e-6);

    for (int i = 0; i < n; ++i)
    {
        outGain += (outTarget - outGain) * smooth;
        scGain += (scTarget - scGain) * smooth;
        float xs[2] = {inL[i], inR[i]};
        float band[kNumBands][2] {};
        float scBand[kNumBands][2] {};
        for (int c = 0; c < 2; ++c)
        {
            float tmp[kMaxBands] {};
            splitBands (xs[c], c, nBands, split, ap, tmp);
            for (int bb = 0; bb < nBands; ++bb)
                band[bb][c] = tmp[bb];
            if (scActive)
            {
                const float sx = (c == 0 ? scL[i] : scR[i]) * scGain;
                splitBands (sx, c, nBands, scSplit, scAp, tmp);
                for (int bb = 0; bb < nBands; ++bb)
                    scBand[bb][c] = tmp[bb];
            }
        }

        float sumL = 0.0f, sumR = 0.0f;
        for (int b = 0; b < kNumBands; ++b)
        {
            if (!used[b])
                continue;
            BandState& st = bands[b];
            // the look-ahead delay (every band, always: the latency never changes)
            const float dl = st.delay[0][st.delayPos], dr = st.delay[1][st.delayPos];
            st.delay[0][st.delayPos] = band[b][0];
            st.delay[1][st.delayPos] = band[b][1];
            if (++st.delayPos >= look)
                st.delayPos = 0;
            float yl, yr;
            if (!active[b])
            {
                // deactivated: no dynamics and no band gains
                yl = dl;
                yr = dr;
                st.limGain = 1.0f;
                st.meterIn = std::max (std::max (std::fabs (yl), std::fabs (yr)), st.meterIn * meterFall);
                st.meterOut = st.meterIn;
            }
            else
            {
                st.inGain += (inTarget[b] - st.inGain) * smooth;
                st.outGain += (outTargetB[b] - st.outGain) * smooth;
                float xl = dl * st.inGain, xr = dr * st.inGain;
                if (preLimit)
                {
                    // the gain starts moving `look` samples before the peak reaches the output
                    const float ahead = std::max (std::fabs (band[b][0]), std::fabs (band[b][1])) * st.inGain;
                    st.limPeak = std::max (ahead, st.limPeak * limPeakDecay);
                    const float need = st.limPeak > ceiling[b] ? ceiling[b] / st.limPeak : 1.0f;
                    st.limGain = need < st.limGain ? need + limAtk * (st.limGain - need) : 1.0f + limRel * (st.limGain - 1.0f);
                    xl *= st.limGain;
                    xr *= st.limGain;
                }
                else
                    st.limGain = 1.0f;
                float g = 1.0f;
                if (!neutral[b])
                {
                    // detector: the band itself, the side-chain band, or a blend of both
                    const float dlv = xl * (1.0f - scMix) + scBand[b][0] * scMix;
                    const float drv = xr * (1.0f - scMix) + scBand[b][1] * scMix;
                    float level;
                    if (rms)
                    {
                        st.rms = rmsC * st.rms + (1.0f - rmsC) * 0.5f * (dlv * dlv + drv * drv);
                        level = std::sqrt (st.rms);
                    }
                    else
                    {
                        const float inst = std::max (std::fabs (dlv), std::fabs (drv));
                        st.peak = std::max (inst, st.peak * peakC);
                        level = st.peak;
                    }
                    const float lev = std::max (-120.0f, gainToDb (level));
                    // Character: the release slows down the deeper the current gain change
                    float relA = rel[b], relB = rel[b];
                    if (character)
                    {
                        const float deep = std::min (1.0f, std::fabs (st.aboveDb + st.belowDb) / 12.0f);
                        relA = relB = rel[b] + (relSlow[b] - rel[b]) * deep;
                    }
                    // Level-domain envelopes feed the static curves (so the audible timing
                    // depends on how far the level is past a threshold, as in the original):
                    // Above reacts with Attack to rising levels, Below with Attack to falling ones.
                    st.envAbove = lev + (lev > st.envAbove ? atk[b] : relA) * (st.envAbove - lev);
                    st.envBelow = lev + (lev < st.envBelow ? atk[b] : relB) * (st.envBelow - lev);
                    float eA = st.envAbove, eB = st.envBelow;
                    if (character)
                    {
                        // a second stage rounds the onset of the gain change
                        st.envAbove2 = eA + (eA > st.envAbove2 ? atk[b] : relA) * (st.envAbove2 - eA);
                        st.envBelow2 = eB + (eB < st.envBelow2 ? atk[b] : relB) * (st.envBelow2 - eB);
                        eA = st.envAbove2;
                        eB = st.envBelow2;
                    }
                    st.aboveDb = (float)(aboveGainDb (eA, ta[b], ra[b], softKnee, kneeDb) * amount);
                    st.belowDb = (float)(belowGainDb (eB, tb[b], rb[b], softKnee, kneeDb) * amount);
                    // Upward compression never lifts the signal past the Below threshold, even while
                    // its envelope is still releasing (e.g. a loud hit right after silence).
                    if (st.belowDb > 0.0f)
                        st.belowDb = std::min (st.belowDb, std::max (0.0f, (float)tb[b] - lev));
                    g = dbToGain (std::clamp (st.aboveDb + st.belowDb, kMaxCutDb, kMaxBoostDb));
                }
                else
                {
                    st.aboveDb *= rel[b];
                    st.belowDb *= rel[b];
                    g = dbToGain (st.aboveDb + st.belowDb);
                }
                yl = xl * g * st.outGain;
                yr = xr * g * st.outGain;
                st.meterIn = std::max (std::max (std::fabs (xl), std::fabs (xr)), st.meterIn * meterFall);
                st.meterOut = std::max (std::max (std::fabs (yl), std::fabs (yr)), st.meterOut * meterFall);
            }
            if (audible[b])
            {
                sumL += yl;
                sumR += yr;
            }
        }
        if (listen)
        {
            outL[i] = scL[i] * scGain;
            outR[i] = scR[i] * scGain;
        }
        else
        {
            outL[i] = sumL * outGain;
            outR[i] = sumR * outGain;
        }
    }

    // the built-in saturator after the Output gain (not on the side-chain listen signal)
    if (!listen)
        sat.process (outL, outR, outL, outR, n);

    for (int b = 0; b < kNumBands; ++b)
    {
        meters[b].inputDb = gainToDb (bands[b].meterIn);
        meters[b].outputDb = gainToDb (bands[b].meterOut);
        meters[b].gainDb = active[b] ? bands[b].aboveDb + bands[b].belowDb + gainToDb (bands[b].limGain) : 0.0f;
        if (!used[b])
            meters[b] = BandMeter {};
    }
}

} // namespace multidyn
