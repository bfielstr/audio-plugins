#include "Transient.h"

#include <cmath>

namespace detonatr {

void Transient::prepare (double sampleRate, int)
{
    sr = sampleRate;
    setOvershootMs (overshootMs);
    setRiseMs (riseMs);
    setRecoveryMs (recoveryMs);
    smoothC = dsp::coef (0.05, sr);
    reset ();
}

void Transient::reset ()
{
    peak = avg = gainDb = 0.0f;
    maxBoost = maxCut = 0.0f;
}

void Transient::setOvershootMs (double ms)
{
    overshootMs = ms;
    overC = dsp::coef (ms, sr);
}

void Transient::setRiseMs (double ms)
{
    riseMs = ms;
    riseC = dsp::coef (ms, sr);
}

void Transient::setRecoveryMs (double ms)
{
    recoveryMs = ms;
    recC = dsp::coef (ms, sr);
}

void Transient::takeGainRange (float& boostDb, float& cutDb)
{
    boostDb = maxBoost;
    cutDb = maxCut;
    maxBoost = maxCut = 0.0f;
}

void Transient::process (float* l, float* r, int n)
{
    const float g = inGain, thr = threshDb, db = deadband, ra = ratio, dr = drive, og = outGain, m = mix;
    const float rc = riseC, oc = overC, ac = recC, sc = smoothC;
    for (int i = 0; i < n; ++i)
    {
        const float xl = l[i] * g, xr = r[i] * g;
        dsp::follow (peak, std::max (std::fabs (xl), std::fabs (xr)), rc, oc);
        avg += (peak - avg) * ac; // the longer-term level: the peak level, averaged
        const float pDb = std::max (dsp::ampToDb (peak + 1e-12f), thr), aDb = std::max (dsp::ampToDb (avg + 1e-12f), thr);
        const float d = pDb - aDb;
        const float dd = d > db ? d - db : (d < -db ? d + db : 0.0f);
        const float target = std::clamp (ra * dd, -kMaxDb, kMaxDb);
        gainDb += (target - gainDb) * sc;
        maxBoost = std::max (maxBoost, gainDb);
        maxCut = std::min (maxCut, gainDb);
        const float k = dsp::dbToAmp (gainDb);
        float yl = xl * k, yr = xr * k;
        if (dr > 0.0f && gainDb > 0.0f)
        {
            // saturation on the raised attack
            const float s = dr * std::min (1.0f, gainDb * (1.0f / 6.0f));
            yl += s * (std::tanh (1.5f * yl) * (1.0f / 1.5f) - yl);
            yr += s * (std::tanh (1.5f * yr) * (1.0f / 1.5f) - yr);
        }
        l[i] += (yl * og - l[i]) * m;
        r[i] += (yr * og - r[i]) * m;
    }
    if (peak < 1e-20f)
        peak = 0.0f;
    if (avg < 1e-20f)
        avg = 0.0f;
}

} // namespace detonatr
