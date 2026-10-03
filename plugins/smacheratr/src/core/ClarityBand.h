// Gently's (called Clarity before) band: the low mids it compresses. A 12 dB/oct high-pass below and
// a 6 dB/oct low-pass above, Width octaves apart around the centre frequency, scaled so the band
// peaks at 0 dB. A band that reaches an end of the spectrum turns into a shelf there, rather than
// dipping back up past the end: with its low edge at 20 Hz or below it is the 6 dB/oct low-pass alone
// (flat to the bottom), with its high edge at 20 kHz or above a 6 dB/oct high-pass at its low edge
// (flat to the top). A shelf is 1 exactly where it is flat and the complement of a filter that adds up
// with it to the input (lowPass1 / highPass1), so turning it down (x + (g - 1) band) is a shelving
// cut of exactly g there (a band alone gets its cut from the two filters' phases cancelling at its
// centre; a single low- or high-pass does not, so a shelf has to be one of such a pair). The Sub and
// High bands are shelves of their own (subBand, highBand). The engine turns the band down
// (x + (g - 1) * band) and the colour display draws it.
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

// first-order high-pass (6 dB/oct), bilinear: with lowPass1 at the same corner, the two add up to
// the input exactly (a complementary pair)
inline BiquadCoeffs highPass1 (double sr, double hz)
{
    const double k = std::tan (M_PI * std::min (hz, 0.45 * sr) / sr);
    BiquadCoeffs r;
    r.b0 = 1.0 / (1.0 + k);
    r.b1 = -r.b0;
    r.b2 = 0.0;
    r.a1 = (k - 1.0) / (k + 1.0);
    r.a2 = 0.0;
    return r;
}

// 1 - H: the filter that adds up with H to the input exactly (same poles)
inline BiquadCoeffs complementOf (const BiquadCoeffs& h)
{
    BiquadCoeffs r;
    r.b0 = 1.0 - h.b0;
    r.b1 = h.a1 - h.b1;
    r.b2 = h.a2 - h.b2;
    r.a1 = h.a1;
    r.a2 = h.a2;
    return r;
}

struct ClarityBand
{
    BiquadCoeffs hp, lp;
    double norm = 1.0; // scales the band's peak to 0 dB
    double lowHz = 0.0, highHz = 0.0;
    bool lowShelf = false, highShelf = false; // runs flat past its low / high edge (no filter there)
};

// where a band's edge makes it a shelf (the ends of the audible spectrum)
constexpr double kShelfLowHz = 20.0, kShelfHighHz = 20000.0;

inline ClarityBand clarityBand (double sr, double centerHz, double widthOct)
{
    ClarityBand b;
    const double half = std::pow (2.0, 0.5 * std::clamp (widthOct, 0.1, 8.0));
    b.lowShelf = centerHz / half <= kShelfLowHz;
    b.highShelf = centerHz * half >= std::min (kShelfHighHz, 0.45 * sr);
    b.lowHz = b.lowShelf ? kShelfLowHz : std::clamp (centerHz / half, 5.0, 0.3 * sr);
    b.highHz = b.highShelf ? std::max (b.lowHz * 1.01, std::min (kShelfHighHz, 0.45 * sr)) : std::clamp (centerHz * half, b.lowHz * 1.01, 0.45 * sr);
    if (b.lowShelf || b.highShelf)
    {
        // a shelf (both: the whole spectrum): 1 where it is flat, no scaling
        if (b.highShelf && !b.lowShelf)
            b.hp = highPass1 (sr, b.lowHz);
        if (b.lowShelf && !b.highShelf)
            b.lp = lowPass1 (sr, b.highHz);
        b.norm = 1.0;
        return b;
    }
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
    if (centerHz / half <= kShelfLowHz || centerHz * half >= std::min (kShelfHighHz, 0.45 * sr))
        return clarityBand (sr, centerHz, widthOct); // a shelf: its own peak scaling
    b.lowHz = std::clamp (centerHz / half, 5.0, 0.3 * sr);
    b.highHz = std::clamp (centerHz * half, b.lowHz * 1.01, 0.45 * sr);
    b.hp = highPass (sr, b.lowHz, M_SQRT1_2);
    b.lp = lowPass1 (sr, b.highHz);
    b.norm = norm;
    return b;
}

// Gently's Sub band: a shelf, flat from the very bottom of the spectrum up to `taperHz`, where its cut
// starts to let go (within about 1 dB of the full cut there, half of it around twice as high, nearly
// none an octave above that); taperHz is 20 - 100 Hz. It is the complement of a critically damped
// 12 dB/oct high-pass, so the cut is exactly the Range where the shelf is flat.
inline ClarityBand subBand (double sr, double taperHz)
{
    constexpr double kBottomHz = 20.0, kCornerOverTaper = 1.14; // the high-pass's corner over the taper point
    ClarityBand b;
    b.lowHz = kBottomHz; // (where the display starts it: the band runs on to the bottom)
    b.lowShelf = true;
    b.highHz = std::clamp (taperHz, 20.0, 100.0);
    b.lp = complementOf (highPass (sr, b.highHz * kCornerOverTaper, 0.5));
    b.norm = 1.0;
    return b;
}

// Gently's High band, the Sub band's mirror: a shelf, flat from `taperHz` up to the very top of the
// spectrum, its cut letting go below taperHz (within about 1 dB of the full cut there, half of it
// around half as high, nearly none an octave below that); taperHz is 2 - 16 kHz. It is the complement
// of a critically damped 12 dB/oct low-pass (which is 0 at Nyquist, so the shelf is the whole Range
// at the top), so the cut is exactly the Range where the shelf is flat.
inline ClarityBand highBand (double sr, double taperHz)
{
    constexpr double kCornerUnderTaper = 1.14; // the taper point over the low-pass's corner (subBand's, mirrored)
    ClarityBand b;
    b.highShelf = true;
    b.lowHz = std::clamp (taperHz, 2000.0, std::min (16000.0, 0.4 * sr));
    b.highHz = std::max (b.lowHz * 1.01, std::min (kShelfHighHz, 0.45 * sr)); // (where the display ends it: the band runs on to the top)
    b.hp = complementOf (lowPass (sr, b.lowHz / kCornerUnderTaper, 0.5));
    b.norm = 1.0;
    return b;
}

// What turning the band down by cutDb does at hz (dB): the signal plus (g - 1) times the band, with the
// band's phase (a shelf or a band's edges cut less than its magnitude alone suggests)
inline double clarityCutAtDb (const ClarityBand& b, double hz, double sr, double cutDb)
{
    auto resp = [&] (const BiquadCoeffs& c) {
        const std::complex<double> z1 = std::polar (1.0, -2.0 * M_PI * hz / sr), z2 = z1 * z1;
        return (c.b0 + c.b1 * z1 + c.b2 * z2) / (1.0 + c.a1 * z1 + c.a2 * z2);
    };
    const std::complex<double> h = resp (b.hp) * resp (b.lp) * b.norm;
    const double g = std::pow (10.0, cutDb / 20.0);
    return 20.0 * std::log10 (std::max (1e-9, std::abs (1.0 + (g - 1.0) * h)));
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
