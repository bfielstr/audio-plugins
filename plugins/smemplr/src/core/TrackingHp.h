// The high-pass that follows the transposition (kTransHpOn): a sample transposed far up brings what
// was below the audible range in it (rumble, a DC drift, the lowest partials) up into it. A cutoff
// that moves with the pitch keeps the sample's own low end where it was: base Hz at 0 semitones,
// base x 2^(semitones / 12) transposed.
//
// Para's slopes at resonance 0, built of Para's sections (the trapezoidal one-pole and state-variable
// filter), so the cutoff means what it means in Para: 6 dB first order and 18 dB Butterworth 3 (-3 dB at
// the cutoff); 12, 24, 36 and 48 dB Linkwitz-Riley (-6 dB at the cutoff: 12 dB one section of Q 0.5,
// the others Butterworth 2, 3 and 4 twice).
#pragma once

#include "para/src/core/Svf.h"

#include <algorithm>
#include <cmath>

namespace smemplr {

class TrackingHp
{
public:
    static constexpr int kMaxSections = 4, kMaxOnePoles = 2;

    void reset ()
    {
        for (auto& ch : svf)
            for (auto& s : ch)
                s.reset ();
        for (auto& ch : one)
            for (auto& o : ch)
                o.reset ();
    }

    // slope: TransHpSlope (6, 12, 18, 24, 36 or 48 dB per octave)
    void setup (int slope, double hz, double sr)
    {
        slope = std::clamp (slope, 0, 5);
        if (slope != shapeOf)
        {
            shapeOf = slope;
            onePoles = sections = 0;
            auto butterworth = [this] (int n) {
                for (int k = 0; k < n / 2; ++k)
                    q[sections++] = 1.0 / (2.0 * std::sin (M_PI * (2.0 * k + 1.0) / (2.0 * n)));
                onePoles += n % 2;
            };
            switch (slope)
            {
                case 0: onePoles = 1; break;               // 6 dB
                case 1: q[sections++] = 0.5; break;        // 12 dB: Linkwitz-Riley 2
                case 2: butterworth (3); break;            // 18 dB
                default:                                   // 24, 36, 48 dB: Linkwitz-Riley 4, 6, 8
                {
                    const int n = slope == 3 ? 2 : (slope == 4 ? 3 : 4);
                    butterworth (n);
                    butterworth (n);
                    break;
                }
            }
        }
        // every section sits at the cutoff: one tan () serves them all
        const double g = std::tan (M_PI * std::clamp (hz, 5.0, 0.49 * sr) / sr);
        for (int k = 0; k < sections; ++k)
        {
            auto& c = co[k]; // as para::SvfCoeffs::set does, with the tan () done once
            c.g = g;
            c.k = 1.0 / q[k];
            c.a1 = 1.0 / (1.0 + g * (g + c.k));
            c.a2 = g * c.a1;
            c.a3 = g * c.a2;
        }
        g1 = g;
    }

    float process (float in, int c)
    {
        double x = in, lp, hp;
        for (int k = 0; k < onePoles; ++k)
        {
            one[c][k].tick (g1, x, lp, hp);
            x = hp;
        }
        for (int k = 0; k < sections; ++k)
        {
            svf[c][k].tick (co[k], x, lp, hp);
            x = hp;
        }
        return (float)x;
    }

private:
    para::SvfCoeffs co[kMaxSections];
    para::Svf svf[2][kMaxSections];
    para::OnePole one[2][kMaxOnePoles];
    double q[kMaxSections] {};
    double g1 = 0.0;
    int onePoles = 0, sections = 0, shapeOf = -1;
};

} // namespace smemplr
