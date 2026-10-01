// The Tape stage: two-band warm tape saturation.
#include "Harness.h"
#include "Signals.h"
#include "Tape.h"

#include <cmath>
#include <cstdio>
#include <random>

using namespace detonatr;

namespace {
constexpr double kSr = 48000.0;

Tape make (double drive)
{
    Tape t;
    t.prepare (kSr, 256);
    t.setSplit (200.0);
    for (int b = 0; b < 2; ++b)
    {
        t.setDriveDb (b, drive);
        t.setMix (b, 1.0);
        t.setDynamics (b, 0.0);
        t.setLevelDb (b, 0.0);
    }
    return t;
}

const double kAmp18 = std::pow (10.0, -18.0 / 20.0) * std::sqrt (2.0); // a -18 dBFS (RMS) sine
} // namespace

TEST (adds_harmonics)
{
    for (double hz : {80.0, 1000.0})
    {
        double last = -200.0;
        for (double drive : {0.0, 6.0, 18.0})
        {
            Tape t = make (drive);
            const auto x = sig::sine (hz, kAmp18, 1.0, kSr);
            const auto y = sig::run (t, x);
            const size_t a = 24000, b = 48000;
            const double f = sig::amplitude (y, hz, kSr, a, b);
            const double h2 = sig::amplitude (y, 2 * hz, kSr, a, b), h3 = sig::amplitude (y, 3 * hz, kSr, a, b);
            const double thd = sig::db (std::sqrt (h2 * h2 + h3 * h3) / f);
            std::printf ("    %.0f Hz, drive %2.0f dB: 2nd %.1f dB, 3rd %.1f dB under the fundamental\n", hz, drive, sig::db (h2 / f),
                         sig::db (h3 / f));
            CHECK (thd > last + 3.0, "more drive, more harmonics (%.1f after %.1f dB)", thd, last);
            if (drive > 0.0)
                CHECK (h2 > 1e-3 * f, "even harmonics too (the asymmetry): %.1f dB", sig::db (h2 / f));
            last = thd;
        }
    }
}

TEST (level_matched)
{
    for (double hz : {80.0, 1000.0})
        for (double drive : {0.0, 6.0, 12.0, 24.0})
        {
            Tape t = make (drive);
            const auto x = sig::sine (hz, kAmp18, 1.0, kSr);
            const auto y = sig::run (t, x);
            const double d = sig::db (sig::rms (y, 24000, 48000) / sig::rms (x, 24000, 48000));
            CHECK (std::fabs (d) < 1.0, "%.0f Hz, drive %.0f dB: %+.2f dB", hz, drive, d);
        }
}

TEST (split_adds_back_up)
{
    // the linear-phase split: lows and highs add back to the input (dry, through the oversampler)
    Tape t = make (0.0);
    t.setMix (0, 0.0);
    t.setMix (1, 0.0);
    // tones from 30 Hz to 18 kHz (the oversampler's filters pass up to 20 kHz)
    std::vector<float> x ((size_t)kSr, 0.0f);
    for (double hz : {30.0, 120.0, 199.0, 203.0, 1000.0, 4700.0, 11000.0, 18000.0})
        sig::add (x, sig::sine (hz, 0.05, 1.0, kSr, hz * 0.001));
    const auto y = sig::run (t, x, 333);
    const int lat = t.latency ();
    double e = 0.0, ref = 0.0;
    for (size_t i = 24000; i < x.size (); ++i)
    {
        const double d = y[i] - x[i - (size_t)lat];
        e += d * d;
        ref += (double)x[i] * x[i];
    }
    const double err = 10.0 * std::log10 (e / ref);
    std::printf ("    latency %d samples, the difference %.1f dB under the input\n", lat, err);
    CHECK (err < -40.0, "the bands add back to the delayed input: %.1f dB", err);
    CHECK (std::fabs (t.lowResponse (200.0) - 0.5) < 0.02, "the lows are 6 dB down at the split: %.3f", t.lowResponse (200.0));
}

TEST (constant_latency)
{
    for (double sr : {44100.0, 48000.0, 96000.0})
    {
        Tape t;
        t.prepare (sr, 512);
        const int l0 = t.latency ();
        bool same = true;
        for (double split : {80.0, 200.0, 650.0, 1000.0})
        {
            t.setSplit (split);
            same = same && t.latency () == l0;
            // the impulse lands at the latency, whatever the split
            t.setDriveDb (0, 0.0);
            t.setDriveDb (1, 0.0);
            t.setMix (0, 0.0);
            t.setMix (1, 0.0);
            t.reset ();
            std::vector<float> x (8192, 0.0f);
            x[100] = 1.0f;
            const int block = 512;
            std::vector<float> y (x.size ()), l (block), r (block);
            for (size_t a = 0; a < x.size (); a += block)
            {
                std::copy (x.begin () + (ptrdiff_t)a, x.begin () + (ptrdiff_t)a + block, l.begin ());
                std::copy (l.begin (), l.end (), r.begin ());
                t.process (l.data (), r.data (), block);
                std::copy (l.begin (), l.end (), y.begin () + (ptrdiff_t)a);
            }
            size_t at = 0;
            for (size_t i = 0; i < y.size (); ++i)
                if (std::fabs (y[i]) > std::fabs (y[at]))
                    at = i;
            same = same && (int)at == 100 + l0;
        }
        std::printf ("    %.0f Hz: %d samples (%.1f ms)\n", sr, l0, 1000.0 * l0 / sr);
        CHECK (same, "%.0f Hz: the same latency at every split", sr);
    }
}

TEST (band_levels_and_dynamics)
{
    Tape t = make (6.0);
    t.setLevelDb (0, 6.0);
    auto x = sig::sine (60.0, kAmp18, 1.0, kSr);
    sig::add (x, sig::sine (3000.0, kAmp18, 1.0, kSr));
    const auto y = sig::run (t, x);
    const double lo = sig::db (sig::amplitude (y, 60.0, kSr, 24000, 48000) / sig::amplitude (x, 60.0, kSr, 24000, 48000));
    const double hi = sig::db (sig::amplitude (y, 3000.0, kSr, 24000, 48000) / sig::amplitude (x, 3000.0, kSr, 24000, 48000));
    std::printf ("    low Level +6: low %+.2f dB, high %+.2f dB\n", lo, hi);
    CHECK (std::fabs (lo - 6.0) < 1.0 && std::fabs (hi) < 1.0, "each band its own level");
    // Dynamics: positive drives a loud part harder (louder against a quiet one)
    auto ratioFor = [] (double dyn) {
        Tape d = make (6.0);
        d.setDynamics (0, dyn);
        d.setDynamics (1, dyn);
        auto q = sig::sine (1000.0, 0.02, 1.0, kSr);
        for (size_t i = 24000; i < q.size (); ++i)
            q[i] *= 10.0f;
        const auto z = sig::run (d, q);
        return sig::db (sig::rms (z, 36000, 48000) / sig::rms (z, 12000, 24000));
    };
    const double neutral = ratioFor (0.0), up = ratioFor (1.0), down = ratioFor (-1.0);
    std::printf ("    a 20 dB step: %.1f dB (Dynamics 0), %.1f (+100 %%), %.1f (-100 %%)\n", neutral, up, down);
    CHECK (up > neutral + 2.0 && down < neutral - 2.0, "Dynamics expands (+) or compresses (-)");
}

TEST (finite_everywhere)
{
    std::mt19937 rng (41);
    std::uniform_real_distribution<double> u (0.0, 1.0);
    Tape t = make (6.0);
    bool ok = true;
    for (int round = 0; round < 12; ++round)
    {
        t.setSplit (80.0 + 920.0 * u (rng));
        for (int b = 0; b < 2; ++b)
        {
            t.setDriveDb (b, 36.0 * u (rng));
            t.setMix (b, u (rng));
            t.setDynamics (b, 2.0 * u (rng) - 1.0);
            t.setLevelDb (b, 48.0 * u (rng) - 24.0);
        }
        auto x = sig::noise (u (rng) < 0.3 ? 5.0 : 0.1, 0.2, kSr, (unsigned)round);
        if (round % 4 == 0)
            std::fill (x.begin (), x.end (), 0.0f);
        ok = ok && sig::finite (sig::run (t, x, 1 + (int)(u (rng) * 600)));
    }
    CHECK (ok, "finite with random settings, loud input and silence");
}

DETONATR_TEST_MAIN
