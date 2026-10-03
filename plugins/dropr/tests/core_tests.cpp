// Headless tests for the Dropr DSP. Run: ./dropr_tests [filter]
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

using namespace dropr;

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

// hits: a 1 kHz tone that steps from -40 dB up to -6 dB at each onset and decays over 100 ms, onsets every `every` s
static Sig hits (double secs, double every, double first = 0.25, double decay = 0.1)
{
    Sig s;
    const size_t n = (size_t)(secs * kSr);
    s.l.assign (n, 0.0f);
    for (size_t i = 0; i < n; ++i)
    {
        const double t = i / kSr;
        double a = 0.01; // -40 dB between hits
        if (t >= first)
        {
            const double since = std::fmod (t - first, every);
            a += 0.5 * std::exp (-since / decay);
        }
        s.l[i] = (float)(a * std::sin (2.0 * M_PI * 1000.0 * t));
    }
    s.r = s.l;
    return s;
}

// the level (dB) of x around time t: RMS over 1 ms, times sqrt 2 (a sine's peak)
static double levelAt (const std::vector<float>& x, double t)
{
    const size_t a = (size_t)(t * kSr), b = a + (size_t)(0.001 * kSr);
    double s = 0.0;
    for (size_t i = a; i < b && i < x.size (); ++i)
        s += (double)x[i] * x[i];
    return 20.0 * std::log10 (std::max (1e-9, std::sqrt (2.0 * s / (double)(b - a))));
}

static std::unique_ptr<Engine> engine ()
{
    auto e = std::make_unique<Engine> ();
    e->prepare (kSr, 512);
    return e;
}

TEST (params)
{
    const auto& t = paramTable ();
    CHECK (t.size () == kNumParams, "size");
    for (uint32_t id = 0; id < kNumParams; ++id)
    {
        double v = 0.0;
        CHECK (t.fromText (id, t.toText (id, t.info (id).def), v), "%s", t.info (id).name);
    }
    CHECK (t.info (kTailBase + pk::kTailOn).def == 0.0, "the end saturator starts off");
    const Shape s = shapeOf ([&] (uint32_t id) { return t.info (id).def; });
    CHECK (s.n == 3 && s.valueAt (0.0) == 0.0 && s.valueAt (1.0) == 1.0, "the default shape: from the bottom back to the top");
}

TEST (latency)
{
    auto e = engine ();
    smacheratr::Tail t;
    t.prepare (kSr, 512);
    CHECK (e->latency () == 240 + t.latency (), "latency %d: the 5 ms look-ahead and the end saturator", e->latency ());
}

TEST (drops_each_hit_and_comes_back)
{
    // hits every 0.5 s; the shape drops the hit by Depth (30 dB) and is back at 0 dB after its Length (150 ms)
    Meters m;
    auto e = engine ();
    e->setMeters (&m);
    auto in = hits (1.5, 0.5);
    auto out = run (*e, in);
    const double lat = e->latency () / kSr;
    for (double onset : {0.25, 0.75, 1.25})
    {
        const double at = levelAt (out.l, onset + lat + 0.0005) - levelAt (in.l, onset + 0.0005);
        const double after = levelAt (out.l, onset + lat + 0.2) - levelAt (in.l, onset + 0.2);
        std::printf ("    hit at %.2f s: %.1f dB on the hit, %.2f dB 200 ms later\n", onset, at, after);
        CHECK (at < -24.0, "the hit drops: %.1f dB", at);
        CHECK (std::fabs (after) < 0.3, "and is back after the shape: %.2f dB", after);
    }
    // the sound starting from silence is a hit too: four
    CHECK (m.hits.load () == 4, "the start and three hits (%d)", m.hits.load ());
    // after the start's shape, before the first hit: untouched
    CHECK (std::fabs (levelAt (out.l, 0.2 + lat) - levelAt (in.l, 0.2)) < 0.05, "between hits: untouched");
}

TEST (depth_mix_and_shape)
{
    auto in = hits (1.0, 0.5);
    auto onHit = [&] (uint32_t id, double v) {
        auto e = engine ();
        e->setParam (id, v);
        e->reset ();
        auto out = run (*e, in);
        return levelAt (out.l, 0.25 + e->latency () / kSr + 0.0005) - levelAt (in.l, 0.25 + 0.0005);
    };
    CHECK (std::fabs (onHit (kDepth, 0.0)) < 0.05, "Depth 0: untouched (%.2f dB)", onHit (kDepth, 0.0));
    CHECK (std::fabs (onHit (kDepth, 12.0) + 12.0) < 1.5, "Depth 12: about 12 dB down on the hit (%.2f dB)", onHit (kDepth, 12.0));
    CHECK (std::fabs (onHit (kMix, 0.0)) < 0.05, "Mix 0: the dry signal (%.2f dB)", onHit (kMix, 0.0));
    CHECK (std::fabs (onHit (kMix, 0.5) - 20.0 * std::log10 (0.5 + 0.5 * std::pow (10.0, -30.0 / 20.0))) < 1.0, "Mix 50 %%: half the dry back (%.2f dB)",
           onHit (kMix, 0.5));
    // a shape drawn the other way (top first, down to the bottom): a gate that lets the hit through
    auto e = engine ();
    e->setParam (pointParam (0, kPtY), 1.0);
    e->setParam (pointParam (1, kPtY), 0.5);
    e->setParam (pointParam (2, kPtY), 0.0);
    e->reset ();
    auto out = run (*e, in);
    const double lat = e->latency () / kSr;
    CHECK (levelAt (out.l, 0.25 + lat + 0.0005) - levelAt (in.l, 0.25 + 0.0005) > -3.0, "a shape starting at the top lets the hit through");
    CHECK (levelAt (out.l, 0.25 + lat + 0.2) - levelAt (in.l, 0.25 + 0.2) < -29.0, "and holds its last level, the bottom, after it");
}

TEST (pre_starts_the_shape_before_the_hit)
{
    // a click-like onset: silence, then a full-scale step at 0.25 s; with Pre at 5 ms the first samples
    // of the onset are already down, with Pre 0 the detector's reaction lets a little through
    Sig in;
    const size_t n = (size_t)(0.5 * kSr);
    in.l.assign (n, 0.0f);
    for (size_t i = (size_t)(0.25 * kSr); i < n; ++i)
        in.l[i] = (float)(0.5 * std::sin (2.0 * M_PI * 1000.0 * (i / kSr)));
    in.r = in.l;
    auto firstMs = [&] (double pre) {
        auto e = engine ();
        e->setParam (kPre, pre);
        e->setParam (kDepth, 40.0);
        e->setParam (pointParam (0, kPtCurve), 0.0);
        e->setParam (pointParam (1, kPtX), 0.5);
        e->setParam (pointParam (1, kPtY), 0.0); // stays at the bottom for half the shape
        e->reset ();
        auto out = run (*e, in);
        const size_t a = (size_t)(0.25 * kSr) + (size_t)e->latency ();
        double pk = 0.0;
        for (size_t i = a; i < a + 24; ++i)
            pk = std::max (pk, (double)std::fabs (out.l[i]));
        return 20.0 * std::log10 (std::max (1e-9, pk / 0.5));
    };
    std::printf ("    the onset's first 0.5 ms: %.1f dB with Pre 5 ms, %.1f dB with Pre 0\n", firstMs (5.0), firstMs (0.0));
    CHECK (firstMs (5.0) < -35.0, "Pre 5 ms: the onset itself is down (%.1f dB)", firstMs (5.0));
    CHECK (firstMs (0.0) > firstMs (5.0) + 10.0, "Pre 0: the shape starts at the hit, after its first samples");
}

TEST (retrigger)
{
    // hits 30 ms apart: Retrigger 50 ms takes every other one, 10 ms takes them all
    auto count = [&] (double ms) {
        Meters m;
        auto e = engine ();
        e->setMeters (&m);
        e->setParam (kRetrigger, ms);
        run (*e, hits (1.0, 0.03, 0.25, 0.005));
        return m.hits.load ();
    };
    std::printf ("    hits: %d with Retrigger 10 ms, %d with 50 ms\n", count (10.0), count (50.0));
    CHECK (count (10.0) > count (50.0) + 5, "a shorter Retrigger takes more hits");
}

TEST (finite_and_cpu)
{
    Sig in;
    const size_t n = (size_t)(10.0 * kSr);
    in.l.resize (n);
    in.r.resize (n);
    uint32_t seed = 1;
    for (size_t i = 0; i < n; ++i)
    {
        seed = seed * 1664525u + 1013904223u;
        const double burst = std::fmod (i / kSr, 0.125) < 0.01 ? 1.0 : 0.05;
        in.l[i] = (float)((int32_t)seed / 2147483648.0 * burst);
        in.r[i] = in.l[i];
    }
    // CPU time (other programs running do not count), the best of three renders
    Sig out;
    double secs = 1e9;
    for (int i = 0; i < 3; ++i)
    {
        auto e = engine ();
        e->setParam (kTailBase + pk::kTailOn, 1.0);
        e->setParam (kPointCount, 8.0);
        const std::clock_t t0 = std::clock ();
        out = run (*e, in, 333);
        secs = std::min (secs, (double)(std::clock () - t0) / CLOCKS_PER_SEC);
    }
    bool finite = true;
    for (size_t i = 0; i < n; ++i)
        finite = finite && std::isfinite (out.l[i]) && std::fabs (out.l[i]) < 4.0f;
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
