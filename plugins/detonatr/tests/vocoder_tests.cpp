// The Vocoder stage: the sound vocoding itself.
#include "Harness.h"
#include "Signals.h"
#include "Vocoder.h"

#include <cmath>
#include <cstdio>
#include <random>

using namespace detonatr;

namespace {
constexpr double kSr = 48000.0;

Vocoder make (double ratio = 1.0)
{
    Vocoder v;
    v.prepare (kSr, 256);
    v.setRatio (ratio);
    return v;
}
} // namespace

TEST (bands_are_log_spaced)
{
    Vocoder v = make ();
    CHECK (v.bands () == 32, "32 bands by default: %d", v.bands ());
    CHECK (std::fabs (v.centre (0) - 40.0) < 1e-6 && std::fabs (v.centre (31) - 16000.0) < 1e-3, "40 Hz .. 16 kHz: %.1f .. %.1f",
           v.centre (0), v.centre (31));
    const double r = v.centre (1) / v.centre (0);
    bool even = true;
    for (int b = 1; b < 31; ++b)
        even = even && std::fabs (v.centre (b + 1) / v.centre (b) - r) < 1e-9;
    CHECK (even, "the same ratio between neighbours");
    v.setBands (100);
    v.setRange (100.0, 8000.0);
    CHECK (v.bands () == 100 && std::fabs (v.centre (99) - 8000.0) < 1e-3, "100 bands, 100 Hz .. 8 kHz");
}

TEST (loud_bands_come_out_louder)
{
    // a loud 300 Hz tone and a 20 dB quieter 3 kHz one: vocoded, the gap widens (the band-wise square)
    Vocoder v = make (1.0);
    auto x = sig::sine (300.0, 0.3, 2.0, kSr);
    sig::add (x, sig::sine (3000.0, 0.03, 2.0, kSr));
    const auto y = sig::run (v, x);
    const size_t a = 48000, b = 96000;
    const double gapIn = sig::db (sig::amplitude (x, 300.0, kSr, a, b) / sig::amplitude (x, 3000.0, kSr, a, b));
    const double gapOut = sig::db (sig::amplitude (y, 300.0, kSr, a, b) / sig::amplitude (y, 3000.0, kSr, a, b));
    std::printf ("    300 Hz over 3 kHz: %.1f dB in, %.1f dB out\n", gapIn, gapOut);
    CHECK (gapOut > gapIn + 10.0, "the loud band emphasised: %.1f -> %.1f dB", gapIn, gapOut);
}

TEST (attacks_come_out_louder)
{
    // a 1 kHz tone starting after silence: its start comes out louder than its body
    Vocoder v = make (1.0);
    std::vector<float> x ((size_t)kSr, 0.0f);
    const auto t = sig::sine (1000.0, 0.25, 0.5, kSr);
    std::copy (t.begin (), t.end (), x.begin () + 24000);
    const auto y = sig::run (v, x);
    const double start = sig::peak (y, 24000, 24000 + 960), body = sig::peak (y, 24000 + 19200, 24000 + 24000);
    std::printf ("    the first 20 ms %.1f dB over the body\n", sig::db (start / body));
    CHECK (start > body * 1.4, "the attack emphasised: %.2f vs %.2f", start, body);
    CHECK (start < 0.25 * 3.0, "and bounded (+8 dB at most on a band): %.2f", start);
}

TEST (level_matched)
{
    // white noise and a held chord come out about as loud as they went in
    for (int k = 0; k < 2; ++k)
    {
        Vocoder v = make (1.0);
        std::vector<float> x;
        if (k == 0)
            x = sig::noise (0.1, 2.0, kSr, 3);
        else
        {
            x = sig::sine (110.0, 0.2, 2.0, kSr);
            sig::add (x, sig::sine (440.0, 0.1, 2.0, kSr));
            sig::add (x, sig::sine (1320.0, 0.05, 2.0, kSr));
        }
        const auto y = sig::run (v, x);
        const double d = sig::db (sig::rms (y, 48000, 96000) / sig::rms (x, 48000, 96000));
        std::printf ("    %s: %+.1f dB\n", k == 0 ? "noise" : "chord", d);
        CHECK (std::fabs (d) < 3.0, "within 3 dB: %+.1f", d);
    }
}

TEST (ratio_blends)
{
    const auto x = sig::noise (0.1, 0.5, kSr, 5);
    Vocoder v0 = make (0.0);
    const auto y0 = sig::run (v0, x);
    double err = 0.0;
    for (size_t i = 0; i < x.size (); ++i)
        err = std::max (err, (double)std::fabs (y0[i] - x[i]));
    CHECK (err < 1e-6, "Ratio 0: the input as it is (%.2g)", err);
    Vocoder v1 = make (1.0), vh = make (0.46);
    const auto y1 = sig::run (v1, x), yh = sig::run (vh, x);
    double e2 = 0.0;
    for (size_t i = 0; i < x.size (); ++i)
        e2 = std::max (e2, (double)std::fabs (yh[i] - (0.54f * x[i] + 0.46f * y1[i])));
    CHECK (e2 < 1e-4, "Ratio 0.46: 54 %% input, 46 %% vocoded (%.2g)", e2);
}

TEST (finite_everywhere)
{
    std::mt19937 rng (9);
    std::uniform_real_distribution<double> u (0.0, 1.0);
    Vocoder v = make (0.5);
    bool ok = true;
    for (int round = 0; round < 12; ++round)
    {
        v.setBands (8 + (int)(u (rng) * 92));
        v.setRange (20.0 + u (rng) * 900.0, 1000.0 + u (rng) * 19000.0);
        v.setOrder (1 + (int)(u (rng) * 3));
        v.setAttack (0.1 + u (rng) * 100.0);
        v.setRelease (5.0 + u (rng) * 900.0);
        v.setRatio (u (rng));
        auto x = sig::noise (u (rng) < 0.3 ? 2.0 : 0.1, 0.2, kSr, (unsigned)round);
        if (round % 4 == 0)
            std::fill (x.begin (), x.end (), 0.0f);
        ok = ok && sig::finite (sig::run (v, x, 1 + (int)(u (rng) * 600)));
    }
    CHECK (ok, "finite with random settings, loud input and silence");
}

DETONATR_TEST_MAIN
