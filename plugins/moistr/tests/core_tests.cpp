// Headless tests for the Moistr DSP. Run: ./moistr_tests [filter]
// The band filters and Gap, the movement (still at 0, High moving the most, Seed, the host's transport),
// the Glue compressor, Grit, Mix, Passes, silence and resonance, and the CPU budget.
#include "Dsp.h"
#include "Engine.h"
#include "Movement.h"
#include "Params.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <functional>
#include <limits>
#include <memory>
#include <string>
#include <vector>

using namespace moistr;

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
#define TEST(name)                       \
    static void name ();                 \
    static Reg reg_##name (#name, name); \
    static void name ()

namespace {

constexpr double kSr = 48000.0;
constexpr double kPi = 3.14159265358979323846;

double rms (const std::vector<float>& x, size_t a, size_t b)
{
    b = std::min (b, x.size ());
    double s = 0.0;
    for (size_t i = a; i < b; ++i)
        s += (double)x[i] * x[i];
    return b > a ? std::sqrt (s / (double)(b - a)) : 0.0;
}
double peak (const std::vector<float>& x, size_t a, size_t b)
{
    double p = 0.0;
    for (size_t i = a; i < std::min (b, x.size ()); ++i)
        p = std::max (p, (double)std::fabs (x[i]));
    return p;
}
double db (double v) { return 20.0 * std::log10 (std::max (v, 1e-20)); }
bool finite (const std::vector<float>& x)
{
    for (float v : x)
        if (!std::isfinite (v))
            return false;
    return true;
}
// the amplitude of the component at `hz` in x[a, b) (a single DFT bin, Hann window)
double toneAt (const std::vector<float>& x, double hz, size_t a, size_t b)
{
    double s = 0, c = 0, wsum = 0;
    for (size_t i = a; i < b; ++i)
    {
        const double w = 0.5 - 0.5 * std::cos (2.0 * kPi * (double)(i - a) / (double)(b - a));
        s += w * x[i] * std::sin (2.0 * kPi * hz * (double)i / kSr);
        c += w * x[i] * std::cos (2.0 * kPi * hz * (double)i / kSr);
        wsum += w;
    }
    return 2.0 * std::sqrt (s * s + c * c) / wsum;
}
std::vector<float> sine (double hz, double amp, double seconds)
{
    std::vector<float> x ((size_t)(seconds * kSr));
    for (size_t i = 0; i < x.size (); ++i)
        x[i] = (float)(amp * std::sin (2.0 * kPi * hz * (double)i / kSr));
    return x;
}
std::vector<float> noise (double amp, double seconds, uint32_t seed = 3)
{
    std::vector<float> x ((size_t)(seconds * kSr));
    for (auto& v : x)
    {
        seed = seed * 1664525u + 1013904223u;
        v = (float)((int32_t)seed / 2147483648.0 * amp);
    }
    return x;
}
// a detuned saw pair (a plain Reese), mono
std::vector<float> reese (double seconds)
{
    std::vector<float> x ((size_t)(seconds * kSr));
    double p1 = 0.0, p2 = 0.3;
    for (auto& v : x)
    {
        p1 += 55.0 / kSr;
        p2 += 55.0 * 1.012 / kSr;
        p1 -= std::floor (p1);
        p2 -= std::floor (p2);
        v = (float)(0.25 * ((2.0 * p1 - 1.0) + (2.0 * p2 - 1.0)));
    }
    return x;
}

std::unique_ptr<Engine> engine (int block = 512)
{
    auto e = std::make_unique<Engine> ();
    e->prepare (kSr, block);
    return e;
}

// the bands alone: no Drive, Glue, Grit or movement
void plain (Engine& e)
{
    e.setParam (kDrive, 0.0);
    e.setParam (kGlue, 0.0);
    e.setParam (kGrit, 0.0);
    e.setParam (kMovement, 0.0);
    e.reset ();
}
// only one band sounding
void solo (Engine& e, int band)
{
    const uint32_t lv[kBands] = {kLowLevel, kMidLevel, kHighLevel};
    for (int b = 0; b < kBands; ++b)
        e.setParam (lv[b], b == band ? 0.0 : kLevelOffDb);
    e.reset ();
}

// renders x (both channels) in blocks, calling `at (sample)` before each
std::vector<float> run (Engine& e, const std::vector<float>& x, std::vector<float>* right = nullptr, int block = 256,
                        const std::function<void (size_t)>& at = {})
{
    std::vector<float> l (x.size ()), r (x.size ());
    for (size_t a = 0; a < x.size (); a += (size_t)block)
    {
        if (at)
            at (a);
        const int m = (int)std::min ((size_t)block, x.size () - a);
        e.process (x.data () + a, x.data () + a, l.data () + a, r.data () + a, m);
    }
    if (right)
        *right = r;
    return l;
}

// the gain (dB) of a band alone at hz: a sine at -12 dBFS through it
double bandGainDb (int band, double hz, const std::function<void (Engine&)>& setup = {})
{
    auto e = engine ();
    plain (*e);
    solo (*e, band);
    if (setup)
        setup (*e);
    e->reset ();
    const auto x = sine (hz, 0.25, 0.4);
    const auto y = run (*e, x);
    const size_t a = (size_t)(0.2 * kSr), b = x.size ();
    return db (toneAt (y, hz, a, b) / toneAt (x, hz, a, b));
}

// the frequency (Hz) where a band's response crosses -3 dB, searched from `from` towards `to`
double edgeHz (int band, double from, double to, const std::function<void (Engine&)>& setup)
{
    const int steps = 48;
    double last = from;
    for (int i = 0; i <= steps; ++i)
    {
        const double f = from * std::pow (to / from, (double)i / steps);
        if ((band == 1 && bandGainDb (band, f, setup) < -3.0) || (band == 2 && bandGainDb (band, f, setup) < -3.0))
            return std::sqrt (f * last);
        last = f;
    }
    return to;
}

} // namespace

TEST (band_filters)
{
    // Low: a low-pass at 180 Hz (flat below, Q 0.8 at its frequency, steep above)
    const double q = dsp::qOf (0.15);
    double lowPass = bandGainDb (0, 40.0), lowAt = bandGainDb (0, 180.0), lowAbove = bandGainDb (0, 1440.0);
    std::printf ("    Low: %.2f dB at 40 Hz, %.2f at 180 (Q %.2f: %.2f), %.1f at 1440\n", lowPass, lowAt, q, db (q), lowAbove);
    CHECK (std::fabs (lowPass) < 0.5, "Low passes 40 Hz: %.2f dB", lowPass);
    CHECK (std::fabs (lowAt - db (q)) < 0.5, "Low at its frequency: %.2f dB (Q says %.2f)", lowAt, db (q));
    CHECK (lowAbove < -30.0, "Low cuts 3 octaves up: %.1f dB", lowAbove);
    // Mid: a band-pass at 450 Hz, 0 dB at its centre
    const double midAt = bandGainDb (1, 450.0), midBelow = bandGainDb (1, 450.0 / 8.0), midAbove = bandGainDb (1, 450.0 * 8.0);
    std::printf ("    Mid: %.2f dB at 450 Hz, %.1f at 56, %.1f at 3600\n", midAt, midBelow, midAbove);
    CHECK (std::fabs (midAt) < 0.5, "Mid passes its centre: %.2f dB", midAt);
    CHECK (midBelow < -15.0 && midAbove < -15.0, "Mid cuts 3 octaves either side: %.1f / %.1f dB", midBelow, midAbove);
    // High: a high-pass at 3 kHz
    const double highPass = bandGainDb (2, 15000.0), highAt = bandGainDb (2, 3000.0), highBelow = bandGainDb (2, 375.0);
    std::printf ("    High: %.2f dB at 15 kHz, %.2f at 3 kHz, %.1f at 375 Hz\n", highPass, highAt, highBelow);
    CHECK (std::fabs (highPass) < 0.7, "High passes 15 kHz: %.2f dB", highPass);
    CHECK (std::fabs (highAt - db (q)) < 0.5, "High at its frequency: %.2f dB", highAt);
    CHECK (highBelow < -30.0, "High cuts 3 octaves down: %.1f dB", highBelow);
    // 24 dB: steeper
    auto steep = [] (Engine& e) { e.setParam (kSlope, kSlope24); };
    const double low24 = bandGainDb (0, 720.0, steep), low12 = bandGainDb (0, 720.0);
    const double high24 = bandGainDb (2, 750.0, steep), high12 = bandGainDb (2, 750.0);
    std::printf ("    two octaves out: Low %.1f (12 dB) / %.1f dB (24 dB), High %.1f / %.1f dB\n", low12, low24, high12, high24);
    CHECK (low24 < low12 - 10.0 && high24 < high12 - 10.0, "24 dB cuts more");
    // a band's Level moves it
    const double lifted = bandGainDb (1, 450.0, [] (Engine& e) { e.setParam (kMidLevel, 6.0); });
    CHECK (std::fabs (lifted - 6.0) < 0.5, "Mid Level +6 dB: %.2f dB", lifted);
}

TEST (gap_widens)
{
    // the hollow between Mid's upper -3 dB point and High's lower one, in octaves, at Gap -50, 0, +50 %
    double width[3];
    const double gaps[3] = {-0.5, 0.0, 0.5};
    for (int i = 0; i < 3; ++i)
    {
        auto setup = [g = gaps[i]] (Engine& e) { e.setParam (kGap, g); };
        const double midTop = edgeHz (1, Engine::setFrequency (defaultParams (), 1) * std::exp2 (-gaps[i]), 20000.0, setup);
        const double highBottom = edgeHz (2, 20000.0, 50.0, setup);
        width[i] = std::log2 (highBottom / midTop);
        // and the middle of the hollow drops further as it widens
        std::printf ("    Gap %+.0f %%: Mid -3 dB at %.0f Hz, High -3 dB at %.0f Hz: %.2f octaves\n", gaps[i] * 100.0, midTop,
                     highBottom, width[i]);
    }
    CHECK (width[2] > width[1] + 0.8 && width[1] > width[0] + 0.8, "Gap widens the hollow: %.2f, %.2f, %.2f octaves", width[0],
           width[1], width[2]);
    CHECK (width[1] > 1.5, "the defaults leave a clear gap between Mid and High: %.2f octaves", width[1]);
    // the set frequencies: Mid down and High up by the same ratio
    ParamArray p = defaultParams ();
    p[kGap] = 1.0;
    CHECK (std::fabs (Engine::setFrequency (p, 1) - 225.0) < 1e-9 && std::fabs (Engine::setFrequency (p, 2) - 6000.0) < 1e-9 &&
               Engine::setFrequency (p, 0) == 180.0,
           "Gap 100 %%: Mid 225 Hz, High 6 kHz, Low unmoved");
}

TEST (movement_zero_is_still)
{
    auto e = engine ();
    e->setParam (kMovement, 0.0);
    e->reset ();
    const auto x = reese (3.0);
    bool still = true, atSet = true;
    const ParamArray p = defaultParams ();
    double first[kBands];
    for (int b = 0; b < kBands; ++b)
        first[b] = e->bandFreq (0, b);
    run (*e, x, nullptr, 256, [&] (size_t) {
        for (int b = 0; b < kBands; ++b)
        {
            still = still && e->bandFreq (0, b) == first[b] && e->bandLevelDb (0, b) == 0.0;
            atSet = atSet && std::fabs (e->bandFreq (0, b) / Engine::setFrequency (p, b) - 1.0) < 1e-12;
        }
    });
    CHECK (still, "Movement 0: every band's frequency and level stay exactly as they are the whole time");
    CHECK (atSet, "at their set frequencies");
}

TEST (high_moves_most)
{
    // the default Moves (Low 20 %, Mid 50 %, High 100 %) at Movement 100 %: the spread of each band's
    // frequency (octaves) and level (dB) over a minute, for several seeds
    for (int seed : {1, 2, 3, 17, 64, 128})
    {
        auto e = engine ();
        e->setParam (kMovement, 1.0);
        e->setParam (kSeed, seed);
        e->reset ();
        double sf[kBands] {}, sf2[kBands] {}, sl[kBands] {}, sl2[kBands] {};
        int count = 0;
        const ParamArray p = defaultParams ();
        const std::vector<float> silence (512, 0.0f);
        std::vector<float> l (512), r (512);
        for (int blk = 0; blk < (int)(60.0 * kSr / 512); ++blk)
        {
            e->process (silence.data (), silence.data (), l.data (), r.data (), 512);
            for (int b = 0; b < kBands; ++b)
            {
                const double o = std::log2 (e->bandFreq (0, b) / Engine::setFrequency (p, b)), d = e->bandLevelDb (0, b);
                sf[b] += o;
                sf2[b] += o * o;
                sl[b] += d;
                sl2[b] += d * d;
            }
            ++count;
        }
        double fsd[kBands], lsd[kBands];
        for (int b = 0; b < kBands; ++b)
        {
            fsd[b] = std::sqrt (std::max (0.0, sf2[b] / count - (sf[b] / count) * (sf[b] / count)));
            lsd[b] = std::sqrt (std::max (0.0, sl2[b] / count - (sl[b] / count) * (sl[b] / count)));
        }
        std::printf ("    seed %3d: frequency spread %.3f / %.3f / %.3f oct, level spread %.2f / %.2f / %.2f dB\n", seed, fsd[0],
                     fsd[1], fsd[2], lsd[0], lsd[1], lsd[2]);
        CHECK (fsd[2] > fsd[1] && fsd[1] > fsd[0] && fsd[0] > 0.0, "seed %d: High's frequency moves most, then Mid, then Low", seed);
        CHECK (lsd[2] > lsd[1] && lsd[1] > lsd[0] && lsd[0] > 0.0, "seed %d: High's level moves most, then Mid, then Low", seed);
        CHECK (fsd[2] > 0.15 && fsd[2] < 1.0, "seed %d: High swings a sensible amount: %.3f oct", seed, fsd[2]);
    }
}

TEST (seed_is_repeatable)
{
    const auto x = reese (2.0);
    auto render = [&] (int seed, int passes) {
        auto e = engine ();
        e->setParam (kSeed, seed);
        e->setParam (kMovement, 1.0);
        e->setParam (kPasses, passes);
        e->reset ();
        std::vector<float> r;
        auto l = run (*e, x, &r);
        l.insert (l.end (), r.begin (), r.end ());
        return l;
    };
    const auto a = render (5, 0), b = render (5, 0), c = render (6, 0);
    CHECK (a == b, "the same Seed renders bit for bit the same");
    CHECK (a != c, "another Seed renders differently");
    double diff = 0.0;
    for (size_t i = 0; i < a.size (); ++i)
        diff = std::max (diff, (double)std::fabs (a[i] - c[i]));
    CHECK (diff > 0.01, "and audibly: %.3f", diff);
    CHECK (render (9, 1) == render (9, 1), "two passes too");
    // the patterns themselves: the same for the same Seed, a different stream for the second pass
    const Pattern p1 = makePattern (77, 0), p2 = makePattern (77, 0), q = makePattern (77, 1);
    CHECK (std::memcmp (&p1.band, &p2.band, sizeof (p1.band)) == 0, "makePattern is deterministic");
    CHECK (p1.band[2].freq.rate != q.band[2].freq.rate && p1.band[0].freq.key != q.band[0].freq.key,
           "the second pass moves on its own");
}

TEST (transport_follows_song)
{
    // Sync: the phase is the song position over the cycle's beats, whatever came before
    auto e = engine ();
    e->setParam (kSync, 1.0);
    e->setParam (kSyncRate, 2); // 1 bar: 4 beats
    e->reset ();
    std::vector<float> in (256, 0.1f), l (256), r (256);
    e->setTransport (140.0, 0.0, false);
    for (int i = 0; i < 50; ++i)
        e->process (in.data (), in.data (), l.data (), r.data (), 256); // (stopped: running on its own)
    e->setTransport (140.0, 10.0, true);
    e->process (in.data (), in.data (), l.data (), r.data (), 256);
    const double expected = 10.0 / 4.0 + 256.0 * 140.0 / 60.0 / 4.0 / kSr;
    CHECK (std::fabs (e->phase () - expected) < 1e-9, "synced: the phase follows the song (%.6f, expected %.6f)", e->phase (), expected);

    // free: two renders from the same song position give the same output, however long the first
    // instance ran before
    auto renderFrom = [&] (int warmBlocks) {
        auto f = engine ();
        f->setParam (kMovement, 1.0);
        f->reset ();
        const auto x = reese (1.0);
        std::vector<float> ll (256), rr (256);
        f->setTransport (128.0, 0.0, false);
        for (int i = 0; i < warmBlocks; ++i)
            f->process (x.data (), x.data (), ll.data (), rr.data (), 256);
        f->reset ();
        f->setParam (kMovement, 1.0);
        double ppq = 32.0;
        std::vector<float> outL;
        for (size_t a = 0; a + 256 <= x.size (); a += 256)
        {
            f->setTransport (128.0, ppq, true);
            f->process (x.data () + a, x.data () + a, ll.data (), rr.data (), 256);
            outL.insert (outL.end (), ll.begin (), ll.end ());
            ppq += 256.0 * 128.0 / 60.0 / kSr;
        }
        return outL;
    };
    const auto a = renderFrom (0), b = renderFrom (77);
    CHECK (a == b, "free: playback from the same song position renders the same");
    // and a jump on the timeline moves the phase there
    auto g = engine ();
    g->setTransport (120.0, 0.0, true);
    g->process (in.data (), in.data (), l.data (), r.data (), 256);
    g->setTransport (120.0, 64.0, true); // (a jump: 64 beats is 32 s at 120 BPM)
    g->process (in.data (), in.data (), l.data (), r.data (), 256);
    const double rate = toPlain (kRate, defaultNormalized (kRate));
    CHECK (std::fabs (g->phase () - (32.0 * rate + 256.0 * rate / kSr)) < 1e-9, "a jump: the phase follows (%.4f)", g->phase ());
}

TEST (glue_reduces_crest)
{
    // a Reese swelling between -20 dB and +1 dB (1.5 times a second): the compressor evens the swells out,
    // so the crest factor falls
    std::vector<float> x = reese (2.0);
    for (size_t i = 0; i < x.size (); ++i)
        x[i] *= (float)(0.1 + 1.0 * (0.5 - 0.5 * std::cos (2.0 * kPi * 1.5 * (double)i / kSr)));
    auto crest = [&] (double glue) {
        auto e = engine ();
        plain (*e);
        e->setParam (kGlue, glue);
        e->reset ();
        const auto y = run (*e, x);
        const size_t a = (size_t)(0.4 * kSr);
        return db (peak (y, a, y.size ()) / rms (y, a, y.size ()));
    };
    const double off = crest (0.0), on = crest (1.0), half = crest (0.5);
    std::printf ("    crest factor: Glue 0 %.2f dB, 50 %% %.2f dB, 100 %% %.2f dB\n", off, half, on);
    CHECK (on < off - 2.0 && half < off, "Glue lowers the crest factor");
    auto e = engine ();
    plain (*e);
    e->setParam (kGlue, 1.0);
    e->reset ();
    run (*e, x);
    CHECK (e->glueReductionDb () > 1.0, "and reports its gain reduction: %.1f dB", e->glueReductionDb ());
}

TEST (grit_adds_harmonics)
{
    // a 110 Hz sine through the Low band opened up (1 kHz): its third harmonic with Grit 0 and 100 %
    auto third = [] (double grit) {
        auto e = engine ();
        plain (*e);
        e->setParam (kLowFreq, 1000.0);
        e->setParam (kGrit, grit);
        e->reset ();
        solo (*e, 0);
        const auto x = sine (110.0, 0.5, 0.5);
        const auto y = run (*e, x);
        const size_t a = (size_t)(0.25 * kSr);
        return db (toneAt (y, 330.0, a, y.size ()) / toneAt (y, 110.0, a, y.size ()));
    };
    const double clean = third (0.0), dirty = third (1.0);
    std::printf ("    3rd harmonic: %.1f dB (Grit 0), %.1f dB (Grit 100 %%)\n", clean, dirty);
    CHECK (dirty > clean + 30.0 && dirty > -40.0, "Grit adds harmonics");
    // and the soft clipper's aliasing stays low: a 9 kHz sine driven hard, energy away from its harmonics
    dsp::Saturator s;
    s.maxDb = 24.0;
    s.setAmount (1.0);
    s.reset ();
    std::vector<double> l (48000), r (48000);
    for (size_t i = 0; i < l.size (); ++i)
        l[i] = r[i] = 0.5 * std::sin (2.0 * kPi * 9000.0 * (double)i / kSr);
    for (size_t a = 0; a < l.size (); a += 16)
        s.process (l.data () + a, r.data () + a, 16);
    std::vector<float> y (l.begin (), l.end ());
    // 27 kHz folds to 21 kHz, 45 kHz to 3 kHz: the alias at 3 kHz against the fundamental
    const double alias = db (toneAt (y, 3000.0, 4800, y.size ()) / toneAt (y, 9000.0, 4800, y.size ()));
    std::printf ("    a 9 kHz sine clipped hard: its 3 kHz alias at %.1f dB\n", alias);
    CHECK (alias < -30.0, "the clipper is anti-aliased: %.1f dB", alias);
}

TEST (mix_zero_is_dry)
{
    auto e = engine ();
    e->setParam (kMix, 0.0);
    e->setParam (kMovement, 1.0);
    e->setParam (kPasses, kPasses2);
    e->reset ();
    const int lat = e->latency ();
    const auto x = noise (0.5, 1.0, 11);
    const auto y = run (*e, x);
    bool same = true;
    for (size_t i = (size_t)lat; i < x.size () && same; ++i)
        same = y[i] == x[i - (size_t)lat];
    std::printf ("    latency %d samples (the end saturator's)\n", lat);
    CHECK (same, "Mix 0: the input bit for bit, %d samples late", lat);
    smacheratr::Tail t;
    t.prepare (kSr, 512);
    CHECK (lat == t.latency (), "the latency is the end saturator's alone: %d", lat);
}

TEST (passes)
{
    const auto x = reese (4.0);
    auto render = [&] (int passes) {
        auto e = engine ();
        e->setParam (kPasses, passes);
        e->reset ();
        return run (*e, x);
    };
    const auto one = render (kPasses1), two = render (kPasses2);
    const size_t a = (size_t)(0.5 * kSr);
    double diff = 0.0;
    for (size_t i = a; i < x.size (); ++i)
        diff = std::max (diff, (double)std::fabs (one[i] - two[i]));
    const double l1 = db (rms (one, a, x.size ())), l2 = db (rms (two, a, x.size ()));
    std::printf ("    level: 1 pass %.1f dBFS, 2 passes %.1f dBFS (input %.1f)\n", l1, l2, db (rms (x, a, x.size ())));
    CHECK (diff > 0.01, "2 passes differ from 1: %.3f", diff);
    CHECK (finite (two) && peak (two, 0, two.size ()) < 2.0, "2 passes stay finite and bounded");
    CHECK (l2 > -40.0 && l2 < 0.0, "and at a sensible level: %.1f dBFS", l2);
    // switching Passes crossfades: no jump bigger than the signal's own steps
    auto e = engine ();
    e->reset ();
    std::vector<float> l (x.size ()), r (x.size ());
    double maxStep = 0.0;
    for (size_t b = 0; b < x.size (); b += 256)
    {
        if (b == (size_t)(2.0 * kSr))
            e->setParam (kPasses, kPasses2);
        e->process (x.data () + b, x.data () + b, l.data () + b, r.data () + b, 256);
    }
    for (size_t i = (size_t)(1.0 * kSr); i < x.size (); ++i)
        maxStep = std::max (maxStep, (double)std::fabs (l[i] - l[i - 1]));
    CHECK (maxStep < 0.5, "switching Passes does not click: largest step %.3f", maxStep);
}

TEST (silence_and_resonance)
{
    // silence in, silence out (exactly)
    {
        auto e = engine ();
        e->setParam (kPasses, kPasses2);
        e->setParam (kMovement, 1.0);
        e->reset ();
        const std::vector<float> x (48000, 0.0f);
        std::vector<float> r;
        const auto l = run (*e, x, &r);
        CHECK (peak (l, 0, l.size ()) == 0.0 && peak (r, 0, r.size ()) == 0.0, "silence in, silence out");
    }
    // every Res at the maximum, moving fast, two passes, the hottest settings: an impulse rings out and
    // dies away (no self-oscillation), never past a bound, and leaves no denormals or NaNs
    {
        auto e = engine ();
        for (uint32_t id : {kLowRes, kMidRes, kHighRes})
            e->setParam (id, 1.0);
        e->setParam (kMovement, 1.0);
        e->setParam (kRate, 2.0);
        e->setParam (kPasses, kPasses2);
        e->setParam (kSlope, kSlope24);
        e->setParam (kDrive, 1.0);
        e->setParam (kGrit, 1.0);
        e->setParam (kGlue, 1.0);
        e->setParam (kLowLevel, 12.0);
        e->setParam (kMidLevel, 12.0);
        e->setParam (kHighLevel, 12.0);
        e->reset ();
        std::vector<float> x ((size_t)(6.0 * kSr), 0.0f);
        x[100] = 1.0f;
        const auto loud = noise (1.0, 1.0, 5);
        std::copy (loud.begin (), loud.end (), x.begin () + 1000);
        std::vector<float> r;
        const auto l = run (*e, x, &r);
        const double top = std::max (peak (l, 0, l.size ()), peak (r, 0, r.size ()));
        const double tailLevel = std::max (peak (l, l.size () - 4800, l.size ()), peak (r, r.size () - 4800, r.size ()));
        int denormals = 0;
        for (size_t i = 0; i < l.size (); ++i)
        {
            const float tiny = std::numeric_limits<float>::min ();
            denormals += (l[i] != 0.0f && std::fabs (l[i]) < tiny) || (r[i] != 0.0f && std::fabs (r[i]) < tiny);
        }
        std::printf ("    max resonance: peak %.2f, the last 0.1 s at %.1f dBFS\n", top, db (tailLevel));
        CHECK (finite (l) && finite (r), "finite");
        CHECK (top < 4.0, "bounded: %.2f", top);
        CHECK (tailLevel < 1e-5, "rings out (no self-oscillation): %.2e", tailLevel);
        CHECK (denormals == 0, "no denormals (%d)", denormals);
    }
}

TEST (cpu_budget)
{
    // 10 s of a stereo Reese, the defaults and the heaviest settings (2 passes, 24 dB, full movement, the end
    // saturator on): CPU time, the best of three renders
    const auto x = reese (10.0);
    for (bool heavy : {false, true})
    {
        double secs = 1e9;
        std::vector<float> l;
        for (int i = 0; i < 3; ++i)
        {
            auto e = engine ();
            if (heavy)
            {
                e->setParam (kPasses, kPasses2);
                e->setParam (kSlope, kSlope24);
                e->setParam (kMovement, 1.0);
                e->setParam (kRate, 2.0);
                e->setParam (kDrive, 0.5);
                e->setParam (kGlue, 1.0);
                e->setParam (kGrit, 1.0);
                e->setParam (kTailBase + pk::kTailOn, 1.0);
            }
            e->reset ();
            const std::clock_t t0 = std::clock ();
            l = run (*e, x, nullptr, 512);
            secs = std::min (secs, (double)(std::clock () - t0) / CLOCKS_PER_SEC);
        }
        CHECK (finite (l), "finite");
        std::printf ("    CPU: %.2f%% of one core (%s)\n", 100.0 * secs / 10.0,
                     heavy ? "2 passes, 24 dB, full movement, the end saturator on" : "the defaults");
        CHECK (secs / 10.0 < (heavy ? 0.15 : 0.08), "too slow");
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
