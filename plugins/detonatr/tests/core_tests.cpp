// Detonatr's engine: the chain of stages, its order, its latency, the bypasses, dry/wet, the defaults
// on an impact and the CPU the default chain takes.
#include "Engine.h"
#include "Harness.h"

#include <algorithm>
#include <cmath>
#include <ctime>
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
std::vector<float> impact (double seconds, double every = 0.6, unsigned seed = 7, double sr = kSr)
{
    std::mt19937 rng (seed);
    std::normal_distribution<float> noise (0.0f, 1.0f);
    std::vector<float> x ((size_t)(seconds * sr));
    const int period = (int)(every * sr);
    for (size_t i = 0; i < x.size (); ++i)
    {
        const double t = (double)(i % (size_t)period) / sr;
        const double env = std::exp (-t / 0.18) * std::min (1.0, t / 0.002);
        x[i] = (float)(env * (0.3 * noise (rng) + 0.4 * std::sin (2 * kPi * 70.0 * t)));
    }
    return x;
}

std::unique_ptr<Engine> engine (double sr = kSr, int block = kBlock)
{
    auto e = std::make_unique<Engine> ();
    e->prepare (sr, block);
    return e;
}

void set (Engine& e, uint32_t id, double v) { e.setParam (id, v); }

// runs a mono signal through as stereo (the right channel a little quieter), in blocks; returns the left output
std::vector<float> run (Engine& e, const std::vector<float>& x, int block = kBlock, std::vector<float>* right = nullptr)
{
    std::vector<float> out (x.size ()), l (block), r (block);
    if (right)
        right->assign (x.size (), 0.0f);
    for (size_t a = 0; a < x.size (); a += (size_t)block)
    {
        const int n = (int)std::min ((size_t)block, x.size () - a);
        for (int i = 0; i < n; ++i)
        {
            l[(size_t)i] = x[a + (size_t)i];
            r[(size_t)i] = 0.8f * x[a + (size_t)i];
        }
        e.process (l.data (), r.data (), l.data (), r.data (), n);
        std::copy (l.begin (), l.begin () + n, out.begin () + (ptrdiff_t)a);
        if (right)
            std::copy (r.begin (), r.begin () + n, right->begin () + (ptrdiff_t)a);
    }
    return out;
}

double maxDelayedError (const std::vector<float>& in, const std::vector<float>& out, int latency, float scale = 1.0f)
{
    double err = 0.0;
    for (size_t i = (size_t)latency; i < in.size (); ++i)
        err = std::max (err, (double)std::fabs (out[i] - scale * in[i - (size_t)latency]));
    return err;
}

void allOff (Engine& e)
{
    for (int s = 0; s < kNumStages; ++s)
        set (e, stageOnParam (s), 0.0);
}

void setOrder (Engine& e, const std::vector<int>& stages)
{
    for (size_t i = 0; i < stages.size (); ++i)
        set (e, kOrderBase + (uint32_t)i, stages[i]);
}

double rmsDb (const std::vector<float>& y, size_t a, size_t b)
{
    double s = 0.0;
    for (size_t i = a; i < b; ++i)
        s += (double)y[i] * y[i];
    return 10.0 * std::log10 (s / (double)(b - a) + 1e-30);
}
} // namespace

TEST (table_is_consistent)
{
    const auto& t = paramTable ();
    CHECK (t.size () == kNumParams, "%u entries, %u ids", t.size (), (unsigned)kNumParams);
    bool ids = true;
    for (uint32_t i = 0; i < t.size (); ++i)
        ids = ids && t.info (i).id == i;
    CHECK (ids, "every entry sits at its id");
    CHECK (std::string (t.info (kTailBase + pk::kTailOn).name) == "Saturator" && t.info (kTailBase + pk::kTailOn).def == 0.0,
           "the Smacheratr at the end, off");
    CHECK (std::string (t.info (kTailExt2Base + pk::kTailExt2Advanced).name) == "Saturator Gently Advanced" && kNumParams == kTailExt2Base + 9,
           "its third block last");
    for (int s = 0; s < kNumStages; ++s)
    {
        CHECK (t.info (stageOnParam (s)).def == 1.0, "%s on by default", stageName (s));
        CHECK (std::string (t.info (stageOnParam (s)).name) == stageName (s), "%s's On is named after it", stageName (s));
        CHECK (stageOfParam (stageOnParam (s)) == s, "%s's On belongs to it", stageName (s));
        CHECK (t.info (kOrderBase + (uint32_t)s).def == s, "stage %d: %s", s + 1, stageName (s));
    }
    CHECK (stageOfParam (kOutput) == -1 && stageOfParam (kTailBase) == -1, "the master and tail parameters belong to no stage");
}

TEST (defaults_are_the_users_settings)
{
    auto def = [] (uint32_t id) { return paramTable ().info (id).def; };
    CHECK (def (kVocBands) == 32 && std::fabs (def (kVocRatio) - 0.46) < 1e-12, "Vocoder: 32 bands, Ratio 0.46");
    CHECK (def (kSpkMode) == 1 && def (kSpkDepth) == 5.1 && def (kSpkSensitivity) == 3.7 && def (kSpkDecay) == 7.1 &&
               def (kSpkSharpness) == 1.3 && def (kSpkDecayTilt) == 0.0 && def (kSpkLink) == 1.0 && def (kSpkMix) == 1.0 &&
               def (kSpkTrim) == 0.0,
           "Spike: Spiff's settings (boost, 5.1, 3.7, 7.1, 1.3)");
    CHECK (def (kTr1Base + kTrGain) == -8.49 && def (kTr1Base + kTrThreshold) == -80.0 && def (kTr1Base + kTrRatio) == 0.27 &&
               def (kTr1Base + kTrOvershoot) == 25.94 && def (kTr1Base + kTrRise) == 0.10 && def (kTr1Base + kTrRecovery) == 77.46 &&
               std::fabs (def (kTr1Base + kTrOverdrive) - 0.6088) < 1e-12,
           "Transient 1: TransMod's settings");
    CHECK (def (kTr2Base + kTrGain) == -1.47 && def (kTr2Base + kTrThreshold) == -14.8 && def (kTr2Base + kTrRatio) == 0.58 &&
               def (kTr2Base + kTrOvershoot) == 6.42 && def (kTr2Base + kTrOverdrive) == 0.0,
           "Transient 2: TransMod's settings");
    CHECK (def (kLim1Base + kLimGain) == 4.9 && def (kLim2Base + kLimGain) == 0.0 && def (kLim1Base + kLimCeiling) == 0.0 &&
               def (kLim2Base + kLimCeiling) == 0.0 && def (kLim1Base + kLimTruePeak) == 1.0 && def (kLim2Base + kLimTruePeak) == 1.0,
           "Limiters: +4.9 and 0 dB into 0 dBTP, True Peak");
    bool same = true;
    for (uint32_t f = 1; f < kCompFields; ++f)
        same = same && def (kComp1Base + f) == def (kComp2Base + f);
    CHECK (same && def (kComp1Base + kCompAutoThreshold) == 1.0 && def (kComp1Base + kCompAutoRelease) == 1.0 &&
               def (kComp1Base + kCompAutoGain) == 1.0 && def (kComp1Base + kCompDry) == -60.0,
           "Comps: the same, Auto Threshold, Auto Release, Auto Gain, no Dry");
    CHECK (def (kTapeSplit) == 200.0 && def (kTapeLowMix) == 1.0 && def (kTapeLowDyn) == 0.0, "Tape: split at 200 Hz, mix 100 %%");
}

TEST (order_resolves)
{
    int def[kNumStages];
    for (int i = 0; i < kNumStages; ++i)
        def[i] = i;
    Order o = resolveOrder (def);
    CHECK (o.stage[0] == kStageVocoder && o.stage[9] == kStageLimiter2, "the default order");
    int dup[kNumStages] = {kStageTape, kStageTape, kStageSpike, 99, kStageSpike, kStageTape, kStageTape, kStageTape, kStageTape, kStageTape};
    o = resolveOrder (dup);
    CHECK (o.stage[0] == kStageTape && o.stage[1] == kStageSpike && o.stage[2] == kStageVocoder && o.stage[3] == kStageMotion &&
               o.stage[9] == kStageLimiter2,
           "duplicates run once, the rest after in the default order");
    bool seen[kNumStages] {};
    for (int s : o.stage)
        seen[s] = true;
    CHECK (std::all_of (seen, seen + kNumStages, [] (bool b) { return b; }), "every stage runs");
}

TEST (latency_is_constant)
{
    std::mt19937 rng (2);
    for (double sr : {44100.0, 48000.0, 96000.0})
    {
        auto e = engine (sr, 512);
        const int l0 = e->latency ();
        std::printf ("    %.0f Hz: latency %d samples (%.2f ms)\n", sr, l0, 1000.0 * l0 / sr);
        bool same = true;
        for (int round = 0; round < 20; ++round)
        {
            std::vector<int> o (kNumStages);
            for (int i = 0; i < kNumStages; ++i)
                o[(size_t)i] = i;
            std::shuffle (o.begin (), o.end (), rng);
            setOrder (*e, o);
            for (int s = 0; s < kNumStages; ++s)
                set (*e, stageOnParam (s), (rng () & 1) ? 1.0 : 0.0);
            set (*e, kVocBands, 8 + (double)(rng () % 90));
            set (*e, kLim1Base + kLimLookahead, 0.1 + (rng () % 49) * 0.1);
            set (*e, kTapeSplit, 80.0 + (double)(rng () % 900));
            set (*e, kTailBase + pk::kTailOn, (rng () & 1) ? 1.0 : 0.0);
            same = same && e->latency () == l0;
        }
        CHECK (same, "%.0f Hz: the same latency in every order, with any stage on or off", sr);
    }
}

TEST (bypassed_is_the_delayed_input)
{
    for (double sr : {44100.0, 48000.0, 96000.0})
    {
        std::mt19937 rng (5);
        for (int round = 0; round < 3; ++round)
        {
            auto e = engine (sr, 512);
            allOff (*e);
            std::vector<int> o (kNumStages);
            for (int i = 0; i < kNumStages; ++i)
                o[(size_t)i] = i;
            std::shuffle (o.begin (), o.end (), rng);
            setOrder (*e, o);
            e->reset ();
            const auto x = impact (1.0, 0.6, 7, sr);
            std::vector<float> r;
            const auto y = run (*e, x, 173, &r);
            const double err = std::max (maxDelayedError (x, y, e->latency ()), maxDelayedError (x, r, e->latency (), 0.8f));
            // (exact here; on arm64 the compiler fuses multiply-adds, which can round the last bit: under -120 dB)
            CHECK (err < 1e-6, "%.0f Hz, every stage off (order %d): the input, delayed (%.2g)", sr, round, err);
        }
    }
}

TEST (each_stage_alone_lines_up)
{
    // a stage on its own, at Mix 0 where it has one: still lined up with the others' delays
    auto e = engine ();
    allOff (*e);
    set (*e, kMotOn, 1.0);
    set (*e, kMotMix, 0.0);
    e->reset ();
    const auto x = impact (1.0);
    const auto y = run (*e, x, 100);
    CHECK (maxDelayedError (x, y, e->latency ()) < 1e-6, "Motion at Mix 0: the delayed input");
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
    // the default chain: sounds, never over 0 dBFS (Limiter 2 at 0 dBTP last), and each stage keeps
    // the level in the same region as it went in
    const auto x = impact (3.0);
    const double inDb = rmsDb (x, x.size () / 3, x.size ());
    std::printf ("    input rms %.1f dBFS\n", inDb);
    double last = inDb;
    bool matched = true;
    auto e0 = engine ();
    for (int k = 1; k <= kNumStages; ++k)
    {
        auto e = engine ();
        for (int s = k; s < kNumStages; ++s)
            set (*e, stageOnParam (s), 0.0);
        e->reset ();
        std::vector<float> r;
        const auto y = run (*e, x, kBlock, &r);
        const double lvl = rmsDb (y, y.size () / 3, y.size ()); // once settled
        double pk = 0.0;
        bool finite = true;
        for (size_t i = 0; i < y.size (); ++i)
        {
            pk = std::max ({pk, (double)std::fabs (y[i]), (double)std::fabs (r[i])});
            finite = finite && std::isfinite (y[i]) && std::isfinite (r[i]);
        }
        std::printf ("    up to %-12s rms %6.1f dBFS (%+5.1f dB), peak %6.2f dBFS\n", stageName (k - 1), lvl, lvl - last, 20.0 * std::log10 (pk + 1e-12));
        CHECK (finite, "finite");
        matched = matched && std::fabs (lvl - last) < 12.0;
        last = lvl;
        if (k == kNumStages)
        {
            CHECK (pk <= 1.0, "the default chain does not clip: peak %.4f", pk);
            CHECK (lvl > inDb - 6.0 && lvl < 0.0, "and is loud, not exploding: %.1f dBFS rms", lvl);
        }
    }
    CHECK (matched, "no stage moves the level by 12 dB or more");
}

TEST (switching_a_stage_crossfades)
{
    // turning the Tape stage off and on while a tone plays: no step in the output
    auto e = engine ();
    allOff (*e);
    set (*e, kTapeOn, 1.0);
    e->reset ();
    std::vector<float> x (188 * kBlock); // about a second, whole blocks
    for (size_t i = 0; i < x.size (); ++i)
        x[i] = (float)(0.3 * std::sin (2 * kPi * 100.0 * (double)i / kSr));
    std::vector<float> y (x.size ()), l (kBlock), r (kBlock);
    for (size_t a = 0; a < x.size (); a += kBlock)
    {
        if (a == 12800)
            set (*e, kTapeOn, 0.0);
        if (a == 25600)
            set (*e, kTapeOn, 1.0);
        std::copy (x.begin () + (ptrdiff_t)a, x.begin () + (ptrdiff_t)a + kBlock, l.begin ());
        std::copy (l.begin (), l.end (), r.begin ());
        e->process (l.data (), r.data (), l.data (), r.data (), kBlock);
        std::copy (l.begin (), l.end (), y.begin () + (ptrdiff_t)a);
    }
    double jump = 0.0;
    for (size_t i = 12000; i + 1 < y.size (); ++i)
        jump = std::max (jump, (double)std::fabs (y[i + 1] - y[i]));
    const double natural = 0.3 * 2 * kPi * 100.0 / kSr;
    std::printf ("    largest step %.4f (the tone's own %.4f)\n", jump, natural);
    CHECK (jump < 3.0 * natural, "no click: %.4f", jump);
}

TEST (order_changes_the_sound)
{
    const auto x = impact (1.5);
    auto a = engine ();
    auto b = engine ();
    setOrder (*b, {kStageLimiter2, kStageVocoder, kStageSpike, kStageMotion, kStageTransient1, kStageLimiter1, kStageTransient2, kStageComp1,
                   kStageComp2, kStageTape});
    a->reset ();
    b->reset ();
    const auto ya = run (*a, x), yb = run (*b, x);
    double diff = 0.0, ref = 0.0;
    for (size_t i = 0; i < x.size (); ++i)
    {
        diff += (double)(ya[i] - yb[i]) * (ya[i] - yb[i]);
        ref += (double)ya[i] * ya[i];
    }
    CHECK (diff > 1e-3 * ref, "moving the last limiter to the front changes the output");
}

TEST (cpu_of_the_default_chain)
{
    // the full default chain at 48 kHz, stereo: CPU time (other programs do not count), best of three
    auto e = engine (kSr, 512);
    const auto x = impact (10.0);
    std::vector<float> l (512), r (512);
    double best = 1e9;
    for (int run = 0; run < 3; ++run)
    {
        e->reset ();
        const std::clock_t t0 = std::clock ();
        for (size_t a = 0; a + 512 <= x.size (); a += 512)
        {
            std::copy (x.begin () + (ptrdiff_t)a, x.begin () + (ptrdiff_t)a + 512, l.begin ());
            std::copy (l.begin (), l.end (), r.begin ());
            e->process (l.data (), r.data (), l.data (), r.data (), 512);
        }
        best = std::min (best, (double)(std::clock () - t0) / CLOCKS_PER_SEC / 10.0);
    }
    std::printf ("    CPU: %.2f%% of one core (the default chain, 48 kHz stereo, best of three)\n", 100.0 * best);
    CHECK (best < 0.25, "under 25 %% of a core: %.1f %%", 100.0 * best);
}

TEST (fuzz)
{
    std::mt19937 rng (3);
    std::uniform_real_distribution<double> u (0.0, 1.0);
    auto e = engine ();
    const auto x = impact (0.5);
    bool finite = true;
    for (int round = 0; round < 30; ++round)
    {
        for (uint32_t id = 0; id < kNumParams; ++id)
            if (u (rng) < 0.3)
                e->setParam (id, toPlain (id, u (rng)));
        auto in = x;
        if (round % 5 == 0)
            for (auto& v : in)
                v *= 20.0f; // very loud
        const auto y = run (*e, in, 1 + (int)(u (rng) * 700));
        for (float v : y)
            finite = finite && std::isfinite (v);
    }
    CHECK (finite, "random settings and block sizes stay finite");
}

DETONATR_TEST_MAIN
