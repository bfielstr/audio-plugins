#include "Engine.h"

#include <algorithm>
#include <cmath>

namespace multidyn {

namespace {
constexpr double kKneeDb = 6.0;
constexpr float kMaxBoostDb = 36.0f, kMaxCutDb = -80.0f;
inline bool on (double v) { return v >= 0.5; }
inline float dbToGain (double db) { return (float)std::pow (10.0, db / 20.0); }
inline float gainToDb (float g) { return 20.0f * std::log10 (std::max (g, 1e-6f)); }

double kneeGain (double amount, double slope, bool softKnee)
{
    // amount: dB beyond the threshold (positive = beyond), slope: dB of gain per dB beyond
    if (!softKnee)
        return amount > 0.0 ? amount * slope : 0.0;
    const double h = kKneeDb * 0.5;
    if (amount <= -h)
        return 0.0;
    if (amount >= h)
        return amount * slope;
    return slope * (amount + h) * (amount + h) / (2.0 * kKneeDb);
}
} // namespace

ParamArray defaultParams ()
{
    ParamArray p {};
    for (uint32_t i = 0; i < kNumParams; ++i)
        p[i] = paramTable ().info (i).def;
    return p;
}

double aboveGainDb (double x, double thresh, double ratio, bool softKnee)
{
    return kneeGain (x - thresh, 1.0 / std::max (0.01, ratio) - 1.0, softKnee);
}

double belowGainDb (double x, double thresh, double ratio, bool softKnee)
{
    return kneeGain (thresh - x, 1.0 - ratio, softKnee);
}

void Engine::prepare (double sampleRate, int)
{
    sr = sampleRate;
    // Level detection. Peak: instant attack, 30 ms release (holds the peak between cycles, so
    // the gain doesn't collapse at zero crossings). RMS: 30 ms mean-square window, slower to
    // react to short transients.
    rmsCoef = (float)std::exp (-1.0 / (0.030 * sr));
    peakCoef = (float)std::exp (-1.0 / (0.030 * sr));
    meterFall = (float)std::exp (-1.0 / (0.300 * sr));    // meter decay
    smooth = (float)(1.0 - std::exp (-1.0 / (0.010 * sr))); // parameter smoothing
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
    updateFilters (true);
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
    const bool softKnee = on (p[kSoftKnee]);
    const bool rms = std::lround (p[kDetector]) == kRms;
    const double amount = std::clamp (p[kAmount], 0.0, 1.0);
    const double timeScale = std::max (0.01, p[kTime]);
    const bool scActive = on (p[kScOn]) && scL != nullptr;
    const float scMix = scActive ? (float)std::clamp (p[kScMix], 0.0, 1.0) : 0.0f;
    const bool listen = on (p[kScListen]) && scActive;
    const float outTarget = dbToGain (p[kOutput]), scTarget = dbToGain (p[kScGain]);

    bool used[kNumBands], active[kNumBands], audible[kNumBands];
    bool anySolo = false;
    for (int b = 0; b < kNumBands; ++b)
    {
        used[b] = bandUsed (b);
        active[b] = on (p[bandParam (b, kBandActive)]);
        anySolo |= used[b] && on (p[bandParam (b, kBandSolo)]);
    }
    float inTarget[kNumBands], outTargetB[kNumBands], atk[kNumBands], rel[kNumBands];
    double ta[kNumBands], ra[kNumBands], tb[kNumBands], rb[kNumBands];
    for (int b = 0; b < kNumBands; ++b)
    {
        audible[b] = used[b] && (!anySolo || on (p[bandParam (b, kBandSolo)]));
        inTarget[b] = dbToGain (p[bandParam (b, kBandInput)]);
        outTargetB[b] = dbToGain (p[bandParam (b, kBandOutput)]);
        const double attackMs = std::max (0.01, p[bandParam (b, kAttack)] * timeScale);
        const double releaseMs = std::max (0.1, p[bandParam (b, kRelease)] * timeScale);
        atk[b] = (float)std::exp (-1.0 / (attackMs * 0.001 * sr));
        rel[b] = (float)std::exp (-1.0 / (releaseMs * 0.001 * sr));
        ta[b] = p[bandParam (b, kAboveThresh)];
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
            float yl, yr;
            if (!active[b])
            {
                // deactivated: no dynamics and no band gains
                yl = band[b][0];
                yr = band[b][1];
                st.meterIn = std::max (std::max (std::fabs (yl), std::fabs (yr)), st.meterIn * meterFall);
                st.meterOut = st.meterIn;
            }
            else
            {
                st.inGain += (inTarget[b] - st.inGain) * smooth;
                st.outGain += (outTargetB[b] - st.outGain) * smooth;
                const float xl = band[b][0] * st.inGain, xr = band[b][1] * st.inGain;
                float g = 1.0f;
                if (!neutral[b])
                {
                    // detector: the band itself, the side-chain band, or a blend of both
                    const float dl = xl * (1.0f - scMix) + scBand[b][0] * scMix;
                    const float dr = xr * (1.0f - scMix) + scBand[b][1] * scMix;
                    float level;
                    if (rms)
                    {
                        st.rms = rmsCoef * st.rms + (1.0f - rmsCoef) * 0.5f * (dl * dl + dr * dr);
                        level = std::sqrt (st.rms);
                    }
                    else
                    {
                        const float inst = std::max (std::fabs (dl), std::fabs (dr));
                        st.peak = std::max (inst, st.peak * peakCoef);
                        level = st.peak;
                    }
                    const double lev = std::max (-120.0f, gainToDb (level));
                    const float tA = (float)(aboveGainDb (lev, ta[b], ra[b], softKnee) * amount);
                    const float tB = (float)(belowGainDb (lev, tb[b], rb[b], softKnee) * amount);
                    st.aboveDb = tA + (std::fabs (tA) > std::fabs (st.aboveDb) ? atk[b] : rel[b]) * (st.aboveDb - tA);
                    st.belowDb = tB + (std::fabs (tB) > std::fabs (st.belowDb) ? atk[b] : rel[b]) * (st.belowDb - tB);
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

    for (int b = 0; b < kNumBands; ++b)
    {
        meters[b].inputDb = gainToDb (bands[b].meterIn);
        meters[b].outputDb = gainToDb (bands[b].meterOut);
        meters[b].gainDb = active[b] ? bands[b].aboveDb + bands[b].belowDb : 0.0f;
        if (!used[b])
            meters[b] = BandMeter {};
    }
}

} // namespace multidyn
