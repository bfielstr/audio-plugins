#include "Ttm.h"

#include <cmath>

namespace detonatr {

void Ttm::prepare (double sampleRate, int)
{
    sr = sampleRate;
    levelC = dsp::coef (5.0, sr);
    longC = dsp::coef (1000.0, sr);
    longRiseC = dsp::coef (300.0, sr);
    powC = dsp::coef (500.0, sr);
    susC = dsp::coef (100.0, sr); // the level's 100 ms average (auto release)
    makeupC = 1.0f - std::exp (-16.0f / (float)(0.05 * sr)); // updated every 16 samples
    setAttackMs (attackMs);
    setReleaseMs (releaseMs);
    setHoldMs (holdMs);
    setCrossovers (xLow, xHigh);
    reset ();
}

void Ttm::reset ()
{
    splitLow.reset ();
    splitHigh.reset ();
    lowAllpass.reset ();
    ms.fill (0.0f);
    longTerm.fill (0.0f);
    levelDb.fill (-100.0f);
    gain.fill (0.0f);
    slowDb.fill (-100.0f);
    down.fill (0.0f);
    up.fill (0.0f);
    hold.fill (0);
    makeup = 0.0f;
    inPow = outPow = 0.0f;
    ctl = 0;
}

void Ttm::setAttackMs (double v)
{
    attackMs = v;
    attC = dsp::coef (v, sr);
}

void Ttm::setReleaseMs (double v)
{
    releaseMs = v;
    relC = dsp::coef (v, sr);
}

void Ttm::setHoldMs (double v)
{
    holdMs = v;
    holdSamples = (int)std::lround (std::max (0.0, v) * 0.001 * sr);
}

void Ttm::setCrossovers (double lo, double hi)
{
    lo = std::clamp (lo, 20.0, 0.4 * sr);
    hi = std::clamp (std::max (hi, lo * 1.5), 30.0, 0.45 * sr);
    xLow = lo;
    xHigh = hi;
    splitLow.setup (multidyn::xoverG (lo, sr), multidyn::kXover24);
    const float gh = multidyn::xoverG (hi, sr);
    splitHigh.setup (gh, multidyn::kXover24);
    lowAllpass.setup (gh, multidyn::kXover24);
}

void Ttm::process (float* l, float* r, int n)
{
    const float lc = levelC, lt = longC, lr = longRiseC, sc = susC, at = attC, sl = slope, kn = knee, rg = range;
    const float relBase = (float)releaseMs, fs = (float)sr;
    for (int i = 0; i < n; ++i)
    {
        float band[kBands][2];
        for (int c = 0; c < 2; ++c)
        {
            const float x = c == 0 ? l[i] : r[i];
            float lo, rest, mid, hi;
            splitLow.tick (x, c, lo, rest);
            splitHigh.tick (rest, c, mid, hi);
            band[0][c] = lowAllpass.tick (lo, c);
            band[1][c] = mid;
            band[2][c] = hi;
        }
        float yl = 0.0f, yr = 0.0f, dl = 0.0f, dr = 0.0f;
        for (int b = 0; b < kBands; ++b)
        {
            const float p = 0.5f * (band[b][0] * band[b][0] + band[b][1] * band[b][1]);
            float& m = ms[(size_t)b];
            m += (p - m) * lc;
            float& lg = longTerm[(size_t)b];
            lg += (m - lg) * (m > lg ? lr : lt);
            if (lg < 1e-10f && m > lg)
                lg = m; // from silence (or a reset): the program level starts where the sound does
            const float lev = dsp::powToDb (m + 1e-20f);
            levelDb[(size_t)b] = lev;
            const float d = lev - targetOf (b);
            // the knee: the correction fades in over Knee dB from the target
            const float ad = std::fabs (d);
            const float soft = ad >= kn ? ad - 0.5f * kn : ad * ad / (2.0f * kn + 1e-9f);
            float want = std::clamp (-(d > 0.0f ? soft : -soft) * sl, -rg, rg);
            if (want > 0.0f)
                want *= std::clamp ((lev + 60.0f) * 0.1f, 0.0f, 1.0f); // nothing raised from under -60 dBFS
            // the two stages: down (cuts, held for Hold before they release) and up (boosts)
            const float wantDown = std::min (0.0f, want), wantUp = std::max (0.0f, want);
            float& sl2 = slowDb[(size_t)b];
            sl2 += (lev - sl2) * sc;
            float rc = relC, rcUp = relC;
            if (autoRelease)
            {
                // program dependent: the further the level is over its 100 ms average (a hit), the
                // faster; a boost lets go of a hit within a millisecond
                const float crest = std::clamp ((lev - sl2) * (1.0f / 12.0f), 0.0f, 1.0f);
                const float x = 1000.0f / (relBase * (1.5f - 1.45f * crest) * fs);
                rc = x / (1.0f + 0.5f * x);
                const float xu = 1000.0f / ((relBase * 1.5f + (1.0f - relBase * 1.5f) * crest) * fs);
                rcUp = xu / (1.0f + 0.5f * xu);
            }
            float& gd = down[(size_t)b];
            if (wantDown < gd)
            {
                gd += (wantDown - gd) * at;
                hold[(size_t)b] = holdSamples;
            }
            else if (hold[(size_t)b] > 0)
                --hold[(size_t)b];
            else
                gd += (wantDown - gd) * rc;
            float& gu = up[(size_t)b];
            gu += (wantUp - gu) * (wantUp > gu ? at : rcUp);
            float& g = gain[(size_t)b];
            g = gd + gu;
            const float k = dsp::dbToAmp (g);
            yl += band[b][0] * k;
            yr += band[b][1] * k;
            dl += band[b][0];
            dr += band[b][1];
        }
        // auto gain: the input's power against the output's (before the make-up), both over 1 s
        inPow += (0.5f * (l[i] * l[i] + r[i] * r[i]) - inPow) * powC;
        outPow += (0.5f * (yl * yl + yr * yr) - outPow) * powC;
        if (++ctl >= 16)
        {
            ctl = 0;
            float target = 0.0f;
            if (autoGain && inPow > 1e-12f && outPow > 1e-12f)
                target = std::clamp (dsp::powToDb (inPow / outPow), -24.0f, 24.0f);
            makeup += (target - makeup) * makeupC;
        }
        const float og = outGain * dsp::dbToAmp (makeup);
        l[i] = yl * og + dl * dry;
        r[i] = yr * og + dr * dry;
    }
}

} // namespace detonatr
