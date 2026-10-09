// Headless tests for the Deepr DSP. Run: ./deepr_tests [filter]
#include "pluginkit/testing/CpuClock.h"
#include "Engine.h"
#include "Params.h"

#include <chrono>
#include <cmath>
#include <ctime>
#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <vector>

using namespace deepr;

static int gFailures = 0, gChecks = 0;
#define CHECK(cond, ...)                                                   \
    do                                                                     \
    {                                                                      \
        ++gChecks;                                                         \
        if (!(cond))                                                       \
        {                                                                  \
            ++gFailures;                                                   \
            std::printf ("    FAIL %s:%d: %s  ", __FILE__, __LINE__, #cond); \
            std::printf (__VA_ARGS__);                                     \
            std::printf ("\n");                                            \
        }                                                                  \
    } while (0)

struct TestCase
{
    const char* name;
    std::function<void ()> fn;
};
static std::vector<TestCase>& tests ()
{
    static std::vector<TestCase> t;
    return t;
}
struct Reg
{
    Reg (const char* n, std::function<void ()> f) { tests ().push_back ({n, std::move (f)}); }
};
#define TEST(name)                     \
    static void name ();               \
    static Reg reg_##name (#name, name); \
    static void name ()
constexpr double kSr = 48000.0;

struct Sig
{
    std::vector<float> l, r;
};

static Sig tones (std::vector<std::pair<double, double>> freqDb, double secs)
{
    Sig s;
    const size_t n = (size_t)(secs * kSr);
    s.l.assign (n, 0.0f);
    for (auto [f, db] : freqDb)
    {
        const double a = std::pow (10.0, db / 20.0);
        for (size_t i = 0; i < n; ++i)
            s.l[i] += (float)(a * std::sin (2.0 * M_PI * f * i / kSr));
    }
    s.r = s.l;
    return s;
}

static Sig run (Engine& e, const Sig& in, int block = 512)
{
    Sig out;
    out.l.resize (in.l.size ());
    out.r.resize (in.r.size ());
    for (size_t pos = 0; pos < in.l.size (); pos += (size_t)block)
    {
        const int m = (int)std::min<size_t> ((size_t)block, in.l.size () - pos);
        e.process (in.l.data () + pos, in.r.data () + pos, out.l.data () + pos, out.r.data () + pos, m);
    }
    return out;
}

// the level of a tone at f in x[a, b) (dB, a sine at 0 dBFS is 0)
static double toneDb (const std::vector<float>& x, double f, size_t a, size_t b)
{
    double re = 0.0, im = 0.0;
    for (size_t i = a; i < b; ++i)
    {
        re += x[i] * std::cos (2.0 * M_PI * f * i / kSr);
        im += x[i] * std::sin (2.0 * M_PI * f * i / kSr);
    }
    return 20.0 * std::log10 (std::max (1e-12, 2.0 * std::hypot (re, im) / (double)(b - a)));
}

// The defaults with the end saturator (on by default) off: the tests are about Deepr's own processing
// (the saturator's have it on).
static std::unique_ptr<Engine> engine ()
{
    auto e = std::make_unique<Engine> ();
    e->setParam (kTailBase + pk::kTailOn, 0.0);
    e->prepare (kSr, 512);
    return e;
}

// the analysis window: the second half of a 2 s render (whole cycles of the test tones)
constexpr size_t kA = 48000, kB = 96000;

TEST (params)
{
    const auto& t = paramTable ();
    CHECK (t.size () == kNumParams, "size");
    for (uint32_t id = 0; id < kNumParams; ++id)
    {
        double v = 0.0;
        CHECK (t.fromText (id, t.toText (id, t.info (id).def), v), "%s", t.info (id).name);
    }
    CHECK (t.info (kDepth).def == 6.0 && t.info (kDipFreq).def == 250.0 && t.info (kSplit).def == 100.0 &&
               t.info (kMonoSub).def == 1.0 && t.info (kSubGain).def == 0.0 && t.info (kMix).def == 1.0,
           "defaults: Depth 6 dB at 250 Hz, split at 100 Hz, mono sub, Sub 0 dB, Mix 100 %%");
    CHECK (t.info (kTailBase + pk::kTailOn).def == 1.0 && t.info (kTailExtBase + pk::kTailExtClarity).def == 1.0,
           "the end saturator starts on, its Gentlr on");
    CHECK (dipKey (-40.0, -30.0) == 0.0 && dipKey (-24.0, -30.0) == 0.5 && dipKey (0.0, -30.0) == 1.0, "the key's law");
}

TEST (latency_is_the_saturators_only)
{
    auto e = engine ();
    smacheratr::Tail t;
    t.prepare (kSr, 512);
    CHECK (e->latency () == t.latency () && e->latency () < 120, "latency %d samples (the end saturator's)", e->latency ());
}

TEST (neutral_is_flat)
{
    // Depth 0, stereo sub, Sub 0 dB: the output is an all-pass of the input, flat at every frequency
    // (and so is Mix 0 %, and any mix in between: the dry path goes through the same all-pass)
    for (double mixV : {1.0, 0.5, 0.0})
        for (double f : {30.0, 60.0, 100.0, 150.0, 250.0, 1000.0, 8000.0})
        {
            auto e = engine ();
            e->setParam (kDepth, 0.0);
            e->setParam (kMonoSub, 0.0);
            e->setParam (kMix, mixV);
            e->reset ();
            auto out = run (*e, tones ({{f, -12.0}}, 2.0));
            const double db = toneDb (out.l, f, kA, kB);
            CHECK (std::fabs (db + 12.0) < 0.05, "mix %.1f: %.0f Hz at %.2f dB (want -12)", mixV, f, db);
        }
}

TEST (dips_the_low_mids_while_the_sub_plays)
{
    auto measure = [] (bool sub, double depth, double f, float* cut = nullptr) {
        Meters m;
        auto e = engine ();
        e->setMeters (&m);
        e->setParam (kDepth, depth);
        std::vector<std::pair<double, double>> t {{250.0, -18.0}};
        if (sub)
            t.push_back ({45.0, -6.0});
        auto out = run (*e, tones (t, 2.0));
        if (cut)
            *cut = m.cutDb.load ();
        return toneDb (out.l, f, kA, kB);
    };
    float cut = 0.0f;
    const double with = measure (true, 6.0, 250.0, &cut), off = measure (true, 0.0, 250.0), alone = measure (false, 6.0, 250.0);
    std::printf ("    250 Hz: %.2f dB with the sub, %.2f dB with Depth 0, %.2f dB without the sub; meter %.2f dB\n", with, off, alone, cut);
    CHECK (std::fabs (with - (off - 6.0)) < 0.5, "the sub well over the threshold: the full Depth (%.2f vs %.2f dB)", with, off);
    CHECK (std::fabs (alone - off) < 0.05, "no sub: no dip (%.2f vs %.2f dB)", alone, off);
    CHECK (std::fabs (cut + 6.0) < 0.1, "the meter shows the dip: %.2f dB", cut);
    const double subWith = measure (true, 6.0, 45.0), subOff = measure (true, 0.0, 45.0);
    CHECK (std::fabs (subWith - subOff) < 0.1 && std::fabs (subOff + 6.0) < 0.2, "the sub itself untouched: %.2f vs %.2f dB", subWith, subOff);
    // the band: a tone well above it is barely dipped
    auto hi = [] (double depth) {
        auto e = engine ();
        e->setParam (kDepth, depth);
        return toneDb (run (*e, tones ({{45.0, -6.0}, {3000.0, -18.0}}, 2.0)).l, 3000.0, kA, kB);
    };
    CHECK (hi (0.0) - hi (6.0) < 1.0, "3 kHz is left alone (%.2f vs %.2f dB)", hi (6.0), hi (0.0));
}

TEST (the_key_follows_the_threshold)
{
    // a sub at -30 dB peak: Threshold -24: no dip; -36: half the Depth (6 of 12 dB over); -60: all of it
    auto cutAt = [] (double threshold) {
        Meters m;
        auto e = engine ();
        e->setMeters (&m);
        e->setParam (kThreshold, threshold);
        run (*e, tones ({{45.0, -30.0}, {250.0, -30.0}}, 2.0));
        return (double)m.cutDb.load ();
    };
    std::printf ("    cut at Threshold -24: %.2f, -36: %.2f, -60: %.2f dB\n", cutAt (-24.0), cutAt (-36.0), cutAt (-60.0));
    CHECK (cutAt (-24.0) > -0.1, "under the threshold: none (%.2f)", cutAt (-24.0));
    CHECK (cutAt (-36.0) < -2.0 && cutAt (-36.0) > -4.5, "6 dB over: about half the Depth (%.2f)", cutAt (-36.0));
    CHECK (std::fabs (cutAt (-60.0) + 6.0) < 0.05, "far over: the Depth (%.2f)", cutAt (-60.0));
}

TEST (release_lets_go)
{
    // the sub stops at 1 s: 0.6 s later (Release 150 ms) the low mids are back
    Meters m;
    auto e = engine ();
    e->setMeters (&m);
    auto in = tones ({{250.0, -18.0}}, 2.0);
    auto sub = tones ({{45.0, -6.0}}, 1.0);
    for (size_t i = 0; i < sub.l.size (); ++i)
    {
        in.l[i] += sub.l[i];
        in.r[i] += sub.r[i];
    }
    auto out = run (*e, in);
    auto ref = engine ();
    ref->setParam (kDepth, 0.0);
    auto outRef = run (*ref, in);
    const double during = toneDb (out.l, 250.0, 24000, 48000) - toneDb (outRef.l, 250.0, 24000, 48000);
    const double after = toneDb (out.l, 250.0, 76800, 96000) - toneDb (outRef.l, 250.0, 76800, 96000);
    CHECK (during < -5.0 && std::fabs (after) < 0.2, "dipped while the sub plays (%.2f dB), back after (%.2f dB)", during, after);
    CHECK (m.cutDb.load () > -0.1, "the meter lets go (%.2f dB)", m.cutDb.load ());
}

TEST (mono_sub)
{
    // a sub in opposite phase on the two sides (what detuned reese layers do down there) and a 1 kHz
    // tone the same: Mono Sub removes the sub's side and leaves everything above the split stereo
    auto make = [] (double f) {
        auto s = tones ({{f, -12.0}}, 2.0);
        for (auto& x : s.r)
            x = -x;
        return s;
    };
    auto sideDb = [&] (double mono, double f) {
        auto e = engine ();
        e->setParam (kDepth, 0.0);
        e->setParam (kMonoSub, mono);
        e->reset ();
        auto out = run (*e, make (f));
        return toneDb (out.l, f, kA, kB);
    };
    std::printf ("    40 Hz side: %.1f dB (mono 100 %%), %.1f dB (0 %%); 1 kHz: %.1f dB\n", sideDb (1.0, 40.0), sideDb (0.0, 40.0),
                 sideDb (1.0, 1000.0));
    CHECK (sideDb (1.0, 40.0) < -40.0, "the sub's side is gone (%.1f dB)", sideDb (1.0, 40.0));
    CHECK (std::fabs (sideDb (0.0, 40.0) + 12.0) < 0.1, "Mono Sub 0 %%: kept (%.1f dB)", sideDb (0.0, 40.0));
    CHECK (std::fabs (sideDb (1.0, 1000.0) + 12.0) < 0.1, "above the split: kept stereo (%.1f dB)", sideDb (1.0, 1000.0));
}

TEST (sub_gain_and_listen)
{
    auto level = [] (uint32_t id, double v, double f) {
        auto e = engine ();
        e->setParam (kDepth, 6.0);
        e->setParam (id, v);
        e->reset ();
        return toneDb (run (*e, tones ({{45.0, -6.0}, {250.0, -18.0}, {2000.0, -18.0}}, 2.0)).l, f, kA, kB);
    };
    CHECK (std::fabs (level (kSubGain, 4.0, 45.0) + 2.0) < 0.2, "Sub Gain +4 dB: %.2f dB", level (kSubGain, 4.0, 45.0));
    // Listen Sub: only the sub; Listen Cut: only what the dip removes (6 dB off -18 dB leaves -24.0 of -18
    // taken out: (1 - 0.5) of the band, -24 dB)
    CHECK (level (kListen, kListenSub, 45.0) > -7.0 && level (kListen, kListenSub, 2000.0) < -60.0, "Listen Sub: the sub alone");
    const double cut = level (kListen, kListenCut, 250.0);
    CHECK (std::fabs (cut + 24.0) < 0.6 && level (kListen, kListenCut, 45.0) < -35.0, "Listen Cut: what the dip takes (%.2f dB)", cut);
}

TEST (finite_and_cpu)
{
    auto make = [] {
        auto e = engine ();
        e->setParam (kTailBase + pk::kTailOn, 1.0);
        e->setParam (kAttack, 1.0);
        e->setParam (kThreshold, -60.0);
        e->setParam (kDepth, 12.0);
        return e;
    };
    Sig in;
    const size_t n = (size_t)(10.0 * kSr);
    in.l.resize (n);
    in.r.resize (n);
    uint32_t seed = 1;
    for (size_t i = 0; i < n; ++i)
    {
        seed = seed * 1664525u + 1013904223u;
        in.l[i] = (float)((int32_t)seed / 2147483648.0) * 0.9f;
        in.r[i] = (float)std::sin (2.0 * M_PI * 41.0 * i / kSr);
    }
    // CPU time (other programs running do not count), the best of three renders
    Sig out;
    double secs = 1e9;
    for (int i = 0; i < 3; ++i)
    {
        auto e = make ();
        const pk::testing::CpuClock t0 = pk::testing::cpuClock ();
        out = run (*e, in, 333);
        secs = std::min (secs, (double)(pk::testing::cpuClock () - t0) / pk::testing::kCpuClocksPerSec);
    }
    bool finite = true;
    for (size_t i = 0; i < n; ++i)
        finite = finite && std::isfinite (out.l[i]) && std::isfinite (out.r[i]) && std::fabs (out.l[i]) < 8.0f;
    CHECK (finite, "finite and bounded");
    std::printf ("    CPU: %.2f%% of one core (stereo, saturator on)\n", 100.0 * secs / 10.0);
    CHECK (secs / 10.0 < 0.05, "too slow");
}

int main (int argc, char** argv)
{
    const char* filter = argc > 1 ? argv[1] : nullptr;
    int ran = 0;
    for (auto& t : tests ())
    {
        if (filter && std::string (t.name).find (filter) == std::string::npos)
            continue;
        const int before = gFailures;
        std::printf ("%s\n", t.name);
        t.fn ();
        std::printf ("  %s\n", gFailures == before ? "ok" : "FAILED");
        ++ran;
    }
    std::printf ("\n%d tests, %d checks, %d failures\n", ran, gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}
