// Gently's (called Clarity before) band: the low mids it compresses. A 12 dB/oct high-pass below and
// a 6 dB/oct low-pass above, Width octaves apart around the centre frequency, scaled so the band
// peaks at 0 dB. The engine turns the band down (x + (g - 1) * band) and the colour display draws it.
// The law that sets the cut is clarityCutDb (Params.h).
#pragma once

#include "Biquad.h"
#include "Shaper.h"

#include <algorithm>
#include <cmath>

namespace smacheratr {

// first-order low-pass (6 dB/oct), bilinear
inline BiquadCoeffs lowPass1 (double sr, double hz)
{
    const double k = std::tan (M_PI * std::min (hz, 0.45 * sr) / sr);
    BiquadCoeffs r;
    r.b0 = r.b1 = k / (1.0 + k);
    r.b2 = 0.0;
    r.a1 = (k - 1.0) / (k + 1.0);
    r.a2 = 0.0;
    return r;
}

struct ClarityBand
{
    BiquadCoeffs hp, lp;
    double norm = 1.0; // scales the band's peak to 0 dB
    double lowHz = 0.0, highHz = 0.0;
};

inline ClarityBand clarityBand (double sr, double centerHz, double widthOct)
{
    ClarityBand b;
    const double half = std::pow (2.0, 0.5 * std::clamp (widthOct, 0.1, 8.0));
    b.lowHz = std::clamp (centerHz / half, 5.0, 0.3 * sr);
    b.highHz = std::clamp (centerHz * half, b.lowHz * 1.01, 0.45 * sr);
    b.hp = highPass (sr, b.lowHz, M_SQRT1_2);
    b.lp = lowPass1 (sr, b.highHz);
    double peakDb = -200.0;
    for (int i = 0; i <= 64; ++i)
    {
        const double hz = b.lowHz * 0.5 * std::pow (b.highHz * 2.0 / (b.lowHz * 0.5), i / 64.0);
        peakDb = std::max (peakDb, magnitudeDb (b.hp, hz, sr) + magnitudeDb (b.lp, hz, sr));
    }
    b.norm = std::pow (10.0, -peakDb / 20.0);
    return b;
}

// The band at another centre, keeping a peak scaling worked out before (the shape of the band on a
// log axis does not change with its centre, away from Nyquist): cheap enough to retune often.
inline ClarityBand clarityBandAt (double sr, double centerHz, double widthOct, double norm)
{
    ClarityBand b;
    const double half = std::pow (2.0, 0.5 * std::clamp (widthOct, 0.1, 8.0));
    b.lowHz = std::clamp (centerHz / half, 5.0, 0.3 * sr);
    b.highHz = std::clamp (centerHz * half, b.lowHz * 1.01, 0.45 * sr);
    b.hp = highPass (sr, b.lowHz, M_SQRT1_2);
    b.lp = lowPass1 (sr, b.highHz);
    b.norm = norm;
    return b;
}

// the band's level at hz, dB (0 at its peak)
inline double clarityBandDb (const ClarityBand& b, double hz, double sr)
{
    return magnitudeDb (b.hp, hz, sr) + magnitudeDb (b.lp, hz, sr) + 20.0 * std::log10 (b.norm);
}

// Gently's region drive: what driving the cut band region `x` (the bands after their cut) through the
// Analog curve adds to the signal, level-matched (the curve's output divided by the gain, so a quiet
// region passes as it is and a loud one is squashed and gains harmonics, rather than getting louder).
// The engine adds it to the signal going into the curve.
inline double clarityRegionDrive (double x, double gain) { return analogClip (x * gain) / gain - x; }

} // namespace smacheratr
