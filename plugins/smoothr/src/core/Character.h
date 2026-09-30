// Character: a dip in the low mids (somewhere in 80 - 250 Hz) that only opens on loud low-mid content,
// just before the limiter. It is Smacheratr's Clarity (Gently) at work on its own:
//   - it listens through Clarity's band (ClarityBand.h: a 12 dB/oct high-pass under it, a 6 dB/oct
//     low-pass over it, peak at 0 dB), here run twice in a row (24 dB/oct under, 12 over) so that
//     only the low mids open it, not a loud bass or a loud upper mid;
//   - Clarity's law sets the cut: the band's level (a sine's peak level, 15 ms attack, 150 ms release)
//     over -18 dBFS turns it down 3 dB for every 5 dB, up to the range; smoothed over 20 ms;
//   - the cut is applied the way Clarity applies it, x + (g - 1) band, but through a resonant
//     band-pass at the same centre and width. Clarity's own band turns the phase round at its edges,
//     so that sum lifts what lies under the band (with a 6 dB dip, 2 dB at 80 Hz: the bass, just
//     what should not be pushed into a limiter); a resonant band-pass is the one band for which the
//     sum can only ever cut (its real part is its squared size), so the dip has no bump either side.
//
// Why before the limiter: the low mids (the boxy build-up of bass harmonics, kick bodies, low guitars
// and keys) carry a lot of peak level for how little they add; turned down when they pile up, they
// stop eating the limiter's headroom, so the limiter pulls the lows down less and less often. It sits
// after the saturator, so it also catches the harmonics the saturator adds there (a 55 Hz bass's
// third is at 165 Hz).
//
// The Character knob moves everything at once: 0 is off (the signal passes bit for bit), and as it
// turns the dip deepens (up to 6 dB), widens (0.9 to 1.5 octaves between its -3 dB points) and slides
// down (its centre from 175 to 140 Hz): a light scoop under the vocals at first, a fuller clean-up of
// the low mids at the top.
#pragma once

#include "smacheratr/src/core/ClarityBand.h"

#include <algorithm>
#include <cmath>

namespace smoothr {

class CharacterDip
{
public:
    static constexpr double kThresholdDb = -18.0; // Clarity's
    static constexpr double kMaxRangeDb = 6.0;

    // the dip for a Character amount (0..1)
    static void shape (double amount, double& centreHz, double& widthOct, double& rangeDb)
    {
        const double a = std::clamp (amount, 0.0, 1.0);
        centreHz = 175.0 * std::pow (140.0 / 175.0, a);
        widthOct = 0.9 + 0.6 * a;
        rangeDb = kMaxRangeDb * a;
    }

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        atk = 1.0 - std::exp (-1.0 / (0.015 * sr));
        rel = 1.0 - std::exp (-1.0 / (0.15 * sr));
        smooth = 1.0 - std::exp (-1.0 / (0.02 * sr));
        designed = -1.0;
        reset ();
    }
    void reset ()
    {
        stop ();
        shaped = target;
    }
    void setAmount (double a) { target = std::clamp (a, 0.0, 1.0); }
    float cutDb () const { return running ? (float)(20.0 * std::log10 (std::max (1e-6, gain))) : 0.0f; }
    bool isRunning () const { return running; }

    // In place.
    void process (float* l, float* r, int n)
    {
        if (!running)
        {
            if (target <= 0.0)
                return; // off: the signal passes untouched
            running = true;
        }
        // the shape follows the knob in small steps (it stays where it was while the dip fades out)
        if (target > 0.0 && shaped != target)
        {
            const double d = target - shaped;
            shaped = std::fabs (d) < 0.002 ? target : shaped + std::clamp (d, -0.02, 0.02);
        }
        if (shaped != designed)
            design (shaped);
        double centre, width, range;
        shape (target, centre, width, range);
        float* ch[2] = {l, r};
        for (int i = 0; i < n; ++i)
        {
            if ((i & 15) == 0)
            {
                const double levelDb = 10.0 * std::log10 (std::max (1e-12, env));
                const double cut = std::clamp ((levelDb - kThresholdDb) * 0.6, 0.0, range);
                gainTarget = std::pow (10.0, -cut / 20.0);
            }
            gain += (gainTarget - gain) * smooth;
            double power = 0.0;
            for (int c = 0; c < 2; ++c)
            {
                const double x = ch[c][i];
                const double heard = lp[c][1].process (hp[c][1].process (lp[c][0].process (hp[c][0].process (x)))) * norm;
                power = std::max (power, 2.0 * heard * heard); // a sine's peak level
                ch[c][i] = (float)(x + (gain - 1.0) * bp[c].process (x));
            }
            env += (power - env) * (power > env ? atk : rel);
        }
        // faded out with the knob at 0: off again (from here the signal passes bit for bit)
        if (target <= 0.0 && std::fabs (gain - 1.0) < 1e-6)
            stop ();
    }

private:
    void stop ()
    {
        running = false;
        for (int c = 0; c < 2; ++c)
        {
            for (int k = 0; k < 2; ++k)
            {
                hp[c][k].reset ();
                lp[c][k].reset ();
            }
            bp[c].reset ();
        }
        env = 0.0;
        gain = gainTarget = 1.0;
    }
    void design (double amount)
    {
        designed = amount;
        double centre, width, range;
        shape (amount, centre, width, range);
        // what it listens through: Clarity's band, twice
        const smacheratr::ClarityBand b = smacheratr::clarityBand (sr, centre, width);
        // what it cuts through: a band-pass (0 dB at the centre) whose -3 dB points are width octaves apart
        const double bw = std::pow (2.0, width);
        const smacheratr::BiquadCoeffs cut = smacheratr::bandPass (sr, centre, std::sqrt (bw) / (bw - 1.0));
        for (int c = 0; c < 2; ++c)
        {
            for (int k = 0; k < 2; ++k)
            {
                hp[c][k].c = b.hp;
                lp[c][k].c = b.lp;
            }
            bp[c].c = cut;
        }
        norm = b.norm * b.norm; // the pair peaks where one pass does, at the square of its peak
    }

    double sr = 48000.0;
    double target = 0.0, shaped = 0.0, designed = -1.0;
    smacheratr::Biquad hp[2][2], lp[2][2]; // what it listens through: [channel][pass]
    smacheratr::Biquad bp[2];              // what it cuts through
    double norm = 1.0;
    double env = 0.0, atk = 0.0, rel = 0.0, smooth = 0.0;
    double gain = 1.0, gainTarget = 1.0;
    bool running = false;
};

} // namespace smoothr
