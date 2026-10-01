// The Transient stages: TransMod-style attack emphasis.
#include "Harness.h"
#include "Signals.h"
#include "Transient.h"

#include <cmath>
#include <cstdio>
#include <random>

using namespace detonatr;

namespace {
constexpr double kSr = 48000.0;
constexpr size_t kStep = 24000;

Transient make (double ratio)
{
    Transient t;
    t.setGainDb (0.0);
    t.setThresholdDb (-80.0);
    t.setDeadbandDb (0.0);
    t.setRatio (ratio);
    t.setOvershootMs (25.0);
    t.setRiseMs (0.1);
    t.setRecoveryMs (80.0);
    t.setOverdrive (0.0);
    t.setOutputDb (0.0);
    t.setMix (1.0);
    t.prepare (kSr, 256);
    return t;
}

// a 200 Hz tone that jumps up by `stepDb` at kStep and stays there
std::vector<float> step (double low, double stepDb)
{
    std::vector<float> x = sig::sine (200.0, low, 1.0, kSr);
    const double k = std::pow (10.0, stepDb / 20.0);
    for (size_t i = kStep; i < x.size (); ++i)
        x[i] = (float)(x[i] * k);
    return x;
}
} // namespace

TEST (positive_ratio_raises_attacks)
{
    const auto x = step (0.03, 20.0);
    Transient t = make (0.5);
    const auto y = sig::run (t, x);
    const double in = sig::peak (x, kStep, kStep + 480) / sig::peak (x, kStep + 14400, kStep + 19200);
    const double out = sig::peak (y, kStep, kStep + 480) / sig::peak (y, kStep + 14400, kStep + 19200);
    std::printf ("    the first 10 ms over the held level: %+.1f dB in, %+.1f dB out\n", sig::db (in), sig::db (out));
    CHECK (sig::db (out) > sig::db (in) + 4.0, "the attack raised");
    Transient n = make (-0.5);
    const auto z = sig::run (n, x);
    const double neg = sig::peak (z, kStep, kStep + 480) / sig::peak (z, kStep + 14400, kStep + 19200);
    CHECK (sig::db (neg) < sig::db (in) - 2.0, "a negative Ratio softens it: %+.1f dB", sig::db (neg));
}

TEST (ratio_one_doubles_a_peak_over_the_average)
{
    // TransMod's example: at +1.00 a peak 10 dB above the average comes out 20 dB above it; here a 10 dB
    // jump: right at the jump the gain is about +10 dB
    const auto x = step (0.02, 10.0);
    Transient t = make (1.0);
    const auto y = sig::run (t, x);
    const double gain = sig::db (sig::peak (y, kStep, kStep + 240) / sig::peak (x, kStep, kStep + 240));
    std::printf ("    the gain on the jump's first 5 ms: %+.1f dB\n", gain);
    CHECK (gain > 6.0 && gain < 11.0, "about +10 dB (the average starts rising at once): %+.1f", gain);
}

TEST (held_level_is_left_alone)
{
    Transient t = make (0.8);
    const auto x = sig::sine (200.0, 0.2, 1.0, kSr);
    const auto y = sig::run (t, x);
    const double d = sig::db (sig::rms (y, 24000, 48000) / sig::rms (x, 24000, 48000));
    CHECK (std::fabs (d) < 0.2, "a steady tone keeps its level: %+.2f dB", d);
}

TEST (threshold_gain_and_output)
{
    // a jump that stays under the threshold is not changed
    const auto x = step (0.001, 20.0); // -60 .. -40 dBFS
    Transient t = make (1.0);
    t.setThresholdDb (-30.0);
    const auto y = sig::run (t, x);
    double err = 0.0;
    for (size_t i = 0; i < x.size (); ++i)
        err = std::max (err, (double)std::fabs (y[i] - x[i]));
    CHECK (err < 1e-5, "under the threshold: untouched (%.2g)", err);
    // Gain and Output scale the signal (a steady tone, no transient)
    Transient g = make (0.0);
    g.setGainDb (-8.49);
    g.setOutputDb (3.0);
    const auto s = sig::sine (200.0, 0.2, 0.5, kSr);
    const auto z = sig::run (g, s);
    const double d = sig::db (sig::rms (z, 4800, 24000) / sig::rms (s, 4800, 24000));
    CHECK (std::fabs (d - (-5.49)) < 0.05, "Gain -8.49 and Output +3: %+.2f dB", d);
}

TEST (overdrive_saturates_the_attack)
{
    const auto x = step (0.05, 15.0);
    Transient a = make (0.5), b = make (0.5);
    b.setOverdrive (1.0);
    const auto ya = sig::run (a, x), yb = sig::run (b, x);
    // the third harmonic of 200 Hz on the attack
    const double ha = sig::amplitude (ya, 600.0, kSr, kStep, kStep + 1200), hb = sig::amplitude (yb, 600.0, kSr, kStep, kStep + 1200);
    const double fa = sig::amplitude (ya, 200.0, kSr, kStep, kStep + 1200);
    std::printf ("    third harmonic on the attack: %.1f dB (clean) vs %.1f dB (overdrive), under the fundamental\n", sig::db (ha / fa),
                 sig::db (hb / fa));
    CHECK (hb > 3.0 * ha, "overdrive adds harmonics to the attack");
    // and not to the held part
    const double sa = sig::amplitude (ya, 600.0, kSr, kStep + 19200, kStep + 24000), sb = sig::amplitude (yb, 600.0, kSr, kStep + 19200, kStep + 24000);
    CHECK (sb < sa + 1e-4, "the held part stays clean");
}

TEST (finite_everywhere)
{
    std::mt19937 rng (12);
    std::uniform_real_distribution<double> u (0.0, 1.0);
    Transient t = make (0.5);
    bool ok = true;
    for (int round = 0; round < 16; ++round)
    {
        t.setGainDb (u (rng) * 48.0 - 24.0);
        t.setThresholdDb (-80.0 + u (rng) * 80.0);
        t.setDeadbandDb (u (rng) * 20.0);
        t.setRatio (u (rng) * 2.0 - 1.0);
        t.setOvershootMs (0.1 + u (rng) * 200.0);
        t.setRiseMs (0.01 + u (rng) * 50.0);
        t.setRecoveryMs (1.0 + u (rng) * 999.0);
        t.setOverdrive (u (rng));
        t.setOutputDb (u (rng) * 48.0 - 24.0);
        auto x = sig::noise (u (rng) < 0.3 ? 3.0 : 0.1, 0.2, kSr, (unsigned)round);
        if (round % 4 == 0)
            std::fill (x.begin (), x.end (), 0.0f);
        ok = ok && sig::finite (sig::run (t, x, 1 + (int)(u (rng) * 600)));
    }
    CHECK (ok, "finite with random settings, loud input and silence");
}

DETONATR_TEST_MAIN
