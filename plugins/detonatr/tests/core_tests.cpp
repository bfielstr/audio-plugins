// Detonatr's engine: the chain of stages, its order, its latency, dry/wet and the defaults on an impact.
#include "Engine.h"
#include "Harness.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <random>
#include <string>
#include <vector>

using namespace detonatr;

namespace {
constexpr double kSr = 48000.0;
constexpr double kPi = 3.14159265358979323846;
constexpr int kBlock = 256;

// a noisy impact with a low tone in it (like a raw explosion recording): a hit every `every` seconds
std::vector<float> impact (double seconds, double every = 0.6, unsigned seed = 7)
{
    std::mt19937 rng (seed);
    std::normal_distribution<float> noise (0.0f, 1.0f);
    std::vector<float> x ((size_t)(seconds * kSr));
    const int period = (int)(every * kSr);
    for (size_t i = 0; i < x.size (); ++i)
    {
        const double t = (double)(i % (size_t)period) / kSr;
        const double env = std::exp (-t / 0.18) * std::min (1.0, t / 0.002);
        x[i] = (float)(env * (0.3 * noise (rng) + 0.4 * std::sin (2 * kPi * 70.0 * t)));
    }
    return x;
}

std::unique_ptr<Engine> engine ()
{
    auto e = std::make_unique<Engine> ();
    e->prepare (kSr, kBlock);
    return e;
}

void set (Engine& e, uint32_t id, double v) { e.setParam (id, v); }

// runs a mono signal through as stereo, in blocks; returns the left output
std::vector<float> run (Engine& e, const std::vector<float>& x, int block = kBlock)
{
    std::vector<float> out (x.size ()), l (block), r (block);
    for (size_t a = 0; a < x.size (); a += (size_t)block)
    {
        const int n = (int)std::min ((size_t)block, x.size () - a);
        std::copy (x.begin () + (ptrdiff_t)a, x.begin () + (ptrdiff_t)a + n, l.begin ());
        std::copy (x.begin () + (ptrdiff_t)a, x.begin () + (ptrdiff_t)a + n, r.begin ());
        e.process (l.data (), r.data (), l.data (), r.data (), n);
        std::copy (l.begin (), l.begin () + n, out.begin () + (ptrdiff_t)a);
    }
    return out;
}

double maxDelayedError (const std::vector<float>& in, const std::vector<float>& out, int latency)
{
    double err = 0.0;
    for (size_t i = (size_t)latency; i < in.size (); ++i)
        err = std::max (err, (double)std::fabs (out[i] - in[i - (size_t)latency]));
    return err;
}

void allOff (Engine& e)
{
    set (e, kCleanOn, 0.0);
    set (e, kToneOn, 0.0);
    set (e, kMultibandOn, 0.0);
    set (e, kTransientOn, 0.0);
    set (e, kTailBase + pk::kTailOn, 0.0);
}

void setOrder (Engine& e, std::initializer_list<int> stages)
{
    uint32_t i = 0;
    for (int s : stages)
        set (e, kOrderBase + i++, s);
}
} // namespace

TEST (table_is_consistent)
{
    const auto& t = paramTable ();
    CHECK (t.size () == kNumParams, "%u entries, %u ids", t.size (), (unsigned)kNumParams);
    CHECK (kMb2Base == kTailExt2Base + pk::kTailExt2Fields &&
               std::string (t.info (kTailExt2Base + pk::kTailExt2Advanced).name) == "Saturator Gently Advanced" &&
               t.info (kTailExt2Base + pk::kTailExt2Threshold).def == -18.0 && t.info (kTailExt2Base + pk::kTailExt2Advanced).def == 0.0,
           "Gently's Advanced block (the end saturator's), then the Multiband stage's second block");
    bool ids = true;
    for (uint32_t i = 0; i < t.size (); ++i)
        ids = ids && t.info (i).id == i;
    CHECK (ids, "every entry sits at its id");
    CHECK (std::string (t.info (kTailBase + pk::kTailOn).name) == "Saturator", "the saturator block where it belongs");
    CHECK (t.info (kTailBase + pk::kTailOn).def == 1.0 && t.info (kTailBase + pk::kTailDrive).def == 18.0, "the Saturator stage is on, driven");
    CHECK (mbIdAt ((uint32_t)mbBlockOf (multidyn::kSoften)) == multidyn::kSoften && mbIdAt ((uint32_t)mbBlockOf (multidyn::kRmsWindow)) == multidyn::kRmsWindow,
           "Multidyn's later parameters map both ways");
    CHECK (mbBlockOf (multidyn::kSatOn) == -1 && mbBlockOf (multidyn::kSatExtBase) == -1 && mbBlockOf (multidyn::kSatExt2Base) == -1,
           "Multidyn's own saturator is left out");
    for (uint32_t j = 0; j < kMbBlock; ++j)
    {
        const auto& a = t.info (kMbBase + j);
        const auto& b = multidyn::paramTable ().info ((uint32_t)mbIdAt (j));
        CHECK (a.min == b.min && a.max == b.max && a.def == b.def && a.type == b.type, "multiband %u has Multidyn's range", j);
    }
    // the second block, at the end: Slope, Soften Color and the Sub band, in Multidyn's order
    CHECK (kMb2Base == kTailExt2Base + pk::kTailExt2Fields && kNumParams == kMb2Base + kMb2Block &&
               mbParam (multidyn::kXoverSlope) == kMb2Base && mbParam (multidyn::kSubOutput) == kNumParams - 1,
           "the second Multiband block ends the table");
    CHECK (std::string (t.info (mbParam (multidyn::kSubOn)).name) == "Multiband Sub Band" &&
               std::string (t.info (mbParam (multidyn::kXoverSlope)).name) == "Multiband Crossover Slope",
           "named like Multidyn's, with Multiband in front");
    for (uint32_t md = 0; md < multidyn::kNumParams; ++md)
    {
        const int64_t id = detIdOfMd (md);
        if (id < 0)
            continue;
        CHECK (mdIdOf ((uint32_t)id) == (int64_t)md && isMbParam ((uint32_t)id) && mbParam (md) == (uint32_t)id, "Multidyn %u maps both ways", md);
        const auto& a = t.info ((uint32_t)id);
        const auto& b = multidyn::paramTable ().info (md);
        CHECK (a.min == b.min && a.max == b.max && a.def == b.def && a.type == b.type && a.curve == b.curve, "%s has Multidyn's range", a.name);
    }
    CHECK (detIdOfMd (multidyn::kSatOn) == -1 && detIdOfMd (multidyn::kScOn) == (int64_t)(kMbBase + multidyn::kScOn),
           "Multidyn's side-chain On sits in the first block (unused), its saturator not");
}

TEST (multiband_new_parameters_reach_the_stage)
{
    // the Multiband stage alone: the Sub band (80 Hz) compresses a loud 30 Hz tone, and Soften Color on
    // keeps the latency
    auto tone = [] (double hz, double seconds) {
        std::vector<float> x ((size_t)(seconds * kSr));
        for (size_t i = 0; i < x.size (); ++i)
            x[i] = (float)(0.5 * std::sin (2 * kPi * hz * (double)i / kSr));
        return x;
    };
    auto render = [&] (double ratio, bool color, int& latency) {
        auto e = engine ();
        allOff (*e);
        set (*e, kMultibandOn, 1.0);
        set (*e, mbParam (multidyn::kSubOn), 1.0);
        set (*e, mbParam (multidyn::kSubFreq), 80.0);
        set (*e, mbParam (multidyn::kSubThresh), -30.0);
        set (*e, mbParam (multidyn::kSubRatio), ratio);
        set (*e, mbParam (multidyn::kSoftenColor), color ? 1.0 : 0.0);
        e->reset ();
        latency = e->latency ();
        const auto y = run (*e, tone (30.0, 2.0));
        double s = 0.0;
        for (size_t i = 48000; i < 96000; ++i)
            s += (double)y[i] * y[i];
        return 10.0 * std::log10 (s / 48000.0 + 1e-30);
    };
    int l0 = 0, l1 = 0, l2 = 0;
    const double off = render (1.0, false, l0), on = render (8.0, false, l1);
    render (1.0, true, l2);
    std::printf ("    30 Hz through the Multiband stage: the Sub band at 1:1 %.1f dB, at 1:8 %.1f dB\n", off, on);
    CHECK (on < off - 10.0, "the Sub band turns the loud sub down: %.1f vs %.1f dB", on, off);
    CHECK (l0 == l1 && l0 == l2, "the latency stays: %d / %d / %d", l0, l1, l2);
}

TEST (order_resolves)
{
    const int def[kNumStages] = {0, 1, 2, 3, 4};
    Order o = resolveOrder (def);
    CHECK (o.stage[0] == kStageClean && o.stage[4] == kStageSaturator, "the default order");
    const int dup[kNumStages] = {kStageSaturator, kStageSaturator, kStageTone, 9, kStageTone};
    o = resolveOrder (dup);
    // saturator first, tone, then the ones left out in the default order
    CHECK (o.stage[0] == kStageSaturator && o.stage[1] == kStageTone && o.stage[2] == kStageClean && o.stage[3] == kStageMultiband &&
               o.stage[4] == kStageTransient,
           "duplicates run once, the rest after: %d %d %d %d %d", o.stage[0], o.stage[1], o.stage[2], o.stage[3], o.stage[4]);
    bool seen[kNumStages] {};
    for (int s : o.stage)
        seen[s] = true;
    CHECK (seen[0] && seen[1] && seen[2] && seen[3] && seen[4], "every stage runs");
}

TEST (latency_is_constant)
{
    auto e = engine ();
    const int l0 = e->latency ();
    std::printf ("    latency %d samples (%.2f ms)\n", l0, 1000.0 * l0 / kSr);
    allOff (*e);
    CHECK (e->latency () == l0, "off: %d vs %d", e->latency (), l0);
    setOrder (*e, {4, 3, 2, 1, 0});
    CHECK (e->latency () == l0, "reversed: %d vs %d", e->latency (), l0);
}

TEST (all_off_is_a_delay)
{
    auto e = engine ();
    allOff (*e);
    e->reset ();
    const auto x = impact (1.5);
    const auto y = run (*e, x);
    const double err = maxDelayedError (x, y, e->latency ());
    std::printf ("    largest difference from the delayed input %.2g\n", err);
    CHECK (err < 1e-4, "every stage off passes the input, delayed: %.2g", err);
}

TEST (dry_is_lined_up)
{
    auto e = engine ();
    set (*e, kDryWet, 0.0);
    e->reset ();
    const auto x = impact (1.0);
    const auto y = run (*e, x, 173);
    const double err = maxDelayedError (x, y, e->latency ());
    CHECK (err < 1e-6, "dry/wet 0: the input delayed by the latency: %.2g", err);
}

TEST (defaults_on_an_impact)
{
    auto e = engine ();
    const auto x = impact (3.0);
    const auto y = run (*e, x);
    bool finite = true;
    double peak = 0.0, sum = 0.0;
    for (float v : y)
    {
        finite = finite && std::isfinite (v);
        peak = std::max (peak, (double)std::fabs (v));
        sum += (double)v * v;
    }
    const double rms = std::sqrt (sum / (double)y.size ());
    std::printf ("    output peak %.1f dBFS, rms %.1f dBFS\n", 20 * std::log10 (peak + 1e-12), 20 * std::log10 (rms + 1e-12));
    CHECK (finite, "finite");
    CHECK (rms > 1e-3, "sounds: rms %.2g", rms);
    CHECK (peak < 4.0, "not wild: peak %.2f", peak);
}

// spectral flatness (0: tonal .. 1: noise) of the loudest 4096-sample frames, averaged
static double flatness (const std::vector<float>& x)
{
    constexpr int N = 4096;
    double total = 0.0;
    int frames = 0;
    for (size_t a = 0; a + N <= x.size (); a += N)
    {
        double e = 0.0;
        for (int i = 0; i < N; ++i)
            e += (double)x[a + (size_t)i] * x[a + (size_t)i];
        if (e < 1e-4)
            continue;
        double logSum = 0.0, sum = 0.0;
        for (int k = 1; k < N / 2; ++k)
        {
            double re = 0.0, im = 0.0;
            for (int i = 0; i < N; ++i)
            {
                const double w = 0.5 - 0.5 * std::cos (2 * kPi * i / N);
                re += w * x[a + (size_t)i] * std::cos (2 * kPi * k * i / N);
                im -= w * x[a + (size_t)i] * std::sin (2 * kPi * k * i / N);
            }
            const double p = re * re + im * im + 1e-20;
            logSum += std::log (p);
            sum += p;
        }
        total += std::exp (logSum / (N / 2 - 1)) / (sum / (N / 2 - 1));
        ++frames;
    }
    return frames ? total / frames : 1.0;
}

TEST (defaults_make_it_tonal)
{
    // the point of Detonatr: a noisy impact comes out tonal (designed impacts measure 0.000 .. 0.006)
    auto e = engine ();
    const auto x = impact (1.2, 0.6, 11);
    const auto y = run (*e, x);
    const std::vector<float> yAligned (y.begin () + e->latency (), y.end ());
    const double fin = flatness (x), fout = flatness (yAligned);
    std::printf ("    flatness in %.3f, out %.4f\n", fin, fout);
    CHECK (fout < 0.006, "as tonal as designed impacts (0.000 .. 0.006): %.4f (input %.3f)", fout, fin);
}

TEST (order_changes_the_sound)
{
    const auto x = impact (1.5);
    auto a = engine ();
    auto b = engine ();
    setOrder (*b, {kStageClean, kStageTone, kStageMultiband, kStageSaturator, kStageTransient}); // saturate, then drop
    a->reset ();
    b->reset ();
    const auto ya = run (*a, x), yb = run (*b, x);
    double diff = 0.0, ref = 0.0;
    for (size_t i = 0; i < x.size (); ++i)
    {
        diff += (double)(ya[i] - yb[i]) * (ya[i] - yb[i]);
        ref += (double)ya[i] * ya[i];
    }
    std::printf ("    difference %.1f dB under the default order's output\n", 10 * std::log10 (diff / (ref + 1e-20) + 1e-20));
    CHECK (diff > 1e-3 * ref, "moving the Saturator before the Transient stage changes the output");
}

TEST (recordings_play)
{
    auto e = engine ();
    auto c = std::make_shared<Carrier> ();
    c->frames = (int)kSr;
    c->sampleRate = kSr;
    for (int ch = 0; ch < 2; ++ch)
    {
        c->ch[ch].resize ((size_t)c->frames);
        for (int i = 0; i < c->frames; ++i)
            c->ch[ch][(size_t)i] = (float)(0.5 * std::sin (2 * kPi * 440.0 * i / kSr));
    }
    set (*e, kToneDry, 0.0);
    set (*e, kResonators, 0.0);
    set (*e, kCarriers, 1.0);
    e->setCarrier (0, c.get ());
    const auto x = impact (1.0);
    const auto y = run (*e, x);
    bool finite = true;
    for (float v : y)
        finite = finite && std::isfinite (v);
    CHECK (finite, "finite with a recording");
    e->setCarrier (0, nullptr);
    const auto z = run (*e, x);
    for (float v : z)
        finite = finite && std::isfinite (v);
    CHECK (finite, "finite after it is taken out");
}

TEST (fuzz)
{
    std::mt19937 rng (3);
    std::uniform_real_distribution<double> u (0.0, 1.0);
    auto e = engine ();
    const auto x = impact (0.5);
    bool finite = true;
    for (int round = 0; round < 20; ++round)
    {
        for (uint32_t id = 0; id < kNumParams; ++id)
            if (u (rng) < 0.3)
                e->setParam (id, toPlain (id, u (rng)));
        const auto y = run (*e, x, 1 + (int)(u (rng) * 700));
        for (float v : y)
            finite = finite && std::isfinite (v);
    }
    CHECK (finite, "random settings and block sizes stay finite");
}

DETONATR_TEST_MAIN
