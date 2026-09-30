// Para's slopes, the same for the high-pass and the low-pass: 6 to 96 dB per octave and Brickwall. At
// every slope the pair meets flat: a high-pass and a low-pass at one cutoff, resonance 0, sum to a flat
// response (an all-pass), so the two filters can meet without a bump.
//   6 dB: first order (the two sum to the input exactly).
//   12, 24, 36 .. 96 dB: Linkwitz-Riley (a Butterworth squared; 12 dB is one section of Q 0.5). The
//     high-pass is inverted in the sum at 12, 36, 60 and 84 dB: those orders sum flat that way.
//   18 dB: Butterworth 3 (a first-order and a second-order section, Q 1): the pair is in quadrature.
//   Brickwall: a 13th-order elliptic pair that is doubly complementary: the low-pass is the half-sum of
//     two all-passes and the high-pass their half-difference. Both are -3 dB at the cutoff, down 40 dB a
//     tenth of the way past it and 80 dB (the floor) from 0.8 x (the low-pass: 1.25 x) on, and they sum
//     to one of the all-passes. The poles of this design lie on the unit circle, so its sections are all
//     at the cutoff too; only their Q differ.
// Every section sits at the cutoff, so one tan () per update serves a whole filter. The digital filters
// are the analog ones through the bilinear transform warped at the cutoff, built of trapezoidal state-
// variable sections, which stay stable while the cutoff moves.
// Resonance: 12 to 96 dB raise their sections' Q (at 12, 18 and 24 dB each section's by up to 20x, as
// always; from 36 dB on spread over the sections so the peak at the cutoff is the 24 dB one's, not a
// power of it). 6 dB and Brickwall have no section to raise: they get a resonant bell at the cutoff
// (up to +26 dB, Q 10), gone at resonance 0.
#pragma once

#include "Params.h"
#include "Svf.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>

namespace para {

constexpr int kMaxSections = 8; // second-order sections of one filter (96 dB)

struct SlopeShape
{
    int onePoles = 0;          // first-order sections (Brickwall: its real all-pass pole, in A1)
    int sections = 0;          // second-order sections (Brickwall: all-passes, the first a1Sections in A1)
    int a1Sections = 0;
    double q[kMaxSections] {}; // the sections' Q at resonance 0
    double resExp = 1.0;       // resonance r multiplies each section's Q by 20^(r * resExp)
    bool invertHp = false;     // the high-pass is inverted in the sum
    bool bell = false;         // the resonance is a bell at the cutoff
    bool allpass = false;      // Brickwall's all-pass pair
};

inline const SlopeShape& slopeShape (int slope)
{
    static const std::array<SlopeShape, kNumSlopes> shapes = [] {
        std::array<SlopeShape, kNumSlopes> t {};
        t[kSlope6].onePoles = 1;
        t[kSlope6].bell = true;
        t[kSlope12].sections = 1;
        t[kSlope12].q[0] = 0.5;
        t[kSlope12].invertHp = true;
        t[kSlope18].onePoles = 1;
        t[kSlope18].sections = 1;
        t[kSlope18].q[0] = 1.0;
        // Linkwitz-Riley 2n: Butterworth n twice; its second-order sections have Q 1 / (2 sin ((2k + 1) pi / 2n)),
        // an odd n a first-order section too
        for (int slope = kSlope24; slope <= kSlope96; ++slope)
        {
            SlopeShape& s = t[(size_t)slope];
            const int n = slope - kSlope24 + 2; // 24 dB: n = 2 .. 96 dB: n = 8
            for (int copy = 0; copy < 2; ++copy)
                for (int k = 0; k < n / 2; ++k)
                    s.q[s.sections++] = 1.0 / (2.0 * std::sin (M_PI * (2.0 * k + 1.0) / (2.0 * n)));
            s.onePoles = n % 2 == 1 ? 2 : 0;
            s.invertHp = n % 2 == 1;
            s.resExp = std::min (1.0, 2.0 / s.sections);
        }
        // Brickwall: the elliptic pair's all-pass sections (13th order, 80 dB, crossing at -3 dB). A1: the
        // first-order section and Q 0.776, 2.03 and 12.7; A2: Q 0.564, 1.20 and 3.91.
        SlopeShape& b = t[kSlopeBrickwall];
        b.allpass = true;
        b.bell = true;
        b.onePoles = 1;
        b.sections = 6;
        b.a1Sections = 3;
        const double q[6] = {0.77609643476727919, 2.031458689420337, 12.669794145485277,
                             0.56430768069235437, 1.2040982525401032, 3.9087540264872671};
        std::copy (q, q + 6, b.q);
        return t;
    }();
    return shapes[(size_t)std::clamp (slope, 0, kNumSlopes - 1)];
}

// How much resonance r (0 .. 1) raises each section's Q.
inline double qScale (const SlopeShape& s, double res) { return std::pow (20.0, std::clamp (res, 0.0, 1.0) * s.resExp); }
// The bell of 6 dB and Brickwall: its peak (as a gain) and its Q.
inline double bellGain (double res) { return std::pow (20.0, std::clamp (res, 0.0, 1.0)); }
inline double bellQ (double res) { return 0.5 * bellGain (res); }

// The Q of a slope's first second-order section at resonance res (12 dB: its one section; resonance 0
// is where the pair meets flat: 0.5 at 12 dB, 1 at 18, 0.707 at 24), up to 20x that at 12, 18 and 24 dB.
inline double resonanceToQ (double res, int slope = kSlope12)
{
    const SlopeShape& s = slopeShape (slope);
    return (s.sections > 0 ? s.q[0] : 0.5) * qScale (s, res);
}

// One filter's coefficients (shared by the channels): a slope at a cutoff and resonance.
struct FilterCoeffs
{
    SvfCoeffs s[kMaxSections], bell;
    double g1 = 0.0, bellM = 0.0; // the first-order sections' g; the bell adds bellM x its band-pass

    void set (int slope, double hz, double res, double sr)
    {
        const SlopeShape& sh = slopeShape (slope);
        const double g = std::tan (M_PI * std::fmin (std::fmax (hz, 5.0), 0.49 * sr) / sr);
        const double scale = sh.allpass ? 1.0 : qScale (sh, res);
        for (int k = 0; k < sh.sections; ++k)
            s[k].setG (g, sh.q[k] * scale);
        g1 = g;
        if (sh.bell)
        {
            // at the cutoff: 1 + bellM / k = the bell's gain
            bell.setG (g, bellQ (res));
            bellM = bell.k * (bellGain (res) - 1.0);
        }
    }
};

// One filter's state, per channel.
struct FilterState
{
    Svf s[kMaxSections], bell;
    OnePole o[2];
    void reset ()
    {
        for (auto& x : s)
            x.reset ();
        bell.reset ();
        for (auto& x : o)
            x.reset ();
    }
};

// One sample through the high-pass (hp) or the low-pass, with the polarity the sum uses.
inline double filterTick (const SlopeShape& sh, const FilterCoeffs& c, FilterState& st, double x, bool hp)
{
    double y = x, lp, bp, h;
    if (sh.allpass)
    {
        // A1: the first-order all-pass (low-pass minus high-pass) and a1Sections sections; A2: the rest
        // (a section's all-pass: x - 2 k bp)
        st.o[0].tick (c.g1, x, lp, h);
        double a1 = lp - h, a2 = x;
        for (int k = 0; k < sh.sections; ++k)
        {
            double& a = k < sh.a1Sections ? a1 : a2;
            st.s[k].tick (c.s[k], a, lp, bp, h);
            a -= 2.0 * c.s[k].k * bp;
        }
        y = hp ? 0.5 * (a2 - a1) : 0.5 * (a1 + a2);
    }
    else
    {
        for (int k = 0; k < sh.onePoles; ++k)
        {
            st.o[k].tick (c.g1, y, lp, h);
            y = hp ? h : lp;
        }
        for (int k = 0; k < sh.sections; ++k)
        {
            st.s[k].tick (c.s[k], y, lp, h);
            y = hp ? h : lp;
        }
    }
    if (sh.bell)
    {
        st.bell.tick (c.bell, y, lp, bp, h);
        y += c.bellM * bp;
    }
    return hp && sh.invertHp ? -y : y;
}

// The analog response at f of the high-pass (hp) or the low-pass at cutoff fc and resonance res, with
// the polarity the sum uses. The digital filter's response at f is this at the frequency warped by the
// bilinear transform: fc tan (pi f / sr) / tan (pi fc / sr) (the cutoff clamped as the engine does).
inline std::complex<double> filterResponse (int slope, bool hp, double f, double fc, double res)
{
    const SlopeShape& sh = slopeShape (slope);
    const std::complex<double> s (0.0, f / fc), one (1.0, 0.0);
    auto allpass2 = [&] (double q) { return (s * s - s / q + one) / (s * s + s / q + one); };
    std::complex<double> h = one;
    if (sh.allpass)
    {
        std::complex<double> a1 = (one - s) / (one + s), a2 = one;
        for (int k = 0; k < sh.sections; ++k)
            (k < sh.a1Sections ? a1 : a2) *= allpass2 (sh.q[k]);
        h = hp ? 0.5 * (a2 - a1) : 0.5 * (a1 + a2);
    }
    else
    {
        const double scale = qScale (sh, res);
        for (int k = 0; k < sh.onePoles; ++k)
            h *= hp ? highPass1Response (f, fc) : lowPass1Response (f, fc);
        for (int k = 0; k < sh.sections; ++k)
            h *= hp ? highPassResponse (f, fc, sh.q[k] * scale) : lowPassResponse (f, fc, sh.q[k] * scale);
    }
    if (sh.bell)
    {
        const double k = 1.0 / bellQ (res), m = k * (bellGain (res) - 1.0);
        h *= (s * s + (k + m) * s + one) / (s * s + k * s + one);
    }
    return hp && sh.invertHp ? -h : h;
}

} // namespace para
