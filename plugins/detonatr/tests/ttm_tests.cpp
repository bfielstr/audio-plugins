// The Comp stages: multiband upward and downward compression to a target (Pro-C 3's TTM style).
#include "Harness.h"
#include "Signals.h"
#include "Ttm.h"

#include <cmath>
#include <cstdio>
#include <random>

using namespace detonatr;

namespace {
constexpr double kSr = 48000.0;

Ttm make (double ratio, double thresholdDb = -20.0)
{
    Ttm t;
    t.setThresholdDb (thresholdDb);
    t.setAutoThreshold (false);
    t.setRatio (ratio);
    t.setAttackMs (10.0);
    t.setReleaseMs (100.0);
    t.setAutoRelease (false);
    t.setKneeDb (0.0);
    t.setRangeDb (30.0);
    t.setHoldMs (0.0);
    t.setAutoGain (false);
    t.setDryDb (-60.0);
    t.setCrossovers (150.0, 2500.0);
    t.setOutputDb (0.0);
    t.prepare (kSr, 256);
    return t;
}

// a sine's level as the stage measures it (its RMS), dB
double sineDb (double amp) { return sig::db (amp / std::sqrt (2.0)); }
double ampOf (double rmsDb) { return std::pow (10.0, rmsDb / 20.0) * std::sqrt (2.0); }
} // namespace

TEST (quiet_band_up_loud_band_down)
{
    // a quiet low tone (-40 dB) and a loud high one (-6 dB), the target -20 dB at 4:1 (and a mid tone
    // at the target, so the mid band is not raised and its edge does not add to the high tone)
    auto x = sig::sine (60.0, ampOf (-40.0), 2.0, kSr);
    sig::add (x, sig::sine (6000.0, ampOf (-6.0), 2.0, kSr));
    sig::add (x, sig::sine (700.0, ampOf (-20.0), 2.0, kSr));
    Ttm t = make (4.0);
    const auto y = sig::run (t, x);
    const size_t a = 72000, b = 96000;
    const double low = sineDb (sig::amplitude (y, 60.0, kSr, a, b)), high = sineDb (sig::amplitude (y, 6000.0, kSr, a, b));
    std::printf ("    low band -40 -> %.1f dB, high band -6 -> %.1f dB (target -20, 4:1: -25 and -16.5)\n", low, high);
    CHECK (std::fabs (low - (-25.0)) < 1.0, "the quiet band brought up towards the target: %.1f dB", low);
    CHECK (std::fabs (high - (-16.5)) < 1.0, "the loud band brought down towards it: %.1f dB", high);
    for (int k = 0; k < 3; ++k)
        std::printf ("    band %d: level %.1f, target %.1f, gain %+.1f dB\n", k, t.meter (k).levelDb, t.meter (k).targetDb, t.meter (k).gainDb);
}

TEST (bands_sum_flat)
{
    // at 1:1 nothing changes: the three bands add up to the input (an all-pass of it)
    for (double hz : {50.0, 150.0, 700.0, 2500.0, 9000.0})
    {
        Ttm t = make (1.0);
        const auto x = sig::sine (hz, 0.3, 0.5, kSr);
        const auto y = sig::run (t, x);
        const double d = sig::db (sig::amplitude (y, hz, kSr, 12000, 24000) / 0.3);
        CHECK (std::fabs (d) < 0.05, "%.0f Hz: %+.3f dB", hz, d);
    }
}

TEST (range_and_silence)
{
    // a band 40 dB under the target is raised by Range at most; silence is never raised
    auto x = sig::sine (1000.0, ampOf (-60.0 + 15.0), 1.5, kSr); // -45 dB
    Ttm t = make (20.0, -5.0);
    t.setRangeDb (12.0);
    const auto y = sig::run (t, x);
    const double d = sig::db (sig::amplitude (y, 1000.0, kSr, 48000, 72000) / sig::amplitude (x, 1000.0, kSr, 48000, 72000));
    CHECK (std::fabs (d - 12.0) < 0.3, "raised by the Range: %+.1f dB", d);
    Ttm s = make (20.0, -5.0);
    const auto q = sig::noise (1e-5, 1.0, kSr, 3); // -100 dBFS
    const auto z = sig::run (s, q);
    CHECK (sig::rms (z, 24000, 48000) < 2.0 * sig::rms (q, 24000, 48000), "a noise floor stays down");
}

TEST (knee_blends_the_two)
{
    // 4 dB under the target: knee 0 corrects it fully (3 dB at 4:1), a 24 dB knee hardly
    auto gainFor = [] (double knee) {
        Ttm t = make (4.0);
        t.setKneeDb (knee);
        t.setCrossovers (50.0, 10000.0); // far from the tone: the other bands get none of it
        const auto x = sig::sine (700.0, ampOf (-24.0), 1.0, kSr);
        const auto y = sig::run (t, x);
        return sig::db (sig::amplitude (y, 700.0, kSr, 24000, 48000) / sig::amplitude (x, 700.0, kSr, 24000, 48000));
    };
    const double hard = gainFor (0.0), soft = gainFor (24.0);
    std::printf ("    4 dB under the target: %+.2f dB (knee 0), %+.2f dB (knee 24)\n", hard, soft);
    CHECK (std::fabs (hard - 3.0) < 0.2, "knee 0: the full correction: %+.2f", hard);
    CHECK (soft > 0.05 && soft < 0.6, "wide knee: a little: %+.2f", soft);
}

TEST (auto_threshold_and_auto_gain)
{
    // with Auto, a steady band is its own target: no change; a hit (a jump) is pulled back towards it
    Ttm t = make (10.0);
    t.setAutoThreshold (true);
    t.setAutoGain (true);
    t.setAttackMs (5.0);
    std::vector<float> x = sig::sine (1000.0, 0.05, 4.0, kSr);
    const auto y = sig::run (t, x);
    const double held = sig::db (sig::amplitude (y, 1000.0, kSr, 144000, 168000) / 0.05);
    std::printf ("    a held tone after 3 s: %+.2f dB\n", held);
    CHECK (std::fabs (held) < 1.0, "a steady band left about as it is: %+.2f dB", held);

    // auto gain keeps a dynamic signal about as loud
    Ttm a = make (10.0);
    a.setAutoThreshold (true);
    a.setAutoGain (true);
    std::vector<float> h (4 * 48000);
    std::mt19937 rng (5);
    std::normal_distribution<float> nd (0.0f, 1.0f);
    for (size_t i = 0; i < h.size (); ++i)
    {
        const double tt = (double)(i % 24000) / kSr;
        h[i] = (float)(0.3 * std::exp (-tt / 0.1) * nd (rng));
    }
    const auto z = sig::run (a, h);
    const double d = sig::db (sig::rms (z, 96000, h.size ()) / sig::rms (h, 96000, h.size ()));
    std::printf ("    hits through Auto Threshold and Auto Gain: %+.1f dB\n", d);
    CHECK (std::fabs (d) < 4.0, "about as loud: %+.1f dB", d);
}

TEST (finite_everywhere)
{
    std::mt19937 rng (31);
    std::uniform_real_distribution<double> u (0.0, 1.0);
    Ttm t = make (4.0);
    bool ok = true;
    for (int round = 0; round < 16; ++round)
    {
        t.setThresholdDb (-60.0 * u (rng));
        t.setAutoThreshold (u (rng) < 0.5);
        t.setRatio (1.0 + 49.0 * u (rng));
        t.setAttackMs (0.01 + 250.0 * u (rng));
        t.setReleaseMs (10.0 + 2490.0 * u (rng));
        t.setAutoRelease (u (rng) < 0.5);
        t.setKneeDb (48.0 * u (rng));
        t.setRangeDb (60.0 * u (rng));
        t.setHoldMs (500.0 * u (rng));
        t.setAutoGain (u (rng) < 0.5);
        t.setDryDb (-60.0 + 66.0 * u (rng));
        t.setCrossovers (40.0 + 960.0 * u (rng), 500.0 + 11500.0 * u (rng));
        auto x = sig::noise (u (rng) < 0.3 ? 3.0 : 0.1, 0.2, kSr, (unsigned)round);
        if (round % 4 == 0)
            std::fill (x.begin (), x.end (), 0.0f);
        ok = ok && sig::finite (sig::run (t, x, 1 + (int)(u (rng) * 600)));
    }
    CHECK (ok, "finite with random settings, loud input and silence");
}

DETONATR_TEST_MAIN
