// The Spike stage: transients found per band, boosted or cut.
#include "Harness.h"
#include "Signals.h"
#include "Spike.h"

#include <cmath>
#include <cstdio>
#include <random>

using namespace detonatr;

namespace {
constexpr double kSr = 48000.0;
constexpr size_t kOnset = 24000;

Spike make (int mode)
{
    Spike s;
    s.prepare (kSr, 256);
    s.setMode (mode);
    s.setDepth (5.1);
    s.setSensitivity (3.7);
    s.setDecay (7.1);
    s.setSharpness (1.3);
    return s;
}

// a steady 200 Hz tone, and a 2 kHz tone that starts sharply at kOnset
std::vector<float> hit ()
{
    auto x = sig::sine (200.0, 0.1, 1.0, kSr);
    const auto b = sig::sine (2000.0, 0.2, 0.4, kSr);
    for (size_t i = 0; i < b.size (); ++i)
        x[kOnset + i] += b[i];
    return x;
}

// the 2 kHz tone's start (its first 8 ms) over its body (200 .. 300 ms in), dB
double attackOverBody (const std::vector<float>& y)
{
    const double a = sig::amplitude (y, 2000.0, kSr, kOnset, kOnset + 384);
    const double b = sig::amplitude (y, 2000.0, kSr, kOnset + 9600, kOnset + 14400);
    return sig::db (a / b);
}
} // namespace

TEST (boost_raises_a_transient_in_its_band)
{
    Spike s = make (Spike::kBoost);
    const auto x = hit ();
    const auto y = sig::run (s, x);
    const double in = attackOverBody (x), out = attackOverBody (y);
    std::printf ("    2 kHz start over its body: %.1f dB in, %.1f dB out\n", in, out);
    CHECK (out > in + 3.0, "boosted: %.1f -> %.1f dB", in, out);
    // the steady low tone is left alone
    const double low = sig::db (sig::amplitude (y, 200.0, kSr, kOnset - 9600, kOnset) / sig::amplitude (x, 200.0, kSr, kOnset - 9600, kOnset));
    CHECK (std::fabs (low) < 0.3, "the steady tone untouched: %+.2f dB", low);
}

TEST (cut_reduces_a_transient_in_its_band)
{
    Spike s = make (Spike::kCut);
    const auto x = hit ();
    const auto y = sig::run (s, x);
    const double in = attackOverBody (x), out = attackOverBody (y);
    std::printf ("    2 kHz start over its body: %.1f dB in, %.1f dB out\n", in, out);
    CHECK (out < in - 2.0, "cut: %.1f -> %.1f dB", in, out);
}

TEST (depth_and_mix_zero_pass_it)
{
    const auto x = hit ();
    Spike a = make (Spike::kBoost);
    a.setDepth (0.0);
    Spike b = make (Spike::kBoost);
    b.setMix (0.0);
    const auto ya = sig::run (a, x), yb = sig::run (b, x);
    double ea = 0.0, eb = 0.0;
    for (size_t i = 0; i < x.size (); ++i)
    {
        ea = std::max (ea, (double)std::fabs (ya[i] - x[i]));
        eb = std::max (eb, (double)std::fabs (yb[i] - x[i]));
    }
    CHECK (ea < 1e-6 && eb < 1e-6, "Depth 0 (%.2g) and Mix 0 (%.2g): the input", ea, eb);
}

TEST (decay_sets_how_long)
{
    // a longer Decay keeps the boost on longer after the onset
    auto tail = [] (double decay) {
        Spike s = make (Spike::kBoost);
        s.setDecay (decay);
        const auto x = hit ();
        const auto y = sig::run (s, x);
        return sig::db (sig::amplitude (y, 2000.0, kSr, kOnset + 1440, kOnset + 2400) / sig::amplitude (x, 2000.0, kSr, kOnset + 1440, kOnset + 2400));
    };
    const double shortD = tail (2.0), longD = tail (9.0);
    std::printf ("    30 .. 50 ms after the onset: %+.1f dB at Decay 2, %+.1f dB at Decay 9\n", shortD, longD);
    CHECK (longD > shortD + 1.0, "longer Decay, longer boost");
}

TEST (finite_everywhere)
{
    std::mt19937 rng (4);
    std::uniform_real_distribution<double> u (0.0, 1.0);
    Spike s = make (Spike::kBoost);
    bool ok = true;
    for (int round = 0; round < 12; ++round)
    {
        s.setMode (u (rng) < 0.5 ? 0 : 1);
        s.setDepth (u (rng) * 10.0);
        s.setSensitivity (u (rng) * 10.0);
        s.setDecay (u (rng) * 10.0);
        s.setSharpness (u (rng) * 10.0);
        s.setDecayTilt (u (rng) * 20.0 - 10.0);
        s.setLink (u (rng));
        s.setRange (20.0 + u (rng) * 1000.0, 1000.0 + u (rng) * 19000.0);
        auto x = sig::noise (u (rng) < 0.3 ? 3.0 : 0.1, 0.2, kSr, (unsigned)round);
        if (round % 4 == 0)
            std::fill (x.begin (), x.end (), 0.0f);
        ok = ok && sig::finite (sig::run (s, x, 1 + (int)(u (rng) * 600)));
    }
    CHECK (ok, "finite with random settings, loud input and silence");
}

DETONATR_TEST_MAIN
