// The Limiter stages: the ceiling holds (sample peak, and true peak with True Peak on).
#include "Harness.h"
#include "Limiter.h"
#include "Signals.h"

#include <cmath>
#include <cstdio>
#include <random>

using namespace detonatr;

namespace {
constexpr double kSr = 48000.0;

Limiter make (double gainDb, double ceilingDb, bool tp, double look = 1.0, double attack = 3.0, double release = 50.0, double link = 0.0)
{
    Limiter l;
    l.setGainDb (gainDb);
    l.setCeilingDb (ceilingDb);
    l.setTruePeak (tp);
    l.setLookaheadMs (look);
    l.setAttackMs (attack);
    l.setReleaseMs (release);
    l.setLink (link);
    l.prepare (kSr, 256);
    return l;
}

// a hard test: noise with loud bursts, a kick-like low thump and a tone at a quarter of the rate with
// its peaks between the samples
std::vector<float> program (unsigned seed)
{
    auto x = sig::noise (0.15, 3.0, kSr, seed);
    std::mt19937 rng (seed);
    std::uniform_real_distribution<float> u (0.0f, 1.0f);
    for (size_t i = 0; i < x.size (); ++i)
    {
        const double t = (double)(i % 24000) / kSr;
        x[i] += (float)(0.9 * std::exp (-t / 0.08) * std::sin (2.0 * sig::kPi * 55.0 * t));
        if ((i / 4800) % 7 == 3)
            x[i] += (float)(0.6 * std::sin (2.0 * sig::kPi * 12000.0 * (double)i / kSr + 0.785398)); // peaks between samples
        if (u (rng) < 0.0005f)
            x[i] += u (rng) < 0.5f ? 1.5f : -1.5f;
    }
    return x;
}

// the same, band-limited to 18 kHz (as music is): a windowed-sinc low-pass, 127 taps
std::vector<float> bandLimited (std::vector<float> x)
{
    constexpr int H = 63;
    std::vector<double> h (2 * H + 1);
    const double fc = 18000.0 / kSr;
    for (int j = -H; j <= H; ++j)
    {
        const double s = j == 0 ? 2.0 * fc : std::sin (2.0 * sig::kPi * fc * j) / (sig::kPi * j);
        h[(size_t)(j + H)] = s * (0.42 + 0.5 * std::cos (sig::kPi * j / (H + 1)) + 0.08 * std::cos (2.0 * sig::kPi * j / (H + 1)));
    }
    std::vector<float> y (x.size (), 0.0f);
    for (size_t i = H; i + H < x.size (); ++i)
    {
        double acc = 0.0;
        for (int j = -H; j <= H; ++j)
            acc += h[(size_t)(j + H)] * x[i + (size_t)j];
        y[i] = (float)acc;
    }
    return y;
}

// the true peak: 8x through a long windowed sinc
double truePeak (const std::vector<float>& y)
{
    constexpr int H = 64, K = 8;
    double pk = 0.0;
    for (size_t m = H; m + H < y.size (); ++m)
        for (int f = 0; f < K; ++f)
        {
            double acc = 0.0;
            for (int j = -H + 1; j <= H; ++j)
            {
                const double t = (double)j - (double)f / K;
                const double sinc = std::fabs (t) < 1e-12 ? 1.0 : std::sin (sig::kPi * t) / (sig::kPi * t);
                const double r = t / (H + 0.5);
                const double w = std::fabs (r) >= 1.0 ? 0.0 : 0.42 + 0.5 * std::cos (sig::kPi * r) + 0.08 * std::cos (2.0 * sig::kPi * r);
                acc += sinc * w * y[m + (size_t)j];
            }
            pk = std::max (pk, std::fabs (acc));
        }
    return pk;
}
} // namespace

TEST (sample_peak_never_over_the_ceiling)
{
    struct Case
    {
        double gain, ceiling, look, attack, release, link;
    } cases[] = {{12.0, -1.0, 1.0, 3.0, 50.0, 0.0}, {24.0, -6.0, 0.1, 0.1, 5.0, 1.0}, {6.0, 0.0, 5.0, 100.0, 2000.0, 0.5},
                 {30.0, -0.3, 2.0, 1.0, 1.0, 0.0}};
    for (const auto& c : cases)
    {
        Limiter l = make (c.gain, c.ceiling, false, c.look, c.attack, c.release, c.link);
        std::vector<float> r;
        const auto y = sig::run (l, program (3), 256, &r);
        const double pk = std::max (sig::peak (y), sig::peak (r)), ceil = std::pow (10.0, c.ceiling / 20.0);
        std::printf ("    +%.0f dB into %.1f dB (look-ahead %.1f ms): peak %.4f dB, %lld clamps\n", c.gain, c.ceiling, c.look, sig::db (pk),
                     (long long)l.clamps ());
        CHECK (pk <= ceil * (1.0 + 1e-6), "the ceiling holds: %.4f dB over %.1f dB", sig::db (pk), c.ceiling);
        CHECK (l.clamps () == 0, "without the last clamp: %lld", (long long)l.clamps ());
        CHECK (sig::finite (y), "finite");
    }
}

TEST (true_peak_holds_between_samples)
{
    // (music is band-limited: near the Nyquist frequency the peaks between samples are ill-defined)
    const auto x = bandLimited (program (5));
    Limiter on = make (12.0, 0.0, true), off = make (12.0, 0.0, false);
    const auto yOn = sig::run (on, x), yOff = sig::run (off, x);
    const double tpOn = sig::db (truePeak (yOn)), tpOff = sig::db (truePeak (yOff));
    std::printf ("    true peak with True Peak on %.3f dBTP, off %.3f dBTP (ceiling 0.0)\n", tpOn, tpOff);
    CHECK (tpOn <= 0.05, "True Peak keeps it under 0 dBTP (within 0.05 dB): %.3f", tpOn);
    CHECK (tpOff > tpOn + 0.3, "without it the peaks between samples go over: %.3f", tpOff);
    CHECK (on.clamps () == 0, "never clamped: %lld", (long long)on.clamps ());
}

TEST (constant_latency_and_transparent_under_the_ceiling)
{
    for (double sr : {44100.0, 48000.0, 96000.0})
    {
        Limiter l;
        l.prepare (sr, 512);
        const int want = Limiter::kHalfTaps + (int)std::lround (0.005 * sr) - 1;
        CHECK (l.latency () == want, "%.0f Hz: %d samples", sr, l.latency ());
    }
    for (double look : {0.1, 1.0, 5.0})
    {
        Limiter l = make (6.0, 0.0, true, look);
        const auto x = sig::noise (0.05, 0.5, kSr, 7); // peaks well under 0 dB at +6 dB
        const auto y = sig::run (l, x, 100);
        const int lat = l.latency ();
        double err = 0.0;
        for (size_t i = (size_t)lat; i < x.size (); ++i)
            err = std::max (err, std::fabs ((double)y[i] - std::pow (10.0, 0.3) * x[i - (size_t)lat]));
        CHECK (err < 1e-6, "look-ahead %.1f ms: the input, +6 dB, delayed by the same latency (%.2g)", look, err);
    }
}

TEST (reduces_loud_material)
{
    // a -6 dBFS sine, +12 dB into -1 dB: 7 dB over the ceiling (the true-peak margin 0.1 dB more)
    Limiter l = make (12.0, -1.0, true);
    const auto x = sig::sine (100.0, 0.5, 0.5, kSr);
    sig::run (l, x);
    l.takeReductionDb (); // settled: from here on
    const auto y = sig::run (l, x);
    const double r = l.takeReductionDb ();
    std::printf ("    a -6 dBFS sine, +12 dB into -1 dB: %.1f dB of reduction at most, out %.2f dB\n", r, sig::db (sig::peak (y)));
    CHECK (r > 6.9 && r < 9.0, "about 7 dB of reduction: %.1f", r);
    CHECK (sig::db (sig::peak (y)) > -1.3 && sig::db (sig::peak (y)) <= -1.0, "and right under the ceiling");
}

TEST (finite_everywhere)
{
    std::mt19937 rng (21);
    std::uniform_real_distribution<double> u (0.0, 1.0);
    Limiter l = make (6.0, -1.0, true);
    bool ok = true;
    for (int round = 0; round < 12; ++round)
    {
        l.setGainDb (u (rng) * 30.0);
        l.setCeilingDb (-30.0 * u (rng));
        l.setLookaheadMs (0.1 + u (rng) * 4.9);
        l.setAttackMs (0.1 + u (rng) * 100.0);
        l.setReleaseMs (1.0 + u (rng) * 1999.0);
        l.setLink (u (rng));
        l.setTruePeak (u (rng) < 0.5);
        auto x = sig::noise (u (rng) < 0.3 ? 10.0 : 0.1, 0.2, kSr, (unsigned)round);
        if (round % 4 == 0)
            std::fill (x.begin (), x.end (), 0.0f);
        ok = ok && sig::finite (sig::run (l, x, 1 + (int)(u (rng) * 600)));
    }
    CHECK (ok, "finite with random settings, loud input and silence");
}

DETONATR_TEST_MAIN
