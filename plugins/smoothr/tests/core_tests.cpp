// Headless tests for the Smoothr DSP. Run: ./smoothr_tests [filter]
#include "Character.h"
#include "Engine.h"
#include "Limiter.h"
#include "Params.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstdio>
#include <functional>
#include <memory>
#include <random>
#include <string>
#include <vector>

using namespace smoothr;

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
using Buf = std::vector<float>;

static double dbOf (double g) { return 20.0 * std::log10 (std::max (1e-12, g)); }
static double gainOf (double db) { return std::pow (10.0, db / 20.0); }

// An engine with `set` applied before it starts (so nothing glides). By default the limiter alone:
// the saturator off and Character at 0.
static std::unique_ptr<Engine> engine (const std::function<void (Engine&)>& set = {}, double sr = kSr, bool bare = true)
{
    auto e = std::make_unique<Engine> ();
    if (bare)
    {
        e->setParam (kTailBase + pk::kTailOn, 0.0);
        e->setParam (kCharacter, 0.0);
    }
    if (set)
        set (*e);
    e->prepare (sr, 512);
    return e;
}

// Runs a stereo signal through in blocks of `block`.
static void run (Engine& e, const Buf& inL, const Buf& inR, Buf& outL, Buf& outR, int block = 512)
{
    const size_t n = inL.size ();
    outL.assign (n, 0.0f);
    outR.assign (n, 0.0f);
    for (size_t pos = 0; pos < n; pos += (size_t)block)
    {
        const int m = (int)std::min ((size_t)block, n - pos);
        e.process (inL.data () + pos, inR.data () + pos, outL.data () + pos, outR.data () + pos, m);
    }
}

static double peakOf (const Buf& x, size_t from = 0)
{
    double p = 0.0;
    for (size_t i = from; i < x.size (); ++i)
        p = std::max (p, (double)std::fabs (x[i]));
    return p;
}

static double rmsOf (const Buf& x, size_t a, size_t b)
{
    double s = 0.0;
    for (size_t i = a; i < b; ++i)
        s += (double)x[i] * x[i];
    return std::sqrt (s / (double)std::max<size_t> (1, b - a));
}

// The true peak (16x, a long windowed sinc) of x from `from` on: every gap between two samples where
// either is over a quarter of `near` is checked at 15 points.
static double truePeak (const Buf& x, size_t from, double near)
{
    constexpr int kHalf = 48, kOver = 16;
    static std::vector<std::vector<double>> taps;
    if (taps.empty ())
    {
        taps.resize (kOver);
        for (int q = 1; q < kOver; ++q)
        {
            const double d = (double)q / kOver;
            double sum = 0.0;
            for (int t = -kHalf + 1; t <= kHalf; ++t)
            {
                const double u = t - d;
                const double w = 0.5 + 0.5 * std::cos (M_PI * u / (kHalf + 1));
                const double v = std::sin (M_PI * u) / (M_PI * u) * w;
                taps[(size_t)q].push_back (v);
                sum += v;
            }
            for (auto& v : taps[(size_t)q])
                v /= sum;
        }
    }
    double p = peakOf (x, from);
    for (size_t j = std::max<size_t> (from, kHalf); j + kHalf < x.size (); ++j)
    {
        if (std::fabs (x[j]) < 0.25 * near && std::fabs (x[j + 1]) < 0.25 * near)
            continue;
        for (int q = 1; q < kOver; ++q)
        {
            double s = 0.0;
            const auto& h = taps[(size_t)q];
            for (int t = -kHalf + 1, k = 0; t <= kHalf; ++t, ++k)
                s += h[(size_t)k] * x[j + (size_t)t];
            p = std::max (p, std::fabs (s));
        }
    }
    return p;
}

// |X(f)|, from `from` over `len` samples (a whole number of periods of f: no window)
static double bin (const Buf& x, size_t from, size_t len, double f, double sr = kSr)
{
    std::complex<double> acc (0.0, 0.0);
    const double w = -2.0 * M_PI * f / sr;
    for (size_t i = 0; i < len; ++i)
        acc += (double)x[from + i] * std::complex<double> (std::cos (w * (double)i), std::sin (w * (double)i));
    return 2.0 * std::abs (acc) / (double)len;
}

// A plain look-ahead limiter to compare with: one gain on the whole signal, from the sample peaks
// (both channels), a 1.5 ms look-ahead (minimum hold, then two moving averages, like Smoothr's
// gains), an exponential release in dB. Its gain is never above what a sample needs either.
struct PlainLimiter
{
    PlainLimiter (double sr, double ceilDb, double releaseMs, double lookMs = 1.5)
        : ceil (gainOf (ceilDb)), rel (std::exp (-1.0 / (releaseMs * 0.001 * sr))), w ((int)std::lround (lookMs * 0.001 * sr))
    {
    }
    void process (const Buf& inL, const Buf& inR, Buf& outL, Buf& outR)
    {
        const size_t n = inL.size ();
        std::vector<double> gr (n + (size_t)w, 0.0), held (n + (size_t)w, 0.0);
        for (size_t i = 0; i < n; ++i)
        {
            const double pk = std::max (std::fabs (inL[i]), std::fabs (inR[i]));
            gr[i] = pk > ceil ? dbOf (pk / ceil) : 0.0;
        }
        double env = 0.0;
        for (size_t i = 0; i < n + (size_t)w; ++i)
        {
            double m = 0.0;
            for (int k = 0; k < w && k <= (int)i; ++k)
                m = std::max (m, gr[i - (size_t)k]);
            env = std::max (m, env * rel);
            held[i] = gainOf (-env);
        }
        const int w1 = (w + 1) / 2, w2 = w + 1 - w1;
        std::vector<double> a1 (n + (size_t)w, 1.0), a2 (n + (size_t)w, 1.0);
        for (size_t i = 0; i < n + (size_t)w; ++i)
        {
            double s = 0.0;
            for (int k = 0; k < w1; ++k)
                s += (int)i - k >= 0 ? held[i - (size_t)k] : 1.0;
            a1[i] = s / w1;
        }
        for (size_t i = 0; i < n + (size_t)w; ++i)
        {
            double s = 0.0;
            for (int k = 0; k < w2; ++k)
                s += (int)i - k >= 0 ? a1[i - (size_t)k] : 1.0;
            a2[i] = s / w2;
        }
        // the gain for sample i is a2[i + w - 1]
        outL.assign (n, 0.0f);
        outR.assign (n, 0.0f);
        for (size_t i = 0; i < n; ++i)
        {
            const double g = a2[i + (size_t)w - 1];
            outL[i] = (float)(inL[i] * g);
            outR[i] = (float)(inR[i] * g);
        }
    }
    double ceil, rel;
    int w;
};

// ---------------------------------------------------------------------------------------------

TEST (param_table)
{
    CHECK (paramTable ().size () == kNumParams, "%u entries, %u ids", paramTable ().size (), (unsigned)kNumParams);
    bool ids = true;
    for (uint32_t i = 0; i < paramTable ().size (); ++i)
        ids &= paramTable ().info (i).id == i;
    CHECK (ids, "every entry sits at its id");
    CHECK (kTailExt2Base + pk::kTailExt2Fields == kNumParams, "the saturator's Gently Advanced block comes last");
    CHECK (paramTable ().info (kTailBase + pk::kTailOn).def == 1.0, "the saturator is on by default");
    CHECK (paramTable ().info (kTailBase + pk::kTailPreLimit).def == 0.0, "its pre-limiter is off");
    CHECK (paramTable ().info (kTailBase + pk::kTailMix).def == 0.5, "half wet");
    CHECK (paramTable ().info (kTailBase + pk::kTailDrive).def == 0.0, "Drive 0 dB");
    std::printf ("    %u parameters: own 0..%u, saturator %u..%u, extended %u..%u\n", (unsigned)kNumParams, (unsigned)kTailBase - 1,
                 (unsigned)kTailBase, (unsigned)kTailBase + pk::kTailFields - 1, (unsigned)kTailExtBase, (unsigned)kNumParams - 1);
}

TEST (split_response)
{
    Limiter lim;
    lim.prepare (kSr, 512);
    const double f[] = {30.0, 55.0, 80.0, 100.0, 150.0, 200.0, 300.0, 500.0, 1000.0};
    std::printf ("    lows' filter (the highs get 1 minus it):");
    for (double hz : f)
        std::printf (" %g Hz %.1f dB;", hz, dbOf (lim.lowResponse (hz)));
    std::printf ("\n");
    CHECK (std::fabs (dbOf (lim.lowResponse (200.0)) + 6.0) < 0.6, "about 6 dB down at 200 Hz: %.2f", dbOf (lim.lowResponse (200.0)));
    CHECK (1.0 - lim.lowResponse (55.0) < 0.01, "a 55 Hz bass is over 99 %% in the lows: %.4f", lim.lowResponse (55.0));
    CHECK (1.0 - lim.lowResponse (80.0) < 0.05, "80 Hz over 95 %%: %.4f", lim.lowResponse (80.0));
    CHECK (lim.lowResponse (400.0) < 0.01, "400 Hz is in the highs: %.4f", lim.lowResponse (400.0));
}

TEST (latency_constant_and_exact)
{
    // the same whatever the settings
    auto a = engine ({}, kSr, false);
    const int lat = a->latency ();
    bool same = true;
    for (int k = 0; k < 6; ++k)
    {
        auto e = engine ([k] (Engine& en) {
            en.setParam (kTailBase + pk::kTailOn, k % 2);
            en.setParam (kSmooth, 0.2 * k);
            en.setParam (kRelease, 5.0 + 150.0 * k);
            en.setParam (kAutoRelease, k % 3 == 0);
            en.setParam (kCharacter, 0.2 * k);
            en.setParam (kTailExtBase + pk::kTailExtHiQuality, k % 2 == 0);
        });
        same &= e->latency () == lat;
        // and a change while running leaves it where it is
        e->setParam (kSmooth, 1.0);
        e->setParam (kTailBase + pk::kTailOn, 1.0);
        same &= e->latency () == lat;
    }
    CHECK (same, "latency %d whatever the settings", lat);
    for (double sr : {44100.0, 96000.0})
    {
        auto e = engine ({}, sr, false);
        std::printf ("    latency at %.1f kHz: %d samples (%.2f ms)\n", sr / 1000.0, e->latency (), 1000.0 * e->latency () / sr);
    }
    const Limiter& lim = a->limiterStage ();
    std::printf ("    latency at 48 kHz: %d samples (%.2f ms): the limiter %d, the saturator %d\n", lat, 1000.0 * lat / kSr,
                 lim.latency (), lat - lim.latency ());

    // an impulse under the ceiling comes out exactly `latency` samples later
    for (int tailOn = 0; tailOn < 2; ++tailOn)
    {
        auto e = engine ([tailOn] (Engine& en) { en.setParam (kTailBase + pk::kTailOn, tailOn); });
        Buf in (4096, 0.0f), outL, outR;
        in[100] = 0.3f;
        run (*e, in, in, outL, outR);
        size_t at = 0;
        for (size_t i = 0; i < outL.size (); ++i)
            if (std::fabs (outL[i]) > std::fabs (outL[at]))
                at = i;
        CHECK ((int)at - 100 == e->latency (), "saturator %s: the impulse at %d, latency %d", tailOn ? "on" : "off", (int)at - 100, e->latency ());
    }
}

TEST (bypassed_is_the_latency_s_delay)
{
    // off (a Smemplr rack slot switched off): the input delayed by the latency, bit for bit, in any
    // block size
    auto e = engine ();
    const int lat = e->latency ();
    std::mt19937 rng (11);
    std::uniform_real_distribution<float> u (-1.0f, 1.0f);
    Buf l (20000), r (20000);
    for (size_t i = 0; i < l.size (); ++i)
    {
        l[i] = u (rng);
        r[i] = u (rng);
    }
    Buf ol = l, orr = r;
    for (size_t pos = 0, k = 0; pos < ol.size (); ++k)
    {
        const int n = (int)std::min<size_t> (1 + (k * 37) % 700, ol.size () - pos);
        e->processBypassed (ol.data () + pos, orr.data () + pos, n);
        pos += (size_t)n;
    }
    bool exact = true;
    for (size_t i = 0; i < ol.size (); ++i)
    {
        const float wl = i >= (size_t)lat ? l[i - (size_t)lat] : 0.0f, wr = i >= (size_t)lat ? r[i - (size_t)lat] : 0.0f;
        exact &= ol[i] == wl && orr[i] == wr;
    }
    CHECK (exact, "the input, %d samples later", lat);
}

TEST (idle_is_exact_delay)
{
    // the saturator off, Character 0, under the ceiling: the output is the input, delayed, bit for bit
    auto e = engine ();
    std::mt19937 rng (7);
    std::uniform_real_distribution<float> u (-0.5f, 0.5f);
    Buf inL (48000), inR (48000), outL, outR;
    for (size_t i = 0; i < inL.size (); ++i)
    {
        inL[i] = u (rng) * 0.8f;
        inR[i] = u (rng) * 0.8f;
    }
    run (*e, inL, inR, outL, outR, 333);
    const int lat = e->latency ();
    bool exact = true;
    for (size_t i = (size_t)lat; i < outL.size (); ++i)
        exact &= outL[i] == inL[i - (size_t)lat] && outR[i] == inR[i - (size_t)lat];
    CHECK (exact, "bit-exact delay of %d samples", lat);

    // after limiting it comes back to exact (the release snaps to 0 dB, the averages add up afresh)
    Buf loud (48000);
    for (size_t i = 0; i < loud.size (); ++i)
        loud[i] = (float)(2.5 * std::sin (2.0 * M_PI * 440.0 * (double)i / kSr));
    Buf o1, o2;
    run (*e, loud, loud, o1, o2);
    Buf quiet (9 * 48000, 0.0f);
    for (size_t i = 0; i < quiet.size (); ++i)
        quiet[i] = inL[i % inL.size ()];
    run (*e, quiet, quiet, outL, outR);
    bool back = true;
    for (size_t i = 8 * 48000; i < quiet.size (); ++i)
        back &= outL[i] == quiet[i - (size_t)lat];
    CHECK (back, "exact again once the release is over");
}

TEST (saturator_off_and_character_zero_bit_exact)
{
    // loud: the engine with the saturator off and Character 0 is its limiter, fed the input delayed
    // by the saturator's latency (the saturator off is a pure delay, Character 0 is nothing at all)
    auto e = engine ();
    Limiter lim;
    lim.setCeilingDb (paramTable ().info (kCeiling).def);
    lim.setRelease (paramTable ().info (kRelease).def, true);
    lim.setSmooth (paramTable ().info (kSmooth).def);
    lim.prepare (kSr, 256);
    const int tailLat = e->latency () - lim.latency ();
    std::mt19937 rng (3);
    std::normal_distribution<float> g (0.0f, 0.6f);
    Buf inL (96000), inR (96000), outL, outR;
    for (size_t i = 0; i < inL.size (); ++i)
    {
        const double bass = 1.2 * std::sin (2.0 * M_PI * 55.0 * (double)i / kSr);
        inL[i] = (float)(bass + g (rng));
        inR[i] = (float)(bass + g (rng));
    }
    run (*e, inL, inR, outL, outR, 256);
    Buf dl (inL.size (), 0.0f), dr (inR.size (), 0.0f);
    for (size_t i = (size_t)tailLat; i < inL.size (); ++i)
    {
        dl[i] = inL[i - (size_t)tailLat];
        dr[i] = inR[i - (size_t)tailLat];
    }
    for (size_t pos = 0; pos < dl.size (); pos += 256)
        lim.process (dl.data () + pos, dr.data () + pos, 256);
    bool exact = true;
    for (size_t i = 0; i < outL.size (); ++i)
        exact &= outL[i] == dl[i] && outR[i] == dr[i];
    CHECK (exact, "engine (saturator off, Character 0) == limiter on the input delayed %d samples", tailLat);
    CHECK (peakOf (outL) < 1.0, "and it was limiting (peak %.3f)", peakOf (outL));
}

TEST (character_dip)
{
    // at 0 nothing at all, bit for bit
    {
        CharacterDip d;
        d.prepare (kSr);
        d.setAmount (0.0);
        Buf l (4800), r (4800);
        for (size_t i = 0; i < l.size (); ++i)
            l[i] = r[i] = (float)(0.9 * std::sin (2.0 * M_PI * 150.0 * (double)i / kSr));
        const Buf l0 = l;
        d.process (l.data (), r.data (), (int)l.size ());
        CHECK (l == l0 && r == l0, "Character 0 leaves the signal untouched");
    }
    // the dip's shape while it is open: a loud 150 Hz tone opens it, a quiet probe measures it
    auto dipAt = [] (double amount, double probeHz, double openerDb, double openerHz = 151.0) {
        CharacterDip d;
        d.prepare (kSr);
        d.setAmount (amount);
        const size_t n = (size_t)(2.0 * kSr);
        Buf l (n), r (n);
        const double opener = gainOf (openerDb), probe = 0.001;
        for (size_t i = 0; i < n; ++i)
            l[i] = r[i] = (float)(opener * std::sin (2.0 * M_PI * openerHz * (double)i / kSr) +
                                  probe * std::sin (2.0 * M_PI * probeHz * (double)i / kSr));
        for (size_t pos = 0; pos < n; pos += 480)
            d.process (l.data () + pos, r.data () + pos, 480);
        return dbOf (bin (l, n / 2, n / 2, probeHz) / probe);
    };
    const double probes[] = {30.0, 40.0, 50.0, 60.0, 80.0, 100.0, 120.0, 160.0, 200.0, 250.0, 300.0, 400.0, 600.0, 1000.0, 3000.0};
    for (double amount : {0.3, 1.0})
    {
        double centre, width, range;
        CharacterDip::shape (amount, centre, width, range);
        std::printf ("    Character %.0f %% (centre %.0f Hz, %.2f oct, range %.1f dB), open (-6 dBFS at 151 Hz):\n     ", amount * 100.0,
                     centre, width, range);
        double deepest = 0.0, deepestHz = 0.0, outside = 0.0, lift = -100.0;
        for (double hz : probes)
        {
            const double db = dipAt (amount, hz, -6.0);
            std::printf (" %g:%.2f", hz, db);
            if (db < deepest)
            {
                deepest = db;
                deepestHz = hz;
            }
            if (hz < 50.0 || hz > 500.0)
                outside = std::min (outside, db);
            lift = std::max (lift, db);
        }
        std::printf ("\n");
        CHECK (deepestHz >= 80.0 && deepestHz <= 250.0, "the dip's deepest point is in 80 - 250 Hz: %g Hz", deepestHz);
        CHECK (deepest < -0.6 * range, "it is most of the range deep: %.2f of %.1f dB", deepest, range);
        CHECK (outside > 0.25 * deepest, "under 50 Hz and over 500 Hz it is under a quarter as deep: %.2f", outside);
        CHECK (lift < 0.05, "it only cuts (no bump either side): %+.2f dB", lift);
    }
    // only on loud low-mid content: a quiet low mid, a loud bass or a loud upper mid hardly open it
    {
        const double quiet = dipAt (1.0, 160.0, -40.0);
        const double bass = dipAt (1.0, 160.0, -6.0, 45.0);
        const double mids = dipAt (1.0, 160.0, -6.0, 1200.0);
        std::printf ("    at 160 Hz with a quiet low mid (-40 dBFS): %.2f dB; with a loud 45 Hz bass: %.2f dB; with a loud 1.2 kHz: %.2f dB\n",
                     quiet, bass, mids);
        CHECK (quiet > -0.1 && bass > -1.0 && mids > -0.3, "quiet %.2f, bass %.2f, mids %.2f", quiet, bass, mids);
    }
    // turned back to 0 it fades out and passes bit for bit again
    {
        CharacterDip d;
        d.prepare (kSr);
        d.setAmount (1.0);
        Buf l (48000), r (48000);
        auto fill = [&] (size_t off) {
            for (size_t i = 0; i < l.size (); ++i)
                l[i] = r[i] = (float)(0.7 * std::sin (2.0 * M_PI * 150.0 * (double)(i + off) / kSr));
        };
        fill (0);
        d.process (l.data (), r.data (), (int)l.size ());
        const bool wasOn = d.cutDb () < -3.0f;
        d.setAmount (0.0);
        fill (48000);
        for (size_t pos = 0; pos < l.size (); pos += 480)
            d.process (l.data () + pos, r.data () + pos, 480);
        fill (96000);
        const Buf l0 = l;
        d.process (l.data (), r.data (), (int)l.size ());
        CHECK (wasOn && !d.isRunning () && l == l0, "back to 0: off and exact (it had cut %s)", wasOn ? "yes" : "no");
    }
}

TEST (ceiling_never_exceeded)
{
    std::mt19937 rng (11);
    std::normal_distribution<float> g (0.0f, 1.0f);
    struct Material
    {
        const char* name;
        std::function<float (size_t, int)> at;
    };
    const double sr = kSr;
    // noise band-limited to 18 kHz (what music has up there), from a long windowed-sinc low-pass
    Buf pinkish[2];
    {
        const int half = 64;
        std::vector<double> h;
        for (int t = -half; t <= half; ++t)
        {
            const double fc = 18000.0 / sr, x = 2.0 * fc * t;
            const double sinc = t == 0 ? 2.0 * fc : std::sin (M_PI * x) / (M_PI * t);
            h.push_back (sinc * (0.42 + 0.5 * std::cos (M_PI * t / half) + 0.08 * std::cos (2.0 * M_PI * t / half)));
        }
        for (auto& b : pinkish)
        {
            Buf w ((size_t)(3.0 * sr) + h.size ());
            for (auto& v : w)
                v = g (rng) * 1.3f;
            b.assign ((size_t)(3.0 * sr), 0.0f);
            for (size_t i = 0; i < b.size (); ++i)
            {
                double acc = 0.0;
                for (size_t k = 0; k < h.size (); ++k)
                    acc += h[k] * w[i + k];
                b[i] = (float)acc;
            }
        }
    }
    std::vector<Material> mats = {
        {"white noise (+12 dB)", [&] (size_t, int) { return g (rng) * 1.2f; }},
        {"noise under 18 kHz (+12 dB)", [&] (size_t i, int c) { return pinkish[c][i]; }},
        {"clicks (+18 dB)", [&] (size_t i, int) {
             const size_t k = i % 12000;
             return k < 96 ? (float)(7.0 * std::sin (M_PI * k / 96.0) * std::sin (2.0 * M_PI * 4000.0 * i / sr)) : 0.0f;
         }},
        {"55 Hz bass + kicks (+12 dB)", [&] (size_t i, int) {
             const double t = (double)(i % 24000) / sr;
             const double kick = 2.5 * std::exp (-t * 30.0) * std::sin (2.0 * M_PI * (50.0 * t + 90.0 * (1.0 - std::exp (-t * 40.0)) / 40.0));
             return (float)(1.3 * std::sin (2.0 * M_PI * 55.0 * i / sr) + kick);
         }},
        {"near-Nyquist tones (+6 dB)", [&] (size_t i, int c) {
             return (float)(1.0 * std::sin (2.0 * M_PI * 11025.0 * i / sr + 0.7 * c) + 0.9 * std::sin (2.0 * M_PI * 16000.0 * i / sr));
         }},
        {"square 100 Hz (+10 dB)", [&] (size_t i, int) { return (i / 240) % 2 ? 2.8f : -2.8f; }},
    };
    for (double ceilDb : {-1.0, -0.1, -6.0})
        for (const auto& m : mats)
        {
            auto e = engine ([ceilDb] (Engine& en) { en.setParam (kCeiling, ceilDb); }, sr, false);
            const size_t n = (size_t)(3.0 * sr);
            Buf l (n), r (n), ol, orr;
            for (size_t i = 0; i < n; ++i)
            {
                l[i] = m.at (i, 0);
                r[i] = m.at (i, 1);
            }
            run (*e, l, r, ol, orr);
            const double c = gainOf (ceilDb);
            const double sp = std::max (peakOf (ol), peakOf (orr));
            const double tp = std::max (truePeak (ol, 0, c), truePeak (orr, 0, c));
            const auto& lim = e->limiterStage ();
            std::printf ("    ceiling %5.1f dB, %-28s sample peak %+.4f dB, true peak %+.3f dB over; clamps %lld, full-band %lld\n", ceilDb, m.name,
                         dbOf (sp) - ceilDb, dbOf (tp) - ceilDb, (long long)lim.clamps (), (long long)lim.fullBandSamples ());
            CHECK (sp <= c, "sample peak %.6f over the ceiling %.6f", sp, c);
            // (white noise has as much up at Nyquist as anywhere, where no short interpolation is exact)
            if (std::string (m.name).find ("white") == std::string::npos)
                CHECK (dbOf (tp) - ceilDb < 0.02, "true peak %.3f dB over", dbOf (tp) - ceilDb);
        }
    // other rates
    for (double rate : {44100.0, 96000.0})
    {
        auto e = engine ({}, rate, false);
        const size_t n = (size_t)(2.0 * rate);
        Buf l (n), r (n), ol, orr;
        for (size_t i = 0; i < n; ++i)
            l[i] = r[i] = (float)(1.5 * std::sin (2.0 * M_PI * 50.0 * i / rate) + 1.2 * std::sin (2.0 * M_PI * 3100.0 * i / rate) *
                                  std::sin (2.0 * M_PI * 3.0 * i / rate) + 0.9 * std::sin (2.0 * M_PI * 13000.0 * i / rate));
        run (*e, l, r, ol, orr);
        const double c = gainOf (-1.0);
        const double sp = peakOf (ol), tp = truePeak (ol, 0, c);
        std::printf ("    %.1f kHz, bass + tones: sample peak %+.4f dB, true peak %+.3f dB over\n", rate / 1000.0, dbOf (sp) + 1.0, dbOf (tp) + 1.0);
        CHECK (sp <= c && dbOf (tp) + 1.0 < 0.02, "%.1f kHz", rate / 1000.0);
    }
}

// The THD of a held bass note pushed 10 dB over the ceiling, Smoothr against a plain fast limiter
// given the same signal (both come out as loud: a held sine limited to the ceiling).
TEST (bass_distortion_held_note)
{
    const double ceilDb = -1.0, over = 10.0;
    for (double hz : {40.0, 55.0, 80.0})
    {
        const size_t n = (size_t)(4.0 * kSr), from = (size_t)(2.0 * kSr), len = (size_t)kSr; // whole periods
        Buf x (n);
        for (size_t i = 0; i < n; ++i)
            x[i] = (float)(gainOf (ceilDb + over) * std::sin (2.0 * M_PI * hz * (double)i / kSr));
        auto thd = [&] (const Buf& y) {
            const double f1 = bin (y, from, len, hz);
            double h = 0.0;
            for (int k = 2; k <= 12; ++k)
                h += std::pow (bin (y, from, len, hz * k), 2.0);
            return 100.0 * std::sqrt (h) / f1;
        };
        const double release = paramTable ().info (kRelease).def;
        PlainLimiter pl (kSr, ceilDb, release);
        Buf ol, orr;
        pl.process (x, x, ol, orr);
        const double tp = thd (ol), rp = rmsOf (ol, from, from + len);
        for (double smooth : {0.0, 0.25, 0.5, 1.0})
        {
            auto e = engine ([&] (Engine& en) {
                en.setParam (kCeiling, ceilDb);
                en.setParam (kSmooth, smooth);
            });
            Buf yl, yr;
            run (*e, x, x, yl, yr);
            const double t = thd (yl), rms = rmsOf (yl, from, from + len);
            std::printf ("    %2.0f Hz +%.0f dB over, Smooth %3.0f %%: THD Smoothr %.4f %% (%.2f dBFS rms), plain limiter (%.0f ms release) %.3f %% (%.2f dBFS rms)\n",
                         hz, over, smooth * 100.0, t, dbOf (rms), release, tp, dbOf (rp));
            CHECK (t < 0.1 * tp, "Smoothr %.4f %% vs plain %.3f %%", t, tp);
            CHECK (rms > rp * gainOf (-0.5), "as loud: %.2f vs %.2f dBFS", dbOf (rms), dbOf (rp));
        }
    }
}

// A loud bass with kick clicks on it, pushed 10+ dB into the limiter: how much the clicks modulate the
// bass (the sidebands they put around it), Smoothr against a plain limiter turned to the same loudness.
TEST (bass_intermodulation_with_kicks)
{
    const double ceilDb = -1.0;
    const size_t period = 24000; // a click every 0.5 s; the bass (56 Hz) fits it 28 times
    const size_t n = 10 * period, from = 4 * period, len = 4 * period;
    auto signal = [&] (double bassOverDb, double gainDb) {
        Buf x (n);
        const double c = gainOf (ceilDb), g = gainOf (gainDb);
        const double bass = c * gainOf (bassOverDb), click = c * gainOf (12.0) - bass;
        const int clickLen = 144; // 3 ms
        for (size_t i = 0; i < n; ++i)
        {
            const size_t k = i % period;
            double v = bass * std::sin (2.0 * M_PI * 56.0 * (double)i / kSr);
            if (k < (size_t)clickLen)
                v += click * 0.5 * (1.0 - std::cos (2.0 * M_PI * (double)k / clickLen)) * std::sin (2.0 * M_PI * 3000.0 * (double)k / kSr);
            x[i] = (float)(v * g);
        }
        return x;
    };
    // the sidebands: everything from 20 to 400 Hz but the bass, against the bass (dB)
    auto sidebands = [&] (const Buf& y) {
        const double b = bin (y, from, len, 56.0);
        double s = 0.0;
        for (double f = 20.0; f <= 400.0; f += 2.0)
            if (f != 56.0)
                s += std::pow (bin (y, from, len, f), 2.0);
        return dbOf (std::sqrt (s) / b);
    };
    const double release = paramTable ().info (kRelease).def;
    auto plain = [&] (double bassOver, double gainDb, Buf& ol) {
        PlainLimiter pl (kSr, ceilDb, release);
        Buf orr;
        pl.process (signal (bassOver, gainDb), signal (bassOver, gainDb), ol, orr);
        return rmsOf (ol, from, from + len);
    };
    for (double bassOver : {2.0, -3.0})
    {
        const Buf x = signal (bassOver, 0.0);
        Buf ol;
        const double sameIn = sidebands ((plain (bassOver, 0.0, ol), ol)), rpSame = rmsOf (ol, from, from + len);
        for (double smooth : {0.0, 0.25, 0.5, 1.0})
        {
            auto e = engine ([&] (Engine& en) {
                en.setParam (kCeiling, ceilDb);
                en.setParam (kSmooth, smooth);
            });
            Buf yl, yr;
            run (*e, x, x, yl, yr);
            const double sb = sidebands (yl), rms = rmsOf (yl, from, from + len);
            // the plain limiter turned down (or up) until it is as loud
            double lo = -12.0, hi = 6.0;
            for (int it = 0; it < 30; ++it)
            {
                const double mid = 0.5 * (lo + hi);
                (plain (bassOver, mid, ol) < rms ? lo : hi) = mid;
            }
            const double inDb = 0.5 * (lo + hi);
            const double rp = plain (bassOver, inDb, ol), sp = sidebands (ol);
            std::printf ("    bass %+.0f dB over, clicks to +12 dB, Smooth %3.0f %%: sidebands around the bass: Smoothr %.1f dB (%.2f dBFS rms); "
                         "plain limiter as loud (%+.1f dB in) %.1f dB (%.2f dBFS rms); plain with the same input %.1f dB (%.2f dBFS rms)\n",
                         bassOver, smooth * 100.0, sb, dbOf (rms), inDb, sp, dbOf (rp), sameIn, dbOf (rpSame));
            if (smooth >= 0.25)
                CHECK (sb < sp - 6.0 && sb < sameIn - 6.0 && std::fabs (dbOf (rp / rms)) < 0.1, "Smoothr %.1f dB vs plain %.1f dB", sb, sp);
            if (smooth >= 0.5)
                CHECK (sb < sp - 10.0, "clearly: Smoothr %.1f dB vs plain %.1f dB", sb, sp);
        }
    }
}

TEST (smooth_moves_the_reduction_to_the_highs)
{
    // clicks over a bass under the ceiling: with Smooth up the lows are pulled down less
    for (double smooth : {0.0, 0.25, 0.5, 1.0})
    {
        auto e = engine ([smooth] (Engine& en) { en.setParam (kSmooth, smooth); });
        const size_t n = (size_t)(3.0 * kSr);
        Buf x (n), yl, yr;
        for (size_t i = 0; i < n; ++i)
        {
            const size_t k = i % 12000;
            x[i] = (float)(0.6 * std::sin (2.0 * M_PI * 50.0 * i / kSr) +
                           (k < 144 ? 2.0 * std::sin (M_PI * k / 144.0) * std::sin (2.0 * M_PI * 3000.0 * i / kSr) : 0.0));
        }
        Meters m;
        e->setMeters (&m);
        run (*e, x, x, yl, yr);
        float lo[400], hi[400];
        const int got = m.gr.read (lo, hi, 400);
        double sumLo = 0.0, sumHi = 0.0;
        for (int i = 0; i < got; ++i)
        {
            sumLo += lo[i];
            sumHi += hi[i];
        }
        std::printf ("    Smooth %3.0f %%: mean reduction, lows %.2f dB, highs %.2f dB\n", smooth * 100.0, sumLo / got, sumHi / got);
        CHECK (got > 100, "history");
    }
}

// A dense, loud little mix (bass, kicks with a body and a click, a snare, hats, a chord) pushed 9 dB in:
// how the reduction is shared between the lows and the highs.
static double denseMix (size_t t, int ch)
{
    const double s = (double)t / kSr;
    const double beat = std::fmod (s, 0.5), off = std::fmod (s + 0.25, 0.5);
    double v = 0.35 * std::sin (2 * M_PI * 55.0 * s);
    v += 0.6 * std::exp (-beat * 25.0) * std::sin (2 * M_PI * (45.0 * beat + 100.0 * (1.0 - std::exp (-beat * 30.0)) / 30.0));
    if (beat < 0.004)
        v += 0.4 * std::sin (M_PI * beat / 0.004) * std::sin (2 * M_PI * 3500.0 * s);
    uint32_t h = (uint32_t)(t * 2654435761u) ^ (uint32_t)(ch * 0x9E3779B9u);
    h ^= h >> 13;
    h *= 0x5bd1e995u;
    h ^= h >> 15;
    const double noise = (double)(h & 0xFFFF) / 32768.0 - 1.0;
    v += 0.3 * std::exp (-off * 30.0) * noise;
    v += 0.05 * noise * std::exp (-std::fmod (s, 0.125) * 60.0);
    for (double f : {196.0, 247.0, 294.0})
        v += 0.06 * std::sin (2 * M_PI * f * s + ch);
    return v;
}

TEST (dense_mix_balance)
{
    const size_t n = (size_t)(8.0 * kSr);
    Buf l (n), r (n), ol, orr;
    for (size_t i = 0; i < n; ++i)
    {
        l[i] = (float)(gainOf (9.0) * denseMix (i, 0));
        r[i] = (float)(gainOf (9.0) * denseMix (i, 1));
    }
    PlainLimiter pl (kSr, -1.0, paramTable ().info (kRelease).def);
    Buf pl0, pl1;
    pl.process (l, r, pl0, pl1);
    for (double smooth : {0.0, 0.25, 0.5, 1.0})
    {
        auto e = engine ([smooth] (Engine& en) { en.setParam (kSmooth, smooth); });
        Meters m;
        e->setMeters (&m);
        run (*e, l, r, ol, orr);
        float lo[800], hi[800];
        const int got = m.gr.read (lo, hi, 800); // the last 5 s
        double sumLo = 0.0, sumHi = 0.0;
        for (int i = 0; i < got; ++i)
        {
            sumLo += lo[i];
            sumHi += hi[i];
        }
        std::printf ("    Smooth %3.0f %%: mean reduction, lows %.2f dB, highs %.2f dB; %.2f dBFS rms (the plain limiter %.2f dBFS rms)\n",
                     smooth * 100.0, sumLo / got, sumHi / got, dbOf (rmsOf (ol, n / 2, n)), dbOf (rmsOf (pl0, n / 2, n)));
        CHECK (peakOf (ol) <= gainOf (-1.0), "under the ceiling");
        if (smooth == 0.5)
            CHECK (sumLo > 0.6 * sumHi, "the balance holds at the default: lows %.2f dB, highs %.2f dB", sumLo / got, sumHi / got);
        CHECK (e->limiterStage ().fullBandSamples () == 0, "the lows never needed the fast gain: %lld samples",
               (long long)e->limiterStage ().fullBandSamples ());
    }
    // and with the whole chain at its defaults (the saturator in, Character 30 %), the saturator driven 3 dB
    {
        auto e = engine ([] (Engine& en) { en.setParam (kTailBase + pk::kTailDrive, 3.0); }, kSr, false);
        Meters m;
        e->setMeters (&m);
        run (*e, l, r, ol, orr);
        float lo[800], hi[800];
        const int got = m.gr.read (lo, hi, 800);
        double sumLo = 0.0, sumHi = 0.0, maxLo = 0.0;
        for (int i = 0; i < got; ++i)
        {
            sumLo += lo[i];
            sumHi += hi[i];
            maxLo = std::max (maxLo, (double)lo[i]);
        }
        std::printf ("    the whole chain, saturator +3 dB: mean reduction, lows %.2f dB (most %.2f), highs %.2f dB; %.2f dBFS rms, "
                     "peak %.3f dBFS; fast full-band samples %lld\n",
                     sumLo / got, maxLo, sumHi / got, dbOf (rmsOf (ol, n / 2, n)), dbOf (std::max (peakOf (ol), peakOf (orr))),
                     (long long)e->limiterStage ().fullBandSamples ());
        CHECK (std::max (peakOf (ol), peakOf (orr)) <= gainOf (-1.0), "under the ceiling");
    }
}

TEST (fuzz_finite)
{
    std::mt19937 rng (5);
    std::uniform_real_distribution<double> u (0.0, 1.0);
    auto e = engine ({}, kSr, false);
    Buf l (1024), r (1024);
    bool finite = true, bounded = true;
    for (int b = 0; b < 3000; ++b)
    {
        if (b % 16 == 0)
            for (int k = 0; k < 4; ++k)
            {
                const uint32_t id = (uint32_t)(u (rng) * (double)kNumParams) % kNumParams;
                const auto& info = paramTable ().info (id);
                e->setParam (id, info.min + u (rng) * (info.max - info.min));
            }
        const int n = 1 + (int)(u (rng) * 1023);
        const double level = b % 50 == 0 ? 1e4 : (u (rng) < 0.1 ? 1e-30 : 4.0);
        for (int i = 0; i < n; ++i)
        {
            l[(size_t)i] = (float)((u (rng) * 2.0 - 1.0) * level);
            r[(size_t)i] = (float)((u (rng) * 2.0 - 1.0) * level);
        }
        if (b == 1234)
            l[7] = std::nanf ("");
        e->process (l.data (), r.data (), l.data (), r.data (), n);
        for (int i = 0; i < n; ++i)
        {
            finite &= std::isfinite (l[(size_t)i]) && std::isfinite (r[(size_t)i]);
            bounded &= std::fabs (l[(size_t)i]) <= 1.0f && std::fabs (r[(size_t)i]) <= 1.0f;
        }
    }
    CHECK (finite, "finite");
    CHECK (bounded, "never over 0 dBFS (the highest ceiling)");
}

TEST (cpu)
{
    std::mt19937 rng (9);
    std::uniform_real_distribution<float> u (-1.0f, 1.0f);
    auto time = [&] (Engine& e, const char* what) {
        Buf l (512), r (512);
        const int blocks = (int)(10.0 * kSr / 512);
        const auto t0 = std::chrono::steady_clock::now ();
        for (int b = 0; b < blocks; ++b)
        {
            for (int i = 0; i < 512; ++i)
            {
                l[(size_t)i] = u (rng) * 0.5f + 0.8f * (float)std::sin (2.0 * M_PI * 55.0 * (b * 512 + i) / kSr);
                r[(size_t)i] = u (rng) * 0.5f + 0.8f * (float)std::sin (2.0 * M_PI * 55.0 * (b * 512 + i) / kSr);
            }
            e.process (l.data (), r.data (), l.data (), r.data (), 512);
        }
        const double secs = std::chrono::duration<double> (std::chrono::steady_clock::now () - t0).count ();
        std::printf ("    %s: %.2f%% of real time (stereo, 48 kHz)\n", what, 100.0 * secs / 10.0);
    };
    auto d = engine ({}, kSr, false);
    time (*d, "defaults (saturator on, Hi-Q, Character 30 %), limiting ~8 dB");
    auto b = engine ();
    time (*b, "the limiter alone");
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
