// Headless tests for the Locus DSP. Run: ./locus_tests [filter]
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

using namespace locus;

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
        const int n = (int)std::min<size_t> ((size_t)block, in.l.size () - pos);
        e.process (in.l.data () + pos, in.r.data () + pos, out.l.data () + pos, out.r.data () + pos, n);
    }
    return out;
}

// Level of a single frequency in [a, b) (least-squares fit of sin/cos).
static double toneDb (const std::vector<float>& x, double f, size_t a, size_t b)
{
    double s = 0, c = 0;
    for (size_t i = a; i < b; ++i)
    {
        s += x[i] * std::sin (2.0 * M_PI * f * i / kSr);
        c += x[i] * std::cos (2.0 * M_PI * f * i / kSr);
    }
    const double amp = 2.0 * std::sqrt (s * s + c * c) / (double)(b - a);
    return 20.0 * std::log10 (std::max (1e-12, amp));
}

// The same over a short window: Hann-weighted so a strong neighbour 25 Hz away does not leak in.
static double toneDbWindowed (const std::vector<float>& x, double f, size_t a, size_t b)
{
    double s = 0, c = 0, wsum = 0;
    for (size_t i = a; i < b; ++i)
    {
        const double w = 0.5 - 0.5 * std::cos (2.0 * M_PI * (double)(i - a) / (double)(b - a));
        s += w * x[i] * std::sin (2.0 * M_PI * f * i / kSr);
        c += w * x[i] * std::cos (2.0 * M_PI * f * i / kSr);
        wsum += w;
    }
    const double amp = 2.0 * std::sqrt (s * s + c * c) / wsum;
    return 20.0 * std::log10 (std::max (1e-12, amp));
}

static double rmsDb (const std::vector<float>& x, size_t a, size_t b)
{
    double s = 0;
    for (size_t i = a; i < b; ++i)
        s += (double)x[i] * x[i];
    return 10.0 * std::log10 (std::max (1e-24, s / (double)(b - a)));
}

static std::unique_ptr<Engine> engine ()
{
    auto e = std::make_unique<Engine> ();
    e->prepare (kSr, 512);
    return e;
}

// ---------------------------------------------------------------------------
TEST (params)
{
    const auto& t = paramTable ();
    CHECK (t.size () == kNumParams, "size");
    CHECK (kNumParams == kTailExt2Base + pk::kTailExt2Fields &&
               std::string (t.info (kTailExt2Base + pk::kTailExt2Advanced).name) == "Saturator Gently Advanced" &&
               t.info (kTailExt2Base + pk::kTailExt2Threshold).def == -18.0 && t.info (kTailExt2Base + pk::kTailExt2Advanced).def == 0.0,
           "Gently's Advanced block (the end saturator's) is the last");
    for (uint32_t id = 0; id < kNumParams; ++id)
    {
        double v;
        CHECK (t.fromText (id, t.toText (id, t.info (id).def), v), "%s", t.info (id).name);
    }
    CHECK (t.toText (kContrast, -0.5) == "-50 %", "%s", t.toText (kContrast, -0.5).c_str ());
}

TEST (transparent_at_zero_contrast)
{
    // Contrast 0, Gain 0: the output is the input delayed by exactly latency() samples.
    auto e = engine ();
    Sig in;
    uint32_t seed = 3;
    in.l.resize (48000);
    in.r.resize (48000);
    for (size_t i = 0; i < in.l.size (); ++i)
    {
        seed = seed * 1664525u + 1013904223u;
        in.l[i] = (float)(((seed >> 8) & 0xFFFF) / 32768.0 - 1.0) * 0.5f;
        in.r[i] = -in.l[i] * 0.7f;
    }
    auto out = run (*e, in, 333);
    const int lat = e->latency ();
    double err = 0;
    for (size_t i = (size_t)lat + 2 * 4096; i < in.l.size (); ++i)
    {
        err = std::max (err, (double)std::fabs (out.l[i] - in.l[i - (size_t)lat]));
        err = std::max (err, (double)std::fabs (out.r[i] - in.r[i - (size_t)lat]));
    }
    CHECK (err < 1e-4, "reconstruction error %g (latency %d)", err, lat);
    CHECK (lat > 4096 && lat < 4096 + 200, "latency %d at 48 kHz (STFT + end-of-chain saturator)", lat);
}

TEST (contrast_widens_and_narrows_level_differences)
{
    // A strong 55 Hz fundamental with a weak component between its harmonics (80 Hz).
    auto measure = [] (double contrast, int mode) {
        auto e = engine ();
        e->setParam (kContrast, contrast);
        e->setParam (kMode, mode);
        auto in = tones ({{55.0, -6.0}, {80.0, -26.0}}, 4.0);
        auto out = run (*e, in);
        const size_t a = 2 * 48000, b = 4 * 48000;
        return toneDb (out.l, 80.0, a, b) - toneDb (out.l, 55.0, a, b); // weak relative to strong
    };
    for (int mode : {kPunchy, kSmooth})
    {
        const double base = measure (0.0, mode);
        const double plus = measure (1.0, mode);
        const double minus = measure (-1.0, mode);
        CHECK (std::fabs (base + 20.0) < 0.5, "unprocessed difference %f", base);
        CHECK (plus < base - 6.0, "mode %d: +100%% contrast should push the weak component down: %f vs %f", mode, plus, base);
        CHECK (minus > base + 4.0, "mode %d: -100%% contrast should bring it closer: %f vs %f", mode, minus, base);
        const double half = measure (0.5, mode);
        CHECK (half < base - 2.0 && half > plus, "mode %d: +50%% is in between: %f", mode, half);
    }
}

TEST (level_is_kept_and_range_respected)
{
    auto e = engine ();
    e->setParam (kContrast, 1.0);
    // bass mix + a 2 kHz tone outside the range
    auto in = tones ({{55.0, -6.0}, {80.0, -24.0}, {110.0, -14.0}, {2000.0, -12.0}}, 4.0);
    auto out = run (*e, in);
    const size_t a = 2 * 48000, b = 4 * 48000;
    const int lat = e->latency ();
    CHECK (std::fabs (toneDb (out.l, 2000.0, a, b) + 12.0) < 0.05, "2 kHz untouched: %f", toneDb (out.l, 2000.0, a, b));
    // low-range loudness roughly unchanged (contrast reshapes, it doesn't turn it up or down)
    auto lowOnly = tones ({{55.0, -6.0}, {80.0, -24.0}, {110.0, -14.0}}, 4.0);
    Sig outLow = run (*engine (), lowOnly); // reference path (contrast 0) for alignment
    double sumIn = 0, sumOut = 0;
    for (double f : {55.0, 80.0, 110.0})
    {
        sumIn += std::pow (10.0, toneDb (outLow.l, f, a, b) / 10.0);
        sumOut += std::pow (10.0, toneDb (out.l, f, a, b) / 10.0);
    }
    const double change = 10.0 * std::log10 (sumOut / sumIn);
    CHECK (std::fabs (change) < 1.5, "low-range loudness change %f dB", change);
    (void)lat;
}

TEST (gain_and_solo)
{
    auto e = engine ();
    e->setParam (kGain, 6.0);
    auto in = tones ({{60.0, -12.0}, {3000.0, -12.0}}, 2.0);
    auto out = run (*e, in);
    const size_t a = 48000, b = 96000;
    CHECK (std::fabs (toneDb (out.l, 60.0, a, b) + 6.0) < 0.2, "gain +6 in range: %f", toneDb (out.l, 60.0, a, b));
    CHECK (std::fabs (toneDb (out.l, 3000.0, a, b) + 12.0) < 0.05, "outside unchanged: %f", toneDb (out.l, 3000.0, a, b));
    e->setParam (kGain, 0.0);
    e->setParam (kSolo, 1.0);
    e->reset ();
    out = run (*e, in);
    CHECK (std::fabs (toneDb (out.l, 60.0, a, b) + 12.0) < 0.2, "solo keeps the range: %f", toneDb (out.l, 60.0, a, b));
    CHECK (toneDb (out.l, 3000.0, a, b) < -80.0, "solo removes the rest: %f", toneDb (out.l, 3000.0, a, b));
    e->setParam (kSolo, 0.0);
    e->setParam (kOutput, -6.0);
    e->reset ();
    out = run (*e, in);
    CHECK (std::fabs (toneDb (out.l, 3000.0, a, b) + 18.0) < 0.1, "output -6 dB: %f", toneDb (out.l, 3000.0, a, b));
}

TEST (level_jumps_do_not_pump)
{
    // A 60 Hz tone that jumps up 12 dB. Contrast must not react to the jump itself (it used to
    // boost every hit by up to 12 dB and then settle, audible as a "wub" on expanded or gated
    // bass): the level right after the jump equals the settled level, in both modes.
    auto levelAfter = [] (int mode, double seconds) {
        auto e = engine ();
        e->setParam (kContrast, 1.0);
        e->setParam (kMode, mode);
        auto quiet = tones ({{60.0, -30.0}}, 2.0);
        auto loud = tones ({{60.0, -18.0}}, 3.0);
        Sig in = quiet;
        in.l.insert (in.l.end (), loud.l.begin (), loud.l.end ());
        in.r = in.l;
        auto out = run (*e, in);
        const size_t start = (size_t)((2.0 + seconds) * kSr) + (size_t)e->latency ();
        return toneDb (out.l, 60.0, start, start + 4800);
    };
    for (int mode : {kPunchy, kSmooth})
    {
        const double settled = levelAfter (mode, 2.0);
        CHECK (std::fabs (settled + 18.0) < 1.0, "mode %d: settled level %f", mode, settled);
        for (double t : {0.15, 0.3, 0.6})
            CHECK (std::fabs (levelAfter (mode, t) - settled) < 1.0, "mode %d: %.2f s after the jump: %f vs settled %f",
                   mode, t, levelAfter (mode, t), settled);
    }
}

TEST (punchy_follows_the_spectrum_faster_than_smooth)
{
    // A weak 80 Hz component appears next to a steady 55 Hz tone. With positive contrast it ends
    // up pushed down; Punchy reaches that state sooner than Smooth.
    auto relativeAfter = [] (int mode, double seconds) {
        auto e = engine ();
        e->setParam (kContrast, 1.0);
        e->setParam (kMode, mode);
        auto before = tones ({{55.0, -6.0}}, 2.0);
        auto after = tones ({{55.0, -6.0}, {80.0, -26.0}}, 3.0);
        Sig in = before;
        in.l.insert (in.l.end (), after.l.begin (), after.l.end ());
        in.r = in.l;
        auto out = run (*e, in);
        const size_t start = (size_t)((2.0 + seconds) * kSr) + (size_t)e->latency ();
        const size_t end = start + 14400; // 0.3 s
        return toneDbWindowed (out.l, 80.0, start, end) - toneDbWindowed (out.l, 55.0, start, end) + 20.0; // change of the difference
    };
    const double punchySettled = relativeAfter (kPunchy, 2.0), smoothSettled = relativeAfter (kSmooth, 2.0);
    CHECK (punchySettled < -6.0 && smoothSettled < -6.0, "settled: punchy %f smooth %f", punchySettled, smoothSettled);
    const double punchy = relativeAfter (kPunchy, 0.15), smooth = relativeAfter (kSmooth, 0.15);
    CHECK (std::fabs (punchy - punchySettled) < 3.0, "punchy is nearly there after 0.15 s: %f vs %f", punchy, punchySettled);
    CHECK (std::fabs (smooth - smoothSettled) > std::fabs (punchy - punchySettled) + 2.0,
           "smooth is still on its way: %f (settled %f) vs punchy %f (settled %f)", smooth, smoothSettled, punchy, punchySettled);
}

TEST (fuzz_and_automation)
{
    uint32_t seed = 11;
    auto rnd = [&] {
        seed = seed * 1664525u + 1013904223u;
        return (double)((seed >> 8) & 0xFFFFFF) / 16777216.0;
    };
    for (int iter = 0; iter < 60; ++iter)
    {
        Engine e;
        e.prepare (iter % 3 == 0 ? 96000.0 : kSr, 512);
        Sig in;
        in.l.resize (40000);
        in.r.resize (40000);
        for (size_t i = 0; i < in.l.size (); ++i)
        {
            in.l[i] = (float)((rnd () * 2 - 1) * ((i / 3000) % 2 ? 1.0 : 1e-4));
            in.r[i] = (float)(rnd () * 2 - 1) * 0.3f;
        }
        Sig out;
        out.l.resize (in.l.size ());
        out.r.resize (in.r.size ());
        for (size_t pos = 0; pos < in.l.size (); pos += 700)
        {
            for (uint32_t id = 0; id < kNumParams; ++id) // automate everything
                if (rnd () < 0.3)
                    e.setParam (id, paramTable ().toPlain (id, rnd ()));
            const int n = (int)std::min<size_t> (700, in.l.size () - pos);
            e.process (in.l.data () + pos, in.r.data () + pos, out.l.data () + pos, out.r.data () + pos, n);
        }
        bool finite = true;
        double pk = 0;
        for (float v : out.l)
        {
            finite &= std::isfinite (v);
            pk = std::max (pk, (double)std::fabs (v));
        }
        CHECK (finite && pk < 50.0, "iteration %d: finite %d peak %f", iter, finite, pk);
    }
}

TEST (performance)
{
    // CPU time (other programs running do not count), the best of three renders
    auto in = tones ({{55.0, -6.0}, {80.0, -20.0}, {1000.0, -20.0}}, 10.0);
    double secs = 1e9;
    for (int i = 0; i < 3; ++i)
    {
        auto e = engine ();
        e->setParam (kContrast, 0.8);
        e->setParam (kHighFreq, 1000.0);
        const std::clock_t t0 = std::clock ();
        run (*e, in);
        secs = std::min (secs, (double)(std::clock () - t0) / CLOCKS_PER_SEC);
    }
    std::printf ("    CPU: %.2f%% of one core (stereo)\n", 100.0 * secs / 10.0);
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
