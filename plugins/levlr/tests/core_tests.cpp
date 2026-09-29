// Headless tests for the Levlr DSP. Run: ./levlr_tests [filter]
#include "Engine.h"
#include "Params.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <vector>

using namespace levlr;

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
static const char* kSlopeNames[kNumSlopes] = {"12 dB/oct", "24 dB/oct", "36 dB/oct", "48 dB/oct",
                                              "60 dB/oct", "72 dB/oct", "84 dB/oct", "96 dB/oct"};

// The crossover alone (no end saturator), with `set` applied before it starts (so nothing glides).
static std::unique_ptr<Engine> engine (const std::function<void (Engine&)>& set = {}, double sr = kSr)
{
    auto e = std::make_unique<Engine> (false);
    if (set)
        set (*e);
    e->prepare (sr, 512);
    return e;
}

// The engine's impulse response (left channel), `secs` long.
static std::vector<double> impulse (Engine& e, double sr = kSr, double secs = 1.0)
{
    const int n = (int)(secs * sr);
    std::vector<double> h ((size_t)n);
    std::vector<float> l (512), r (512);
    for (int pos = 0; pos < n; pos += 512)
    {
        const int m = std::min (512, n - pos);
        std::fill (l.begin (), l.end (), 0.0f);
        if (pos == 0)
            l[0] = 1.0f;
        std::copy (l.begin (), l.end (), r.begin ());
        e.process (l.data (), r.data (), l.data (), r.data (), m);
        for (int i = 0; i < m; ++i)
            h[(size_t)(pos + i)] = l[(size_t)i];
    }
    return h;
}

// the response at f, from an impulse response
static std::complex<double> at (const std::vector<double>& h, double f, double sr = kSr)
{
    std::complex<double> acc (0.0, 0.0);
    const double w = -2.0 * M_PI * f / sr;
    for (size_t i = 0; i < h.size (); ++i)
        acc += h[i] * std::complex<double> (std::cos (w * (double)i), std::sin (w * (double)i));
    return acc;
}
static double db (std::complex<double> c) { return 20.0 * std::log10 (std::abs (c) + 1e-12); }
static double deg (std::complex<double> c) { return std::arg (c) * 180.0 / M_PI; }

static std::vector<double> logFreqs (int count, double lo, double hi)
{
    std::vector<double> f;
    for (int i = 0; i < count; ++i)
        f.push_back (lo * std::pow (hi / lo, (double)i / (count - 1)));
    return f;
}

// dB rms of a sine through the engine (the last half of `secs`), relative to the input
static double sineGain (Engine& e, double hz, double secs = 0.6, double amp = 0.1)
{
    const int n = (int)(secs * kSr);
    std::vector<float> l (480), r (480);
    double acc = 0.0;
    int count = 0;
    for (int pos = 0; pos < n; pos += 480)
    {
        const int m = std::min (480, n - pos);
        for (int i = 0; i < m; ++i)
            l[(size_t)i] = r[(size_t)i] = (float)(amp * std::sin (2.0 * M_PI * hz * (double)(pos + i) / kSr));
        e.process (l.data (), r.data (), l.data (), r.data (), m);
        if (pos >= n / 2)
            for (int i = 0; i < m; ++i)
            {
                acc += (double)l[(size_t)i] * l[(size_t)i];
                ++count;
            }
    }
    return 10.0 * std::log10 (acc / std::max (1, count) + 1e-20) - 20.0 * std::log10 (amp / std::sqrt (2.0));
}

TEST (parameters_and_defaults)
{
    const auto& t = paramTable ();
    CHECK (t.size () == kNumParams, "every parameter: %u of %u", (unsigned)t.size (), (unsigned)kNumParams);
    for (uint32_t id = 0; id < t.size (); ++id)
        CHECK (t.info (id).id == id, "id %u in its place", id);
    CHECK (kNumParams == kTailExtBase + pk::kTailExtFields, "the end saturator's extended block is last");
    CHECK (t.info (kSlope).def == (double)kSlope24, "24 dB/oct by default");
    CHECK (t.info (xoverParam (0)).def == 120.0 && t.info (xoverParam (1)).def == 1000.0 && t.info (xoverParam (2)).def == 6000.0,
           "crossovers at 120 Hz, 1 kHz, 6 kHz");
    for (int b = 0; b < kBands; ++b)
        CHECK (t.info (bandParam (b, kGain)).def == 0.0 && t.info (bandParam (b, kMute)).def == 0.0 &&
                   t.info (bandParam (b, kSolo)).def == 0.0,
               "band %d at 0 dB, heard", b + 1);
    CHECK (t.info (kTailBase + pk::kTailOn).def == 0.0 && t.info (kTailBase + pk::kTailPreLimit).def == 1.0,
           "the end Smacheratr: off, Pre-Limit on");
    std::printf ("    %u parameters (tail at %u, bands at %u, tail ext at %u)\n", (unsigned)kNumParams, (unsigned)kTailBase,
                 (unsigned)kBandBase, (unsigned)kTailExtBase);
}

TEST (crossovers_stay_in_order)
{
    const double gap = std::pow (2.0, kMinGapOct);
    auto ordered = [&] (const double* x, double sr) {
        bool ok = x[0] >= kMinXoverHz - 1e-9 && x[kCrossovers - 1] <= std::min (kMaxXoverHz, 0.45 * sr) + 1e-6;
        for (int k = 1; k < kCrossovers; ++k)
            ok &= x[k] >= x[k - 1] * gap * (1.0 - 1e-9);
        return ok;
    };
    const double cases[][3] = {{5000.0, 1000.0, 800.0}, {20.0, 20.0, 20.0}, {20000.0, 20000.0, 20000.0}, {100.0, 105.0, 110.0}, {120.0, 1000.0, 6000.0}};
    for (const auto& c : cases)
        for (double sr : {44100.0, 48000.0, 96000.0})
        {
            double x[kCrossovers];
            effectiveCrossovers (c, sr, x);
            CHECK (ordered (x, sr), "%.0f/%.0f/%.0f at %.0f -> %.1f/%.1f/%.1f", c[0], c[1], c[2], sr, x[0], x[1], x[2]);
        }
    double x[kCrossovers];
    const double def[kCrossovers] = {120.0, 1000.0, 6000.0};
    effectiveCrossovers (def, kSr, x);
    CHECK (x[0] == 120.0 && x[1] == 1000.0 && x[2] == 6000.0, "ordered settings are kept");
}

TEST (every_slope_sums_to_an_allpass)
{
    // all bands at 0 dB: the level is flat; the phase is not (it turns at each crossover)
    for (double sr : {44100.0, 48000.0, 96000.0})
        for (int s = 0; s < kNumSlopes; ++s)
        {
            auto e = engine ([s] (Engine& en) { en.setParam (kSlope, s); }, sr);
            const auto h = impulse (*e, sr);
            double worst = 0.0, worstF = 0.0, worstModel = 0.0, turned = 0.0;
            const double xo[kCrossovers] = {120.0, 1000.0, 6000.0};
            const double gains[kBands] = {1.0, 1.0, 1.0, 1.0};
            for (double f : logFreqs (60, 20.0, std::min (20000.0, 0.45 * sr)))
            {
                const auto r = at (h, f, sr);
                if (std::fabs (db (r)) > worst)
                {
                    worst = std::fabs (db (r));
                    worstF = f;
                }
                // the display's model of the filters matches them
                const auto m = totalResponse (xo, s, gains, f, sr);
                worstModel = std::max (worstModel, std::abs (r - m));
                turned = std::max (turned, std::fabs (deg (r)));
            }
            const double p120 = deg (at (h, 120.0, sr)), p1k = deg (at (h, 1000.0, sr)), p6k = deg (at (h, 6000.0, sr));
            // the group delay at 120 Hz (the phase's slope there)
            const double gd = -std::arg (at (h, 121.0, sr) / at (h, 119.0, sr)) / (2.0 * M_PI * 2.0) * 1000.0;
            std::printf ("    %s at %.0f: flat within %.4f dB (at %.0f Hz); phase %.0f / %.0f / %.0f deg at 120 / 1k / 6k, "
                         "group delay %.2f ms at 120 Hz; model off by %.5f\n",
                         kSlopeNames[s], sr, worst, worstF, p120, p1k, p6k, gd, worstModel);
            CHECK (worst < 0.1, "%s at %.0f flat: %.3f dB at %.0f Hz", kSlopeNames[s], sr, worst, worstF);
            CHECK (turned > 90.0 && gd > 0.5, "%s: the phase turns (up to %.0f deg; %.2f ms at 120 Hz)", kSlopeNames[s], turned, gd);
            CHECK (worstModel < 0.003, "%s: the display's response matches (%.5f)", kSlopeNames[s], worstModel);
        }
}

TEST (a_band_moves_its_own_range)
{
    // band 2 (120 Hz .. 1 kHz) at +12 dB: a tone in its middle rises 12 dB; one in band 1 or band 4
    // hardly moves
    for (int s = 0; s < kNumSlopes; ++s)
    {
        auto e = engine ([s] (Engine& en) {
            en.setParam (kSlope, s);
            en.setParam (bandParam (1, kGain), 12.0);
        });
        const auto h = impulse (*e);
        const double mid = db (at (h, std::sqrt (120.0 * 1000.0))), low = db (at (h, 30.0)), high = db (at (h, 12000.0));
        std::printf ("    %s: band 2 +12: %.2f dB at 346 Hz, %.2f at 30 Hz, %.2f at 12 kHz\n", kSlopeNames[s], mid, low, high);
        // 12 dB/oct parts the bands gently: its middle is a little under +12, and some of the lift
        // spills two octaves down
        const double tol = s == kSlope12 ? 1.5 : 0.5, spill = s == kSlope12 ? 2.0 : 0.5;
        CHECK (std::fabs (mid - 12.0) < tol, "%s: the band's middle: %.2f dB", kSlopeNames[s], mid);
        CHECK (std::fabs (low) < spill && std::fabs (high) < spill, "%s: the other bands: %.2f, %.2f dB", kSlopeNames[s], low, high);
    }
    // a sine through the whole engine (end saturator included, off)
    Engine full;
    full.setParam (bandParam (1, kGain), 12.0);
    full.setParam (bandParam (3, kGain), -6.0);
    full.prepare (kSr, 512);
    const double g346 = sineGain (full, 346.0), g12k = sineGain (full, 12000.0), g40 = sineGain (full, 40.0);
    std::printf ("    full engine: %.2f dB at 346 Hz (+12), %.2f at 12 kHz (-6), %.2f at 40 Hz (0)\n", g346, g12k, g40);
    CHECK (std::fabs (g346 - 12.0) < 0.5 && std::fabs (g12k + 6.0) < 0.5 && std::fabs (g40) < 0.5, "sines through the engine");
}

TEST (moving_a_crossover_moves_the_edge)
{
    // band 1 muted: the rest is -6 dB at crossover 1 (Linkwitz-Riley: each side -6 dB there)
    auto measure = [] (double x1, double f) {
        auto e = engine ([x1] (Engine& en) {
            en.setParam (bandParam (0, kMute), 1.0);
            en.setParam (xoverParam (0), x1);
        });
        return db (at (impulse (*e), f));
    };
    const double a120 = measure (120.0, 120.0), a500 = measure (500.0, 500.0), a120moved = measure (500.0, 120.0);
    std::printf ("    band 1 muted: %.2f dB at the 120 Hz edge; moved to 500 Hz: %.2f dB there, %.2f at 120 Hz\n", a120, a500, a120moved);
    CHECK (std::fabs (a120 + 6.02) < 0.2 && std::fabs (a500 + 6.02) < 0.2, "-6 dB at the edge: %.2f, %.2f", a120, a500);
    CHECK (a120moved < -20.0, "below the moved edge: %.2f dB", a120moved);

    // a crossover moved while playing glides there (no jump) and gets there
    auto e = engine ();
    e->setParam (xoverParam (1), 2000.0);
    std::vector<float> l (480, 0.0f), r (480, 0.0f);
    for (int i = 0; i < 100; ++i)
        e->process (l.data (), r.data (), l.data (), r.data (), 480);
    Meters m;
    e->setMeters (&m);
    e->process (l.data (), r.data (), l.data (), r.data (), 480);
    CHECK (std::fabs (m.xover[1].load () - 2000.0f) < 1.0f, "glided to 2 kHz: %.1f", m.xover[1].load ());
}

TEST (mute_and_solo)
{
    const double band3Mid = std::sqrt (1000.0 * 6000.0);
    {
        auto e = engine ([] (Engine& en) { en.setParam (bandParam (2, kMute), 1.0); });
        const auto h = impulse (*e);
        std::printf ("    band 3 muted: %.1f dB at %.0f Hz, %.2f at 346 Hz\n", db (at (h, band3Mid)), band3Mid, db (at (h, 346.0)));
        CHECK (db (at (h, band3Mid)) < -20.0, "muted: %.1f dB", db (at (h, band3Mid)));
        CHECK (std::fabs (db (at (h, 346.0))) < 0.5, "the rest stays: %.2f dB", db (at (h, 346.0)));
    }
    {
        auto e = engine ([] (Engine& en) { en.setParam (bandParam (1, kSolo), 1.0); });
        const auto h = impulse (*e);
        const double in = db (at (h, 346.0)), below = db (at (h, 30.0)), above = db (at (h, 5000.0));
        std::printf ("    band 2 soloed: %.2f dB at 346 Hz, %.1f at 30 Hz, %.1f at 5 kHz\n", in, below, above);
        CHECK (std::fabs (in) < 0.5 && below < -20.0 && above < -20.0, "solo: only band 2");
    }
    {
        // a soloed band is heard even when muted; two soloed bands both are
        auto e = engine ([] (Engine& en) {
            en.setParam (bandParam (1, kSolo), 1.0);
            en.setParam (bandParam (1, kMute), 1.0);
            en.setParam (bandParam (3, kSolo), 1.0);
        });
        const auto h = impulse (*e);
        CHECK (std::fabs (db (at (h, 346.0))) < 0.5 && std::fabs (db (at (h, 12000.0))) < 0.5 && db (at (h, 2449.0)) < -20.0,
               "solo wins over mute; two solos: %.2f %.2f %.2f", db (at (h, 346.0)), db (at (h, 12000.0)), db (at (h, 2449.0)));
    }
}

TEST (changes_are_click_free)
{
    // a 200 Hz sine: the largest step between samples stays near the sine's own while the slope
    // changes (fade out, swap, fade in), a band jumps 24 dB, and a crossover jumps across it
    const double amp = 0.5, normal = amp * 2.0 * M_PI * 200.0 / kSr;
    auto e = engine ();
    std::vector<float> l (480), r (480);
    long long t = 0;
    auto run = [&] (int blocks) {
        double worst = 0.0;
        float prev = 0.0f;
        for (int b = 0; b < blocks; ++b)
        {
            for (int i = 0; i < 480; ++i)
                l[(size_t)i] = r[(size_t)i] = (float)(amp * std::sin (2.0 * M_PI * 200.0 * (double)(t + i) / kSr));
            e->process (l.data (), r.data (), l.data (), r.data (), 480);
            for (int i = 0; i < 480; ++i)
            {
                if (b > 0 || i > 0)
                    worst = std::max (worst, (double)std::fabs (l[(size_t)i] - prev));
                prev = l[(size_t)i];
            }
            t += 480;
        }
        return worst;
    };
    run (20);
    e->setParam (kSlope, kSlope48);
    const double slopeStep = run (20);
    CHECK (e->slopeInUse () == kSlope48, "the new slope is in use");
    e->setParam (bandParam (1, kGain), -24.0);
    const double gainStep = run (20);
    e->setParam (xoverParam (0), 400.0);
    const double xoverStep = run (20);
    std::printf ("    largest step: %.4f (slope), %.4f (gain), %.4f (crossover); the sine's own %.4f\n", slopeStep, gainStep,
                 xoverStep, normal);
    CHECK (slopeStep < 2.0 * normal && gainStep < 1.5 * normal && xoverStep < 1.5 * normal, "no clicks");
}

TEST (silence_after_a_burst)
{
    // a loud burst at 48 dB/oct, then silence: the output dies away to nothing (no denormal crawl)
    auto e = engine ([] (Engine& en) {
        en.setParam (kSlope, kSlope48);
        en.setParam (bandParam (0, kGain), 12.0);
    });
    std::vector<float> l (512), r (512);
    uint32_t seed = 3;
    for (int b = 0; b < 20; ++b)
    {
        for (int i = 0; i < 512; ++i)
        {
            seed = seed * 1664525u + 1013904223u;
            l[(size_t)i] = r[(size_t)i] = (float)((double)(seed >> 8) / 8388608.0 - 1.0);
        }
        e->process (l.data (), r.data (), l.data (), r.data (), 512);
    }
    float last = 0.0f;
    const auto t0 = std::chrono::steady_clock::now ();
    const int blocks = (int)(5.0 * kSr / 512);
    for (int b = 0; b < blocks; ++b)
    {
        std::fill (l.begin (), l.end (), 0.0f);
        std::fill (r.begin (), r.end (), 0.0f);
        e->process (l.data (), r.data (), l.data (), r.data (), 512);
        if (b == blocks - 1)
            for (float v : l)
                last = std::max (last, std::fabs (v));
    }
    const double secs = std::chrono::duration<double> (std::chrono::steady_clock::now () - t0).count ();
    std::printf ("    after 5 s of silence: %g at most; %.2f%% of real time\n", last, 100.0 * secs / 5.0);
    CHECK (last < 1e-20f, "silent: %g", last);
}

TEST (fuzz_and_cpu)
{
    uint32_t seed = 11;
    auto rnd = [&] {
        seed = seed * 1664525u + 1013904223u;
        return (double)((seed >> 8) & 0xFFFFFF) / 16777216.0;
    };
    Engine e;
    e.prepare (kSr, 1024);
    std::vector<float> l (1024), r (1024);
    bool finite = true;
    for (int blk = 0; blk < 3000; ++blk)
    {
        if (blk % 15 == 0)
            for (int k = 0; k < 5; ++k)
            {
                const uint32_t id = (uint32_t)(rnd () * (double)kNumParams) % kNumParams;
                const auto& info = paramTable ().info (id);
                e.setParam (id, info.min + rnd () * (info.max - info.min));
            }
        const int n = 1 + (int)(rnd () * 1023);
        for (int i = 0; i < n; ++i)
        {
            l[(size_t)i] = (float)(rnd () * 2.0 - 1.0) * 0.7f;
            r[(size_t)i] = (float)(rnd () * 2.0 - 1.0) * 0.7f;
        }
        e.process (l.data (), r.data (), l.data (), r.data (), n);
        for (int i = 0; i < n; ++i)
            finite &= std::isfinite (l[(size_t)i]) && std::isfinite (r[(size_t)i]) && std::fabs (l[(size_t)i]) < 100.0f;
    }
    CHECK (finite, "finite and bounded");

    // CPU: each slope, bands at different levels, crossovers moving, the end saturator on
    for (int s = 0; s < kNumSlopes; ++s)
    {
        Engine c;
        c.setParam (kSlope, s);
        c.setParam (bandParam (0, kGain), 6.0);
        c.setParam (bandParam (2, kGain), -4.0);
        c.setParam (kTailBase + pk::kTailOn, 1.0);
        c.prepare (kSr, 512);
        const int blocks = (int)(10.0 * kSr / 512);
        const auto t0 = std::chrono::steady_clock::now ();
        for (int b = 0; b < blocks; ++b)
        {
            if (b % 8 == 0)
                c.setParam (xoverParam (1), 600.0 + 800.0 * rnd ());
            for (int i = 0; i < 512; ++i)
                l[(size_t)i] = r[(size_t)i] = (float)(rnd () * 2.0 - 1.0) * 0.3f;
            c.process (l.data (), r.data (), l.data (), r.data (), 512);
        }
        const double secs = std::chrono::duration<double> (std::chrono::steady_clock::now () - t0).count ();
        std::printf ("    %s, saturator on: %.2f%% of real time (stereo, 48 kHz)\n", kSlopeNames[s], 100.0 * secs / 10.0);
    }
    // and the crossovers alone
    {
        auto c = engine ([] (Engine& en) { en.setParam (kSlope, kSlope48); });
        const auto t0 = std::chrono::steady_clock::now ();
        const int blocks = (int)(10.0 * kSr / 512);
        for (int b = 0; b < blocks; ++b)
        {
            for (int i = 0; i < 512; ++i)
                l[(size_t)i] = r[(size_t)i] = (float)(rnd () * 2.0 - 1.0) * 0.3f;
            c->process (l.data (), r.data (), l.data (), r.data (), 512);
        }
        const double secs = std::chrono::duration<double> (std::chrono::steady_clock::now () - t0).count ();
        std::printf ("    48 dB/oct crossovers alone: %.2f%% of real time\n", 100.0 * secs / 10.0);
    }
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
