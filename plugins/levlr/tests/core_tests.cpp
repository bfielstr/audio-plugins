// Headless tests for the Levlr DSP. Run: ./levlr_tests [filter]
#include "Engine.h"
#include "Params.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cmath>
#include <ctime>
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

// The engine's impulse response (left channel), `secs` long, from its latency on (so the drives'
// constant delay doesn't count as phase).
static std::vector<double> impulse (Engine& e, double sr = kSr, double secs = 1.0)
{
    const int lat = e.latency ();
    const int n = (int)(secs * sr) + lat;
    std::vector<double> h ((size_t)(n - lat));
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
            if (pos + i >= lat)
                h[(size_t)(pos + i - lat)] = l[(size_t)i];
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
    CHECK (kTailExt2Base == kTailExtBase + pk::kTailExtFields, "the end saturator's extended block, then Gentlr's Advanced block");
    CHECK (kBandCount == kTailExt2Base + pk::kTailExt2Fields &&
               std::string (t.info (kTailExt2Base + pk::kTailExt2Advanced).name) == "Saturator Gentlr Advanced" &&
               t.info (kTailExt2Base + pk::kTailExt2Threshold).def == -18.0 && t.info (kTailExt2Base + pk::kTailExt2Advanced).def == 0.0,
           "Gentlr's Advanced block (the end saturator's) is the last");
    CHECK (t.info (kSlope).def == (double)kSlope24, "24 dB/oct by default");
    CHECK (t.info (xoverParam (0)).def == 120.0 && t.info (xoverParam (1)).def == 1000.0 && t.info (xoverParam (2)).def == 6000.0,
           "crossovers at 120 Hz, 1 kHz, 6 kHz");
    for (int b = 0; b < kBands; ++b)
        CHECK (t.info (bandParam (b, kGain)).def == 0.0 && t.info (bandParam (b, kMute)).def == 0.0 &&
                   t.info (bandParam (b, kSolo)).def == 0.0,
               "band %d at 0 dB, heard", b + 1);
    CHECK (t.info (kTailBase + pk::kTailOn).def == 0.0 && t.info (kTailBase + pk::kTailPreLimit).def == 1.0,
           "the end Smacheratr: off, Pre-Limit on");
    // the band count and the drives come after the end saturator's blocks, at the IDs they are saved under
    CHECK (kBandCount == 49 && kDriveBase == 50 && kTailExt3Base == 58 && kTailExt4Base == 64 && kDriveOversampling == 69 && kNumParams == 70,
           "Bands at 49, the drives at 50 .. 57, the end saturator's fourth block at 58 .. 63, its fifth at 64 .. 68, Oversampling at 69");
    CHECK (std::string (t.info (kTailExt4Base + pk::kTailExt4GlueSub1).name) == "Saturator Gentlr Glue Sub / 1", "the fifth block");
    CHECK (std::string (t.info (kTailExt3Base + pk::kTailExt3High).name) == "Saturator Gentlr High (unused)", "the fourth block");
    CHECK (std::string (t.info (kBandCount).name) == "Bands" && bandsOf (t.info (kBandCount).def) == 4, "four bands by default");
    for (int b = 0; b < kBands; ++b)
        CHECK (t.info (driveParam (b, kDriveDb)).def == 0.0 && t.info (driveParam (b, kDriveDb)).max == kMaxDriveDb &&
                   t.info (driveParam (b, kDriveType)).def == (double)kDriveAnalog &&
                   std::string (t.info (driveParam (b, kDriveDb)).name) == "Band " + std::to_string (b + 1) + " Drive",
               "band %d: drive 0 dB (off), Analog", b + 1);
    CHECK (t.info (driveParam (0, kDriveType)).choices.size () == (size_t)kNumDriveTypes, "five drive types");
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
    const std::clock_t t0 = std::clock (); // CPU time: other programs running do not count
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
    const double secs = (double)(std::clock () - t0) / CLOCKS_PER_SEC;
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
        const std::clock_t t0 = std::clock (); // CPU time: other programs running do not count
        for (int b = 0; b < blocks; ++b)
        {
            if (b % 8 == 0)
                c.setParam (xoverParam (1), 600.0 + 800.0 * rnd ());
            for (int i = 0; i < 512; ++i)
                l[(size_t)i] = r[(size_t)i] = (float)(rnd () * 2.0 - 1.0) * 0.3f;
            c.process (l.data (), r.data (), l.data (), r.data (), 512);
        }
        const double secs = (double)(std::clock () - t0) / CLOCKS_PER_SEC;
        std::printf ("    %s, saturator on: %.2f%% of real time (stereo, 48 kHz)\n", kSlopeNames[s], 100.0 * secs / 10.0);
    }
    // and the crossovers alone
    {
        auto c = engine ([] (Engine& en) { en.setParam (kSlope, kSlope48); });
        const std::clock_t t0 = std::clock (); // CPU time: other programs running do not count
        const int blocks = (int)(10.0 * kSr / 512);
        for (int b = 0; b < blocks; ++b)
        {
            for (int i = 0; i < 512; ++i)
                l[(size_t)i] = r[(size_t)i] = (float)(rnd () * 2.0 - 1.0) * 0.3f;
            c->process (l.data (), r.data (), l.data (), r.data (), 512);
        }
        const double secs = (double)(std::clock () - t0) / CLOCKS_PER_SEC;
        std::printf ("    48 dB/oct crossovers alone: %.2f%% of real time\n", 100.0 * secs / 10.0);
    }
}

// ---- Bands and the drives ----

// A stereo signal: (sample index, channel) -> value.
using Signal = std::function<float (long long, int)>;

// Runs `n` samples of `sig` (from sample t on, which it advances) through e in blocks of `block`;
// appends the output.
static void run (Engine& e, const Signal& sig, long long& t, int n, std::vector<float>& outL, std::vector<float>* outR = nullptr,
                 int block = 480)
{
    std::vector<float> l ((size_t)block), r ((size_t)block);
    for (int pos = 0; pos < n; pos += block)
    {
        const int m = std::min (block, n - pos);
        for (int i = 0; i < m; ++i)
        {
            l[(size_t)i] = sig (t + i, 0);
            r[(size_t)i] = sig (t + i, 1);
        }
        e.process (l.data (), r.data (), l.data (), r.data (), m);
        outL.insert (outL.end (), l.begin (), l.begin () + m);
        if (outR)
            outR->insert (outR->end (), r.begin (), r.begin () + m);
        t += m;
    }
}

static Signal sine (double hz, double amp)
{
    return [hz, amp] (long long t, int) { return (float)(amp * std::sin (2.0 * M_PI * hz * (double)t / kSr)); };
}

// a fixed noise (its own on each channel)
static Signal noise (double amp, uint32_t seed = 1)
{
    return [amp, seed] (long long t, int c) {
        uint32_t s = (uint32_t)(t * 2 + c) * 2654435761u + seed * 97u;
        s ^= s >> 13;
        s *= 0x5bd1e995u;
        s ^= s >> 15;
        return (float)(amp * ((double)(s & 0xFFFFFF) / 8388608.0 - 1.0));
    };
}

// a spread of sines up to 9 kHz, well inside the oversampler's pass band (a different phase on each channel)
static Signal tones (double amp)
{
    return [amp] (long long t, int c) {
        static const double f[] = {63.0, 180.0, 440.0, 1250.0, 3100.0, 5300.0, 8900.0};
        double v = 0.0;
        for (int k = 0; k < 7; ++k)
            v += std::sin (2.0 * M_PI * f[k] * (double)t / kSr + 0.7 * k + 1.3 * c);
        return (float)(amp * v / 7.0);
    };
}

// the amplitude of the component at hz in x[a .. b) (Hann-windowed)
static double amplitudeAt (const std::vector<float>& x, size_t a, size_t b, double hz, double sr = kSr)
{
    std::complex<double> acc (0.0, 0.0);
    double wsum = 0.0;
    const size_t n = b - a;
    for (size_t i = 0; i < n; ++i)
    {
        const double w = 0.5 - 0.5 * std::cos (2.0 * M_PI * (double)i / (double)n);
        const double ph = -2.0 * M_PI * hz * (double)i / sr;
        acc += w * (double)x[a + i] * std::complex<double> (std::cos (ph), std::sin (ph));
        wsum += w;
    }
    return 2.0 * std::abs (acc) / wsum;
}

static void fft (std::vector<std::complex<double>>& a)
{
    const size_t n = a.size ();
    for (size_t i = 1, j = 0; i < n; ++i)
    {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j)
            std::swap (a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1)
    {
        const double ang = -2.0 * M_PI / (double)len;
        const std::complex<double> wl (std::cos (ang), std::sin (ang));
        for (size_t i = 0; i < n; i += len)
        {
            std::complex<double> w (1.0, 0.0);
            for (size_t k = 0; k < len / 2; ++k)
            {
                const auto u = a[i + k], v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
                w *= wl;
            }
        }
    }
}

// The strongest component that is not a harmonic of f0 (nor DC), in dB against the fundamental: what
// the curve's harmonics above Nyquist fold back as. x: 16384 samples from `a` on (Blackman-Harris).
static double worstAliasDb (const std::vector<float>& x, size_t a, double f0, double sr = kSr)
{
    constexpr size_t kN = 16384;
    std::vector<std::complex<double>> s (kN);
    for (size_t i = 0; i < kN; ++i)
    {
        const double p = 2.0 * M_PI * (double)i / (double)kN;
        const double w = 0.35875 - 0.48829 * std::cos (p) + 0.14128 * std::cos (2 * p) - 0.01168 * std::cos (3 * p);
        s[i] = w * (double)x[a + i];
    }
    fft (s);
    const double bin = sr / (double)kN;
    auto nearHarmonic = [&] (size_t k) {
        const double f = (double)k * bin;
        const double h = std::round (f / f0);
        return std::fabs (f - h * f0) <= 6.0 * bin; // (h 0: DC)
    };
    const double fund = std::abs (s[(size_t)std::lround (f0 / bin)]);
    double worst = 0.0;
    for (size_t k = 1; k < kN / 2; ++k)
        if (!nearHarmonic (k))
            worst = std::max (worst, std::abs (s[k]));
    return 20.0 * std::log10 (worst / fund + 1e-15);
}

// All of it that is not a harmonic of f0 (nor DC) below maxHz, in dB against the fundamental (as
// worstAliasDb, summed: the aliases' energy).
static double aliasEnergyDb (const std::vector<float>& x, size_t a, double f0, double maxHz, double sr = kSr)
{
    constexpr size_t kN = 16384;
    std::vector<std::complex<double>> s (kN);
    for (size_t i = 0; i < kN; ++i)
    {
        const double p = 2.0 * M_PI * (double)i / (double)kN;
        const double w = 0.35875 - 0.48829 * std::cos (p) + 0.14128 * std::cos (2 * p) - 0.01168 * std::cos (3 * p);
        s[i] = w * (double)x[a + i];
    }
    fft (s);
    const double bin = sr / (double)kN;
    const double fund = std::abs (s[(size_t)std::lround (f0 / bin)]);
    double sum = 0.0;
    for (size_t k = 1; k < kN / 2 && (double)k * bin < maxHz; ++k)
    {
        const double f = (double)k * bin;
        if (std::fabs (f - std::round (f / f0) * f0) > 6.0 * bin)
            sum += std::norm (s[k]);
    }
    return 10.0 * std::log10 (sum / (fund * fund) + 1e-30);
}

static void setBands (Engine& e, int count) { e.setParam (kBandCount, (double)(count - 1)); }
static void setDrive (Engine& e, int band, double db, int type)
{
    e.setParam (driveParam (band, kDriveDb), db);
    e.setParam (driveParam (band, kDriveType), (double)type);
}
static const char* kTypeNames[kNumDriveTypes] = {"Analog", "Tape", "Tube", "Hard Clip", "Fold"};

TEST (latency_is_constant)
{
    for (double sr : {44100.0, 48000.0, 96000.0})
    {
        // the latency is the drives' oversampler's (plus the end saturator's), whatever is set
        auto e = engine ({}, sr);
        const int lat = e->latency ();
        Engine withTail;
        withTail.prepare (sr, 512);
        std::printf ("    %.0f Hz: %d samples (with the end saturator %d)\n", sr, lat, withTail.latency ());
        CHECK (lat > 0 && lat == e->driveLatency (), "the drives' latency: %d", lat);
        const int tailLat = withTail.latency ();
        CHECK (tailLat > lat, "the end saturator's adds to it");
        bool same = true;
        std::vector<float> out;
        long long t = 0;
        for (int count = 1; count <= kBands; ++count)
            for (int type = 0; type < kNumDriveTypes; ++type)
                for (double db : {0.0, 12.0, 36.0})
                {
                    setBands (*e, count);
                    for (int b = 0; b < kBands; ++b)
                        setDrive (*e, b, db, type);
                    run (*e, noise (0.3), t, 256, out);
                    same &= e->latency () == lat;
                    setBands (withTail, count);
                    setDrive (withTail, 0, db, type);
                    same &= withTail.latency () == tailLat;
                }
        CHECK (same, "the same latency at every Bands, Drive and Type");
    }

    // one band, no drive: the input exactly, latency () samples later
    {
        auto e = engine ([] (Engine& en) { setBands (en, 1); });
        const int lat = e->latency ();
        std::vector<float> outL, outR;
        long long t = 0;
        const Signal in = noise (0.5, 7);
        run (*e, in, t, 9000, outL, &outR, 333);
        bool exact = true;
        for (int i = 0; i < 9000; ++i)
        {
            const float wantL = i < lat ? 0.0f : in (i - lat, 0), wantR = i < lat ? 0.0f : in (i - lat, 1);
            exact &= outL[(size_t)i] == wantL && outR[(size_t)i] == wantR;
        }
        CHECK (exact, "one band: the input, %d samples later, bit for bit", lat);

        // switched off (built into another plug-in): the same delay
        auto b = engine ();
        std::vector<float> l (9000), r (9000);
        for (int i = 0; i < 9000; ++i)
        {
            l[(size_t)i] = in (i, 0);
            r[(size_t)i] = in (i, 1);
        }
        for (int pos = 0; pos < 9000; pos += 500)
            b->processBypassed (l.data () + pos, r.data () + pos, 500);
        bool delayed = true;
        for (int i = 0; i < 9000; ++i)
            delayed &= l[(size_t)i] == (i < lat ? 0.0f : in (i - lat, 0)) && r[(size_t)i] == (i < lat ? 0.0f : in (i - lat, 1));
        CHECK (delayed, "bypassed: the same %d samples of delay", lat);
    }

    // a driven band lines up with the clean ones: Hard Clip below its clip is the band itself (its auto
    // gain undoes the drive), so with it on the output nulls against the clean output
    for (int band : {0, 1, 3})
    {
        auto clean = engine ();
        auto driven = engine ([band] (Engine& en) { setDrive (en, band, 6.0, kDriveHard); });
        std::vector<float> a, b;
        long long ta = 0, tb = 0;
        const int n = (int)(0.5 * kSr);
        run (*clean, tones (0.08), ta, n, a);
        run (*driven, tones (0.08), tb, n, b);
        double sig = 0.0, diff = 0.0;
        for (size_t i = (size_t)n / 2; i < (size_t)n; ++i)
        {
            sig += (double)a[i] * a[i];
            diff += ((double)a[i] - b[i]) * ((double)a[i] - b[i]);
        }
        const double nullDb = 10.0 * std::log10 (diff / sig + 1e-30);
        std::printf ("    band %d driven (Hard Clip 6 dB, under its clip) against clean: %.1f dB\n", band + 1, nullDb);
        CHECK (nullDb < -40.0, "band %d: a driven band stays in time with the rest (%.1f dB)", band + 1, nullDb);
    }
}

TEST (drive_off_is_clean)
{
    // Drive at 0 dB (every type) is no drive, bit for bit
    const int n = (int)(0.4 * kSr);
    std::vector<float> ref, refR;
    {
        auto e = engine ([] (Engine& en) {
            en.setParam (kSlope, kSlope48);
            en.setParam (bandParam (1, kGain), 5.0);
        });
        long long t = 0;
        run (*e, noise (0.4), t, n, ref, &refR);
    }
    for (int type = 0; type < kNumDriveTypes; ++type)
    {
        auto e = engine ([type] (Engine& en) {
            en.setParam (kSlope, kSlope48);
            en.setParam (bandParam (1, kGain), 5.0);
            for (int b = 0; b < kBands; ++b)
                setDrive (en, b, 0.0, type);
        });
        std::vector<float> l, r;
        long long t = 0;
        run (*e, noise (0.4), t, n, l, &r);
        CHECK (l == ref && r == refR, "%s at 0 dB: the clean output, bit for bit", kTypeNames[type]);
    }

    // a drive turned on and back off: once it has faded out, the output is the clean one again, bit for bit
    {
        auto clean = engine ();
        auto e = engine ();
        std::vector<float> a, b;
        long long ta = 0, tb = 0;
        run (*clean, noise (0.4), ta, 4800, a);
        run (*e, noise (0.4), tb, 4800, b);
        setDrive (*e, 1, 24.0, kDriveTube);
        setDrive (*e, 3, 12.0, kDriveFold);
        run (*clean, noise (0.4), ta, 9600, a);
        run (*e, noise (0.4), tb, 9600, b);
        double diff = 0.0;
        for (size_t i = 4800; i < a.size (); ++i)
            diff = std::max (diff, (double)std::fabs (a[i] - b[i]));
        CHECK (diff > 0.01, "the drives do something: %.3f", diff);
        setDrive (*e, 1, 0.0, kDriveTube);
        setDrive (*e, 3, 0.0, kDriveFold);
        run (*clean, noise (0.4), ta, 4800, a); // (15 ms fades)
        run (*e, noise (0.4), tb, 4800, b);
        const size_t from = a.size ();
        run (*clean, noise (0.4), ta, 9600, a);
        run (*e, noise (0.4), tb, 9600, b);
        bool exact = true;
        for (size_t i = from; i < a.size (); ++i)
            exact &= a[i] == b[i];
        CHECK (exact, "off again: the clean output, bit for bit");
    }

    // a drive on a band past the count does nothing
    {
        auto two = engine ([] (Engine& en) { setBands (en, 2); });
        auto twoDriven = engine ([] (Engine& en) {
            setBands (en, 2);
            setDrive (en, 2, 36.0, kDriveHard);
            setDrive (en, 3, 36.0, kDriveFold);
        });
        std::vector<float> a, b;
        long long ta = 0, tb = 0;
        run (*two, noise (0.4), ta, n, a);
        run (*twoDriven, noise (0.4), tb, n, b);
        CHECK (a == b, "Bands 2: the drives of bands 3 and 4 are off");
    }
}

TEST (each_type_saturates)
{
    // one band (the whole signal through band 1's drive), a sine at the auto gain's level (-12 dBFS peak)
    const double f0 = 220.0;
    double second[kNumDriveTypes] {};
    for (int type = 0; type < kNumDriveTypes; ++type)
        for (double driveDb : {0.0, 6.0, 18.0})
        {
            auto e = engine ([&] (Engine& en) {
                setBands (en, 1);
                setDrive (en, 0, driveDb, type);
            });
            std::vector<float> out;
            long long t = 0;
            const int n = (int)(0.6 * kSr);
            run (*e, sine (f0, kDriveRefPeak), t, n, out);
            const size_t a = (size_t)n / 3, b = (size_t)n;
            const double h1 = amplitudeAt (out, a, b, f0);
            double hSum = 0.0;
            for (int h = 2; h <= 15; ++h)
            {
                const double ah = amplitudeAt (out, a, b, f0 * h);
                hSum += ah * ah;
                if (h == 2 && driveDb == 18.0)
                    second[type] = 20.0 * std::log10 (ah / h1 + 1e-12);
            }
            const double thd = std::sqrt (hSum) / h1, levelDb = 20.0 * std::log10 (h1 / kDriveRefPeak);
            std::printf ("    %-9s Drive %4.1f dB: THD %6.2f%%, fundamental %+.2f dB\n", kTypeNames[type], driveDb, 100.0 * thd,
                         levelDb);
            if (driveDb == 0.0)
                CHECK (thd < 1e-4, "%s at 0 dB: clean (THD %.4f%%)", kTypeNames[type], 100.0 * thd);
            else if (driveDb == 18.0)
            {
                CHECK (thd > 0.03, "%s at 18 dB saturates: THD %.2f%%", kTypeNames[type], 100.0 * thd);
                // (the auto gain keeps the level near where it was)
                CHECK (std::fabs (levelDb) < 4.0, "%s at 18 dB: the level stays (%.2f dB)", kTypeNames[type], levelDb);
            }
        }
    std::printf ("    2nd harmonic at 18 dB: Analog %.1f, Tape %.1f, Tube %.1f, Hard %.1f, Fold %.1f dB\n", second[0], second[1],
                 second[2], second[3], second[4]);
    CHECK (second[kDriveTube] > -35.0, "Tube: even harmonics (%.1f dB)", second[kDriveTube]);
    CHECK (second[kDriveTape] < -70.0 && second[kDriveHard] < -70.0, "Tape and Hard Clip: symmetric (%.1f, %.1f dB)", second[kDriveTape],
           second[kDriveHard]);
}

TEST (aliasing_is_low)
{
    // one band, a high sine driven hard: what folds back below Nyquist, against the same curve run at
    // the plain sample rate (no oversampling)
    for (double f0 : {1003.7, 4987.3}) // (not divisors of the rate)
    for (int type = 0; type < kNumDriveTypes; ++type)
    {
        const double driveDb = 18.0;
        auto e = engine ([&] (Engine& en) {
            setBands (en, 1);
            setDrive (en, 0, driveDb, type);
        });
        std::vector<float> out;
        long long t = 0;
        run (*e, sine (f0, kDriveRefPeak), t, 16384 + 9600, out);
        const double over = worstAliasDb (out, 9600, f0);
        // the same curve at 1x
        const auto& makeup = DriveMakeup::get ();
        const float g = (float)std::pow (10.0, driveDb / 20.0), mk = makeup.at (type, driveDb);
        std::vector<float> naive (16384);
        for (size_t i = 0; i < naive.size (); ++i)
            naive[i] = mk * driveCurve (type, g * sine (f0, kDriveRefPeak) ((long long)i, 0));
        const double plain = worstAliasDb (naive, 0, f0);
        std::printf ("    %-9s at %.0f Hz, 18 dB: worst alias %.1f dB (without oversampling %.1f dB)\n", kTypeNames[type], f0,
                     over, plain);
        // (a 5 kHz sine hard-clipped at 18 dB is the worst case: its harmonics are strong far past 4x)
        CHECK (over < (f0 < 2000.0 ? -60.0 : -35.0), "%s at %.0f Hz: aliasing %.1f dB", kTypeNames[type], f0, over);
        CHECK (over < plain - 10.0 || over < -80.0, "%s: oversampling helps (%.1f against %.1f dB)", kTypeNames[type], over, plain);
    }
}

TEST (drive_oversampling)
{
    // Oversampling: 4x by default (what the drives always ran at, so the latency an old project knows),
    // 2x the first half-band stage alone, Off no latency at all; each exact (one band, no drive: the input
    // that many samples later, bit for bit, run or bypassed) and the same at every Drive while it is held
    CHECK (std::lround (toPlain (kDriveOversampling, defaultNormalized (kDriveOversampling))) == kDriveOs4x, "4x by default");
    int lat[3];
    for (int m = 0; m < 3; ++m)
    {
        auto e = engine ([m] (Engine& en) {
            setBands (en, 1);
            en.setParam (kDriveOversampling, m);
        });
        lat[m] = e->latency ();
        std::vector<float> outL;
        long long t = 0;
        const Signal in = noise (0.5, 3);
        run (*e, in, t, 6000, outL, nullptr, 333);
        bool exact = true;
        for (int i = 0; i < 6000; ++i)
            exact &= outL[(size_t)i] == (i < lat[m] ? 0.0f : in (i - lat[m], 0));
        CHECK (exact, "Oversampling %d: the input %d samples later, bit for bit", m, lat[m]);
        setDrive (*e, 0, 24.0, kDriveTape);
        run (*e, in, t, 3000, outL);
        CHECK (e->latency () == lat[m], "Oversampling %d: the same latency with the drive on", m);
        auto b = engine ([m] (Engine& en) { en.setParam (kDriveOversampling, m); });
        std::vector<float> l (3000), r (3000);
        for (int i = 0; i < 3000; ++i)
            l[(size_t)i] = r[(size_t)i] = in (i, 0);
        for (int pos = 0; pos < 3000; pos += 500)
            b->processBypassed (l.data () + pos, r.data () + pos, 500);
        bool delayed = true;
        for (int i = 0; i < 3000; ++i)
            delayed &= l[(size_t)i] == (i < lat[m] ? 0.0f : in (i - lat[m], 0));
        CHECK (delayed, "Oversampling %d bypassed: the same %d samples", m, lat[m]);
    }
    std::printf ("    latency at 48 kHz: Off %d, 2x %d, 4x %d samples\n", lat[0], lat[1], lat[2]);
    CHECK (lat[0] == 0 && lat[1] > 0 && lat[1] < lat[2] && lat[2] == engine ()->latency (), "Off 0 < 2x < 4x (the default)");

    // switched while running: the next block runs at the new latency, and the meters say so
    {
        Meters m;
        auto e = engine ([] (Engine& en) { setBands (en, 1); });
        e->setMeters (&m);
        CHECK (m.driveLatency.load () == lat[2], "published: 4x's %d", m.driveLatency.load ());
        std::vector<float> out;
        long long t = 0;
        run (*e, noise (0.3), t, 2000, out);
        e->setParam (kDriveOversampling, kDriveOsOff);
        CHECK (e->latency () == 0, "reports Off's once set");
        out.clear ();
        const Signal in = noise (0.3, 9);
        long long t2 = 0;
        run (*e, in, t2, 2000, out, nullptr, 500);
        bool exact = true;
        for (int i = 0; i < 2000; ++i)
            exact &= out[(size_t)i] == in (i, 0);
        CHECK (exact && m.driveLatency.load () == 0, "runs at Off's: no delay (%d)", m.driveLatency.load ());
    }

    // aliasing: a 5 kHz sine hard-clipped at 18 dB (the worst case): what folds back below 19 kHz (above
    // it the downsampling filter's transition is the same at 2x and 4x) is the least at 4x
    double worst[3];
    for (int m = 0; m < 3; ++m)
    {
        auto e = engine ([m] (Engine& en) {
            setBands (en, 1);
            setDrive (en, 0, 18.0, kDriveHard);
            en.setParam (kDriveOversampling, m);
        });
        std::vector<float> out;
        long long t = 0;
        run (*e, sine (4987.3, kDriveRefPeak), t, 16384 + 9600, out);
        worst[m] = aliasEnergyDb (out, 9600, 4987.3, 19000.0);
    }
    std::printf ("    Hard Clip 18 dB at 5 kHz, alias energy below 19 kHz: Off %.1f, 2x %.1f, 4x %.1f dB\n", worst[0], worst[1], worst[2]);
    CHECK (worst[1] < worst[0] - 6.0 && worst[2] < worst[1] - 3.0, "4x the least aliasing, then 2x, then Off");
}

TEST (bands_count)
{
    CHECK (bandsOf (0.0) == 1 && bandsOf (3.0) == 4 && bandsOf (-5.0) == 1 && bandsOf (9.0) == 4, "the choice as a count");
    // N bands at 0 dB: flat (an all-pass), and the display's model of them matches
    for (int count = 1; count <= kBands; ++count)
        for (int s : {kSlope12, kSlope48, kSlope96})
        {
            auto e = engine ([&] (Engine& en) {
                setBands (en, count);
                en.setParam (kSlope, s);
            });
            CHECK (e->bandsInUse () == count, "%d bands in use", count);
            const auto h = impulse (*e);
            const double xo[kCrossovers] = {120.0, 1000.0, 6000.0};
            const double ones[kBands] = {1.0, 1.0, 1.0, 1.0};
            double worst = 0.0, worstModel = 0.0;
            for (double f : logFreqs (40, 20.0, 20000.0))
            {
                const auto r = at (h, f);
                worst = std::max (worst, std::fabs (db (r)));
                worstModel = std::max (worstModel, std::abs (r - totalResponse (xo, s, ones, f, kSr, count)));
            }
            CHECK (worst < 0.1 && worstModel < 0.003, "%d bands, %s: flat within %.3f dB, model off by %.5f", count, kSlopeNames[s],
                   worst, worstModel);
        }
    // two bands: band 2 is everything above crossover 1; bands 3 and 4 (their gain, mute, solo) are not there
    {
        auto e = engine ([] (Engine& en) {
            setBands (en, 2);
            en.setParam (bandParam (1, kGain), 12.0);
            en.setParam (bandParam (2, kGain), -24.0);
            en.setParam (bandParam (3, kSolo), 1.0);
        });
        const auto h = impulse (*e);
        const double low = db (at (h, 40.0)), mid = db (at (h, 2500.0)), high = db (at (h, 12000.0));
        std::printf ("    2 bands, band 2 +12 (band 3 at -24, band 4 soloed: unused): %.2f dB at 40 Hz, %.2f at 2.5 kHz, %.2f at 12 kHz\n",
                     low, mid, high);
        CHECK (std::fabs (low) < 0.5 && std::fabs (mid - 12.0) < 0.5 && std::fabs (high - 12.0) < 0.5, "2 bands: band 2 is the rest");
        double g[kBands];
        bandGains ([&] (uint32_t id) { return e->param (id); }, g, 2);
        CHECK (g[0] == 1.0 && g[1] > 3.9, "a solo past the count doesn't silence the bands in use");
    }
    // one band: bands 2 .. 4 don't count; band 1's gain is the whole
    {
        auto e = engine ([] (Engine& en) {
            setBands (en, 1);
            en.setParam (bandParam (0, kGain), -6.0);
        });
        const auto h = impulse (*e);
        CHECK (std::fabs (db (at (h, 50.0)) + 6.0) < 0.05 && std::fabs (db (at (h, 10000.0)) + 6.0) < 0.05, "1 band: its gain everywhere");
    }
}

TEST (drive_and_bands_changes_are_click_free)
{
    // a 200 Hz sine in band 2: while the band count changes, a drive comes on, changes type, goes off,
    // the largest step between samples stays near the larger of the settled signal's before and after
    // (a driven sine has steeper edges of its own)
    const double amp = 0.25, normal = amp * 2.0 * M_PI * 200.0 / kSr;
    auto e = engine ();
    std::vector<float> out;
    long long t = 0;
    auto worstStep = [&] (int blocks) {
        const size_t from = out.size ();
        run (*e, sine (200.0, amp), t, blocks * 480, out);
        double worst = 0.0;
        for (size_t i = std::max<size_t> (from, 1); i < out.size (); ++i)
            worst = std::max (worst, (double)std::fabs (out[i] - out[i - 1]));
        return worst;
    };
    double before = worstStep (20);
    struct Step
    {
        const char* what;
        std::function<void ()> change;
    };
    const Step steps[] = {
        {"Bands 4 -> 1", [&] { setBands (*e, 1); }},
        {"Bands 1 -> 3", [&] { setBands (*e, 3); }},
        {"Bands 3 -> 2", [&] { setBands (*e, 2); }},
        {"drive on (Tape 12 dB)", [&] { setDrive (*e, 1, 12.0, kDriveTape); }},
        {"Tape -> Tube", [&] { setDrive (*e, 1, 12.0, kDriveTube); }},
        {"Tube -> Fold", [&] { setDrive (*e, 1, 12.0, kDriveFold); }},
        {"12 -> 24 dB", [&] { setDrive (*e, 1, 24.0, kDriveFold); }},
        {"Bands 2 -> 4, driven", [&] { setBands (*e, 4); }},
        {"drive off", [&] { setDrive (*e, 1, 0.0, kDriveFold); }},
    };
    for (const auto& s : steps)
    {
        s.change ();
        const double w = worstStep (10), after = worstStep (10);
        const double settled = std::max ({before, after, normal});
        std::printf ("    %-22s largest step %.4f (settled %.4f before, %.4f after; the sine's own %.4f)\n", s.what, w, before,
                     after, normal);
        CHECK (w < 1.25 * settled, "%s: no click (%.4f against %.4f)", s.what, w, settled);
        before = after;
    }
    CHECK (e->bandsInUse () == 4, "4 bands again");
}

TEST (old_state_migration)
{
    // a 0.6.0 state (version 2): every ID up to the end saturator's third block, none of the new ones.
    // Whatever the new places hold, it loads with four bands and every drive off
    double norm[kNumParams];
    bool has[kNumParams];
    for (uint32_t id = 0; id < kNumParams; ++id)
    {
        has[id] = id < kBandCount;
        norm[id] = has[id] ? defaultNormalized (id) : 0.0; // (0: as a host reading them missing might)
    }
    norm[xoverParam (1)] = toNormalized (xoverParam (1), 2500.0);
    migrateState (2, norm, has);
    CHECK (bandsOf (toPlain (kBandCount, norm[kBandCount])) == 4, "four bands");
    for (int b = 0; b < kBands; ++b)
        CHECK (toPlain (driveParam (b, kDriveDb), norm[driveParam (b, kDriveDb)]) == 0.0, "band %d's drive off", b + 1);
    CHECK (std::fabs (toPlain (xoverParam (1), norm[xoverParam (1)]) - 2500.0) < 0.5, "the rest as it was saved");
    CHECK (kFirstAddedAfter060 == kBandCount && kEndAddedAfter060 == kNumParams, "the IDs added after 0.6.0");
    CHECK (defaultNormalized (kBandCount) == 1.0, "Bands: 4 is normalized 1");

    // version 1: its three slopes among the eight
    for (int old = 0; old < 3; ++old)
    {
        double n1[kNumParams];
        bool h1[kNumParams] {};
        for (uint32_t id = 0; id < kNumParams; ++id)
            n1[id] = defaultNormalized (id);
        n1[kSlope] = old / 2.0;
        h1[kSlope] = true;
        migrateState (1, n1, h1);
        const int want[3] = {kSlope12, kSlope24, kSlope48};
        CHECK (std::lround (toPlain (kSlope, n1[kSlope])) == want[old], "version 1's slope %d", old);
    }
    // version 3: the end saturator's Sub band on (its Range kept), its High band off (Range 0)
    {
        double n[kNumParams];
        bool h[kNumParams];
        for (uint32_t id = 0; id < kNumParams; ++id)
        {
            n[id] = 0.5;
            h[id] = true;
        }
        n[kTailExt2Base + pk::kTailExt2Sub] = 1.0;
        n[kTailExt3Base + pk::kTailExt3High] = 0.0;
        migrateState (3, n, h);
        CHECK (n[kTailExt2Base + pk::kTailExt2SubRange] == 0.5 && n[kTailExt3Base + pk::kTailExt3HighRange] == 0.0 && n[kSlope] == 0.5,
               "version 3: Sub (on) kept, High (off) at 0, the rest as saved");
    }
    // version 5: the end saturator's Oversampling was its Hi-Quality switch (on 4x, off Off, a value in
    // between on the end the switch read it as); the drives' Oversampling, not in it, is 4x (their default)
    for (double hq : {0.0, 0.3, 0.7, 1.0})
    {
        double n[kNumParams];
        bool h[kNumParams];
        for (uint32_t id = 0; id < kNumParams; ++id)
        {
            n[id] = defaultNormalized (id);
            h[id] = id != kDriveOversampling;
        }
        const uint32_t os = kTailExtBase + pk::kTailExtOversampling;
        n[os] = hq;
        migrateState (5, n, h);
        const int want = hq >= 0.5 ? 2 : 0;
        CHECK (std::lround (toPlain (os, n[os])) == want, "Hi-Quality %.1f: %s", hq, want ? "4x" : "Off");
        CHECK (std::lround (toPlain (kDriveOversampling, n[kDriveOversampling])) == kDriveOs4x, "the drives at 4x");
    }
    // this version's own state is kept as it is
    double n3[kNumParams];
    bool h3[kNumParams];
    for (uint32_t id = 0; id < kNumParams; ++id)
    {
        n3[id] = 0.25;
        h3[id] = true;
    }
    migrateState (kStateVersion, n3, h3);
    bool kept = true;
    for (uint32_t id = 0; id < kNumParams; ++id)
        kept &= n3[id] == 0.25;
    CHECK (kept, "version %d: unchanged", kStateVersion);
}

TEST (cpu_with_drives)
{
    // every band driven (each a different type), 96 dB/oct, the end saturator on: the processor's own
    // CPU time, the best of three renders of 5 s
    auto cpu = [] (bool drives) {
        double best = 1e9;
        for (int rep = 0; rep < 3; ++rep)
        {
            Engine c;
            c.setParam (kSlope, kSlope96);
            c.setParam (kTailBase + pk::kTailOn, 1.0);
            if (drives)
                for (int b = 0; b < kBands; ++b)
                    setDrive (c, b, 12.0 + 4.0 * b, (b + 1) % kNumDriveTypes);
            c.prepare (kSr, 512);
            std::vector<float> out;
            out.reserve ((size_t)(5.0 * kSr) + 512);
            long long t = 0;
            const std::clock_t t0 = std::clock ();
            run (c, tones (0.3), t, (int)(5.0 * kSr), out, nullptr, 512);
            best = std::min (best, (double)(std::clock () - t0) / CLOCKS_PER_SEC);
        }
        return best / 5.0;
    };
    const double clean = cpu (false), driven = cpu (true);
    std::printf ("    96 dB/oct, saturator on: %.2f%% of real time; every band driven: %.2f%%\n", 100.0 * clean, 100.0 * driven);
    CHECK (driven < 0.25, "every band driven: %.2f%% of real time", 100.0 * driven);
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
