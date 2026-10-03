// Headless tests for the Dropr DSP. Run: ./dropr_tests [filter]
#include "Engine.h"
#include "Gain.h"
#include "Params.h"

#include <algorithm>
#include <cmath>
#include <complex>
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

static std::unique_ptr<Engine> engine ()
{
    auto e = std::make_unique<Engine> ();
    e->prepare (kSr, 512);
    return e;
}

// every gain neutral, no compression, no saturator: the bands should add up to the input (all-passed)
static void neutral (Engine& e)
{
    e.setParam (kInput, 0.0);
    e.setParam (kNegative, 0.0);
    e.setParam (kDownRatio, 1.0);
    e.setParam (kUpRatio, 1.0);
    e.setParam (kMakeup, 0.0);
    e.setParam (kTilt, 0.0);
    e.setParam (kAdaptive, 0.0);
    for (int k = 0; k < kMaxBands; ++k)
        e.setParam (kBandGain1 + (uint32_t)k, 0.0);
    e.setParam (kTailBase + pk::kTailOn, 0.0);
    e.reset ();
}

static Sig tone (double hz, double amp, double secs, double ampR = -1.0)
{
    Sig s;
    const size_t n = (size_t)(secs * kSr);
    s.l.resize (n);
    s.r.resize (n);
    for (size_t i = 0; i < n; ++i)
    {
        const double v = std::sin (2.0 * M_PI * hz * (double)i / kSr);
        s.l[i] = (float)(amp * v);
        s.r[i] = (float)((ampR < 0.0 ? amp : ampR) * v);
    }
    return s;
}

// the amplitude (from the RMS) of x over [a, b) seconds, in dB
static double ampDb (const std::vector<float>& x, double a, double b)
{
    const size_t i0 = (size_t)(a * kSr), i1 = std::min (x.size (), (size_t)(b * kSr));
    double s = 0.0;
    for (size_t i = i0; i < i1; ++i)
        s += (double)x[i] * x[i];
    return 20.0 * std::log10 (std::max (1e-12, std::sqrt (2.0 * s / (double)std::max<size_t> (1, i1 - i0))));
}

static double db (double a) { return 20.0 * std::log10 (a); }

TEST (bands_sum_flat)
{
    for (int bands : {6, 3, 1})
    {
        auto e = engine ();
        e->setParam (kBands, bands);
        neutral (*e);
        double worst = 0.0;
        for (double hz : {25.0, 50.0, 70.0, 150.0, 400.0, 800.0, 1500.0, 2150.0, 3000.0, 4500.0, 7000.0, 11000.0, 16000.0})
        {
            e->reset ();
            const Sig out = run (*e, tone (hz, 0.5, 0.6));
            const double d = ampDb (out.l, 0.3, 0.6) - db (0.5);
            worst = std::max (worst, std::fabs (d));
        }
        std::printf ("    %d bands: flat within %.3f dB\n", bands, worst);
        CHECK (worst < 0.05, "%d bands sum flat (%.3f dB off)", bands, worst);
    }
    // the same with every band's gain at +6 dB: +6 dB everywhere
    auto e = engine ();
    neutral (*e);
    for (int k = 0; k < kMaxBands; ++k)
        e->setParam (kBandGain1 + (uint32_t)k, 6.0);
    e->reset ();
    const Sig out = run (*e, tone (1000.0, 0.25, 0.5));
    const double d = ampDb (out.l, 0.3, 0.5) - db (0.25);
    CHECK (std::fabs (d - 6.0) < 0.05, "every band +6 dB: %.3f dB", d);
}

TEST (crossovers_where_set)
{
    const double sets[2][kNumXovers] = {{70.0, 800.0, 2150.0, 4500.0, 11000.0}, {120.0, 400.0, 1000.0, 3000.0, 8000.0}};
    for (const auto& set : sets)
        for (int j = 0; j < kNumXovers; ++j)
        {
            auto e = engine ();
            for (int i = 0; i < kNumXovers; ++i)
                e->setParam (kXover1 + (uint32_t)i, set[i]);
            neutral (*e);
            e->setParam (kBandGain1 + (uint32_t)j + 1, -36.0); // the band above the crossover, gone
            e->reset ();
            auto at = [&] (double hz) {
                e->reset ();
                const Sig out = run (*e, tone (hz, 0.5, 0.5));
                return ampDb (out.l, 0.3, 0.5) - db (0.5);
            };
            // Linkwitz-Riley: each side -6 dB at the crossover; the exact sum of the bands from the filters'
            // responses (the other bands' parts near it count too)
            auto theory = [&] (double hz) {
                std::complex<double> sum = 0.0, above = 1.0;
                for (int k = 0; k < kMaxBands; ++k)
                {
                    std::complex<double> b = above;
                    if (k < kNumXovers)
                    {
                        const auto r = multidyn::xoverResponse (set[k], multidyn::kXover24, hz, kSr);
                        b *= r.low;
                        above *= r.high;
                        for (int m = k + 1; m < kNumXovers; ++m)
                            b *= multidyn::xoverResponse (set[m], multidyn::kXover24, hz, kSr).allpass;
                    }
                    sum += b * (k == j + 1 ? std::pow (10.0, -36.0 / 20.0) : 1.0);
                }
                return db (std::abs (sum));
            };
            const double expect = theory (set[j]);
            const double on = at (set[j]), below = at (set[j] / 3.0);
            CHECK (std::fabs (on - expect) < 0.05 && std::fabs (expect + 6.0) < 1.0, "crossover %d at %.0f Hz: %.2f dB there (expected %.2f)", j + 1, set[j], on, expect);
            CHECK (below > -0.3, "crossover %d: a third of it below is untouched (%.2f dB)", j + 1, below);
        }
}

// one band, a steady 1 kHz tone at `inDb`: the output level
static double steadyOut (double inDb, const std::function<void (Engine&)>& set)
{
    auto e = engine ();
    e->setParam (kBands, 1.0);
    neutral (*e);
    e->setParam (kAttack, 1.0);
    e->setParam (kRelease, 50.0);
    e->setParam (kKnee, 0.0);
    set (*e);
    e->reset ();
    const Sig out = run (*e, tone (1000.0, std::pow (10.0, inDb / 20.0), 0.6));
    return ampDb (out.l, 0.4, 0.6);
}

TEST (downward_vs_theory)
{
    struct Case
    {
        const char* name;
        bool negative;
        double ratio, range, expect;
    };
    // a tone at -20 dB, threshold -40 dB: 20 dB over
    const Case cases[] = {{"1 : 4", false, 4.0, 12.0, -35.0},
                          {"1 : inf", false, kRatioInf, 12.0, -40.0},
                          {"1 : -0.5 (Range 60)", true, 0.5, 60.0, -50.0},
                          {"1 : -1 (Range 60)", true, 1.0, 60.0, -60.0},
                          {"1 : -1 (Range 12: the floor)", true, 1.0, 12.0, -52.0},
                          {"1 : -inf (Range 12)", true, kRatioInf, 12.0, -52.0},
                          {"1 : -inf (Range 30)", true, kRatioInf, 30.0, -70.0}};
    for (const Case& c : cases)
    {
        const double out = steadyOut (-20.0, [&] (Engine& e) {
            e.setParam (kDownThreshold, -40.0);
            e.setParam (kNegative, c.negative ? 1.0 : 0.0);
            e.setParam (c.negative ? kNegRatio : kDownRatio, c.ratio);
            e.setParam (kRange, c.range);
        });
        // the law itself
        GainLaw g = GainLaw::from ([&] (uint32_t id) {
            switch (id)
            {
                case kDownThreshold: return -40.0;
                case kNegative: return c.negative ? 1.0 : 0.0;
                case kNegRatio:
                case kDownRatio: return c.ratio;
                case kRange: return c.range;
                case kUpRatio: return 1.0;
                default: return paramTable ().info (id).def;
            }
        });
        std::printf ("    %-30s out %.2f dB (theory %.1f)\n", c.name, out, c.expect);
        CHECK (std::fabs (out - c.expect) < 0.3, "%s: %.2f dB, expected %.1f", c.name, out, c.expect);
        CHECK (std::fabs (-20.0 + g.gain (-20.0) - c.expect) < 1e-6, "%s: the law gives %.2f", c.name, -20.0 + g.gain (-20.0));
    }
    // below the threshold: untouched
    const double under = steadyOut (-50.0, [] (Engine& e) {
        e.setParam (kDownThreshold, -40.0);
        e.setParam (kNegative, 1.0);
        e.setParam (kNegRatio, kRatioInf);
    });
    CHECK (std::fabs (under + 50.0) < 0.1, "below the threshold untouched: %.2f dB", under);
    // the soft knee: at the threshold itself, 1 : inf and a 12 dB knee take W/8 = 1.5 dB off
    const double knee = steadyOut (-40.0, [] (Engine& e) {
        e.setParam (kDownThreshold, -40.0);
        e.setParam (kDownRatio, kRatioInf);
        e.setParam (kKnee, 12.0);
    });
    CHECK (std::fabs (knee + 41.5) < 0.2, "the knee at the threshold: %.2f dB (expected -41.5)", knee);
}

TEST (negative_louder_gives_quieter)
{
    double last = 1e9;
    for (double in : {-36.0, -30.0, -24.0, -18.0, -12.0})
    {
        const double out = steadyOut (in, [] (Engine& e) {
            e.setParam (kDownThreshold, -40.0);
            e.setParam (kNegative, 1.0);
            e.setParam (kNegRatio, 1.0);
            e.setParam (kRange, 60.0);
        });
        std::printf ("    in %.0f dB -> out %.2f dB\n", in, out);
        CHECK (out < last - 5.0, "louder in (%.0f dB) gives quieter out (%.2f dB after %.2f)", in, out, last);
        last = out;
    }
}

TEST (upward_lifts_a_quiet_band)
{
    // one band: a tone at -50 dB, upward threshold -30 dB, 1 : 2: +10 dB
    const double one = steadyOut (-50.0, [] (Engine& e) {
        e.setParam (kUpThreshold, -30.0);
        e.setParam (kUpRatio, 2.0);
    });
    CHECK (std::fabs (one + 40.0) < 0.3, "1 : 2 lifts -50 dB to -40 dB: %.2f dB", one);
    const double cap = steadyOut (-90.0, [] (Engine& e) {
        e.setParam (kUpThreshold, -30.0);
        e.setParam (kUpRatio, kRatioInf);
    });
    CHECK (std::fabs (cap - (-90.0 + kMaxUpDb)) < 0.3, "1 : inf lifts at most %.0f dB: %.2f dB", kMaxUpDb, cap);
    // six bands: a quiet tone in band 2 comes up by 10 dB (the others turned away)
    auto e = engine ();
    neutral (*e);
    for (int k = 0; k < kMaxBands; ++k)
        e->setParam (kBandGain1 + (uint32_t)k, k == 1 ? 0.0 : -36.0);
    e->setParam (kUpThreshold, -30.0);
    e->setParam (kUpRatio, 2.0);
    e->setParam (kAttack, 1.0);
    e->setParam (kRelease, 50.0);
    e->reset ();
    const double hz = std::sqrt (70.0 * 800.0);
    const Sig out = run (*e, tone (hz, std::pow (10.0, -50.0 / 20.0), 1.0));
    const double lifted = ampDb (out.l, 0.6, 1.0);
    std::printf ("    band 2 at %.0f Hz: -50 dB -> %.2f dB\n", hz, lifted);
    CHECK (std::fabs (lifted + 40.0) < 0.5, "the quiet band comes up 10 dB: %.2f dB", lifted);
}

TEST (attack_release_times)
{
    // two bands split at 200 Hz, a 3 kHz tone stepping from -50 dB to -10 dB and back: threshold -40 dB, 1 : inf,
    // so the gain goes from 0 to -30 dB and back
    auto measure = [] (double adaptive, double& atkMs, double& relMs) {
        auto e = engine ();
        e->setParam (kBands, 2.0);
        e->setParam (kXover1, 200.0);
        neutral (*e);
        e->setParam (kDownThreshold, -40.0);
        e->setParam (kDownRatio, kRatioInf);
        e->setParam (kAttack, 10.0);
        e->setParam (kRelease, 200.0);
        e->setParam (kAdaptive, adaptive);
        e->reset ();
        Sig in = tone (3000.0, 1.0, 2.0);
        for (size_t i = 0; i < in.l.size (); ++i)
        {
            const double t = i / kSr;
            const float a = (float)(t >= 0.5 && t < 1.0 ? std::pow (10.0, -10.0 / 20.0) : std::pow (10.0, -50.0 / 20.0));
            in.l[i] *= a;
            in.r[i] *= a;
        }
        // the band's gain, read from the meters every millisecond
        Meters m;
        e->setMeters (&m);
        std::vector<double> g;
        Sig out = in;
        for (size_t pos = 0; pos + 48 <= in.l.size (); pos += 48)
        {
            e->process (in.l.data () + pos, in.r.data () + pos, out.l.data () + pos, out.r.data () + pos, 48);
            g.push_back (m.gainDb[1].load ());
        }
        atkMs = relMs = -1.0;
        const double atkMark = -30.0 * (1.0 - std::exp (-1.0)), relMark = -30.0 * std::exp (-1.0);
        for (size_t i = 500; i < 1000 && atkMs < 0; ++i)
            if (g[i] <= atkMark)
                atkMs = (double)i - 500.0 + 1.0;
        for (size_t i = 1000; i < g.size () && relMs < 0; ++i)
            if (g[i] >= relMark)
                relMs = (double)i - 1000.0 + 1.0;
    };
    double atk, rel, atkA, relA;
    measure (0.0, atk, rel);
    measure (1.0, atkA, relA);
    std::printf ("    Attack 10 ms: 63 %% after %.1f ms; Release 200 ms: 63 %% after %.1f ms (the detector holds 5 ms first)\n", atk, rel);
    std::printf ("    Adaptive Time 100 %%: %.1f ms and %.1f ms\n", atkA, relA);
    CHECK (std::fabs (atk - 10.0) < 1.5, "attack time constant %.1f ms", atk);
    CHECK (std::fabs (rel - 205.0) < 15.0, "release time constant %.1f ms (200 ms after the 5 ms hold)", rel);
    CHECK (atkA > 0 && atkA < 0.5 * atk, "Adaptive Time speeds up the large move (%.1f ms)", atkA);
    CHECK (relA > 0 && relA < 0.7 * rel, "and the release (%.1f ms)", relA);
}

// a snare-like hit every 0.5 s: a fast transient (a 2 ms crack of noise and a 220 Hz knock, gone in about
// 5 ms) and a body (190 Hz and noise, decaying over 120 ms) 20 dB under it
static Sig snare (double secs)
{
    Sig s;
    const size_t n = (size_t)(secs * kSr);
    s.l.resize (n);
    s.r.resize (n);
    uint32_t seed = 7;
    for (size_t i = 0; i < n; ++i)
    {
        const double t = std::fmod (i / kSr, 0.5);
        seed = seed * 1664525u + 1013904223u;
        const double noise = (int32_t)seed / 2147483648.0;
        const double crack = 0.9 * std::exp (-t / 0.0015) * (0.6 * noise + 0.4 * std::sin (2 * M_PI * 220.0 * t));
        const double body = 0.09 * std::exp (-t / 0.12) * (0.7 * std::sin (2 * M_PI * 190.0 * t) + 0.3 * noise);
        s.l[i] = s.r[i] = (float)(crack + body);
    }
    return s;
}

// the transient's peak (first 5 ms of each hit) against the body's level (30 .. 150 ms), dB
static double transientToBody (const std::vector<float>& x, int latency)
{
    double sum = 0.0;
    int hits = 0;
    for (double onset = 1.0; onset + 0.5 <= (double)x.size () / kSr; onset += 0.5)
    {
        const size_t a = (size_t)(onset * kSr) + (size_t)latency;
        double pk = 0.0, s = 0.0;
        for (size_t i = a; i < a + 240; ++i)
            pk = std::max (pk, (double)std::fabs (x[i]));
        const size_t b0 = a + (size_t)(0.03 * kSr), b1 = a + (size_t)(0.15 * kSr);
        for (size_t i = b0; i < b1; ++i)
            s += (double)x[i] * x[i];
        const double bodyPk = std::sqrt (2.0 * s / (double)(b1 - b0));
        sum += db (pk / std::max (1e-9, bodyPk));
        ++hits;
    }
    return sum / std::max (1, hits);
}

TEST (snare_defaults_clamp_the_snap)
{
    const Sig in = snare (4.0);
    auto e = engine ();
    const Sig out = run (*e, in);
    const double before = transientToBody (in.l, 0), after = transientToBody (out.l, e->latency ());
    double pk = 0.0, s = 0.0;
    for (float v : out.l)
    {
        pk = std::max (pk, (double)std::fabs (v));
        s += (double)v * v;
    }
    std::printf ("    transient-to-body: %.1f dB in, %.1f dB out (defaults); output peak %.2f dBFS, RMS %.1f dBFS\n", before, after,
                 db (pk), 10.0 * std::log10 (s / (double)out.l.size ()));
    CHECK (after < before - 12.0, "the snap is clamped and the body comes up: %.1f dB -> %.1f dB", before, after);
    CHECK (db (pk) <= 0.01, "Hard Clip holds the end to 0 dBFS (%.2f dBFS)", db (pk));
    CHECK (db (pk) > -1.0, "the defaults land near 0 dBFS (%.2f dBFS)", db (pk));
    CHECK (10.0 * std::log10 (s / (double)out.l.size ()) > -16.0, "and loud (the body driven into the saturator)");
}

TEST (channel_link)
{
    // one band, threshold -40 dB, 1 : inf: left at -10 dB (30 dB over), right at -40 dB (at the threshold)
    auto rightOut = [] (double link) {
        auto e = engine ();
        e->setParam (kBands, 1.0);
        neutral (*e);
        e->setParam (kDownThreshold, -40.0);
        e->setParam (kDownRatio, kRatioInf);
        e->setParam (kAttack, 1.0);
        e->setParam (kRelease, 50.0); // (the onset's overshoot is gone well before the measurement)
        e->setParam (kLink, link);
        e->reset ();
        const Sig out = run (*e, tone (1000.0, std::pow (10.0, -10.0 / 20.0), 0.6, std::pow (10.0, -40.0 / 20.0)));
        return std::make_pair (ampDb (out.l, 0.4, 0.6), ampDb (out.r, 0.4, 0.6));
    };
    const auto full = rightOut (1.0), none = rightOut (0.0), half = rightOut (0.5);
    std::printf ("    right channel: %.1f dB linked, %.1f dB at 50 %%, %.1f dB unlinked (left %.1f dB)\n", full.second, half.second,
                 none.second, full.first);
    CHECK (std::fabs (full.second + 70.0) < 0.5, "linked: the right gets the left's 30 dB (%.1f dB)", full.second);
    CHECK (std::fabs (none.second + 40.0) < 0.3, "unlinked: the right untouched (%.1f dB)", none.second);
    CHECK (std::fabs (half.second + 55.0) < 0.5, "50 %%: half way (in dB: %.1f dB)", half.second);
    CHECK (std::fabs (full.first + 40.0) < 0.3 && std::fabs (none.first + 40.0) < 0.3, "the left at the threshold either way");
}

TEST (mid_side)
{
    // a mono signal in Mid-Side: the side is silent, so it sounds like Stereo; a side-only signal is
    // compressed on its own level
    auto run1 = [] (int mode, double ampR) {
        auto e = engine ();
        e->setParam (kBands, 1.0);
        neutral (*e);
        e->setParam (kDownThreshold, -40.0);
        e->setParam (kDownRatio, kRatioInf);
        e->setParam (kAttack, 1.0);
        e->setParam (kRelease, 50.0);
        e->setParam (kLink, 0.0);
        e->setParam (kMode, mode);
        e->reset ();
        Sig in = tone (1000.0, 0.1, 0.6);
        for (size_t i = 0; i < in.r.size (); ++i)
            in.r[i] = (float)(in.l[i] * ampR);
        const Sig out = run (*e, in);
        return std::make_pair (ampDb (out.l, 0.4, 0.6), ampDb (out.r, 0.4, 0.6));
    };
    const auto st = run1 (kModeStereo, 1.0), ms = run1 (kModeMidSide, 1.0);
    CHECK (std::fabs (st.first - ms.first) < 0.1 && std::fabs (st.second - ms.second) < 0.1, "mono: Mid-Side = Stereo (%.2f / %.2f dB)",
           st.first, ms.first);
    const auto side = run1 (kModeMidSide, -1.0); // all side: its level is the tone's, 20 dB over
    CHECK (std::fabs (side.first + 40.0) < 0.3 && std::fabs (side.second + 40.0) < 0.3, "side only: compressed to the threshold (%.2f dB)",
           side.first);
}

TEST (no_clicks_on_changes)
{
    // a steady two-tone signal; Bands, Mode and a crossover change mid-way: the output never jumps more
    // from one sample to the next than the steady signal does (with a margin)
    auto e = engine ();
    neutral (*e);
    for (int k = 0; k < kMaxBands; ++k)
        e->setParam (kBandGain1 + (uint32_t)k, k % 2 ? 9.0 : -9.0);
    e->reset ();
    Sig in = tone (300.0, 0.3, 2.0);
    const Sig hi = tone (3000.0, 0.2, 2.0);
    for (size_t i = 0; i < in.l.size (); ++i)
    {
        in.l[i] += hi.l[i];
        in.r[i] += hi.r[i];
    }
    Sig out;
    out.l.resize (in.l.size ());
    out.r.resize (in.r.size ());
    const int block = 256;
    for (size_t pos = 0; pos < in.l.size (); pos += block)
    {
        const double t = pos / kSr;
        if (std::fabs (t - 0.5) < 1e-3)
            e->setParam (kBands, 2.0);
        if (std::fabs (t - 0.8) < 2e-3)
            e->setParam (kBands, 5.0);
        if (std::fabs (t - 1.1) < 2e-3)
            e->setParam (kMode, kModeMidSide);
        if (std::fabs (t - 1.4) < 2e-3)
            e->setParam (kXover1 + 1, 2000.0);
        const int m = (int)std::min<size_t> (block, in.l.size () - pos);
        e->process (in.l.data () + pos, in.r.data () + pos, out.l.data () + pos, out.r.data () + pos, m);
    }
    double steady = 0.0, worst = 0.0;
    for (size_t i = 1; i < out.l.size (); ++i)
    {
        const double d = std::fabs (out.l[i] - out.l[i - 1]);
        if (i < (size_t)(0.45 * kSr) && i > (size_t)(0.2 * kSr))
            steady = std::max (steady, d);
        else if (i > (size_t)(0.45 * kSr))
            worst = std::max (worst, d);
    }
    std::printf ("    largest step: %.4f steady, %.4f around the changes\n", steady, worst);
    CHECK (worst < steady * 2.0, "no clicks (%.4f against %.4f)", worst, steady);
}

TEST (constant_latency)
{
    smacheratr::Tail t;
    t.prepare (kSr, 512);
    auto e = engine ();
    const int lat = e->latency ();
    CHECK (lat == t.latency (), "the latency is the end saturator's (%d, %d)", lat, t.latency ());
    e->setParam (kBands, 3.0);
    e->setParam (kMode, kModeMidSide);
    e->setParam (kTailBase + pk::kTailOn, 0.0);
    e->setParam (kNegative, 0.0);
    CHECK (e->latency () == lat, "and it never changes (%d)", e->latency ());
    // an impulse with the saturator off and nothing compressing: the bands' all-pass starts at the latency
    neutral (*e);
    Sig in;
    in.l.assign (4800, 0.0f);
    in.r.assign (4800, 0.0f);
    in.l[100] = in.r[100] = 0.5f;
    const Sig out = run (*e, in);
    size_t first = 0;
    while (first < out.l.size () && std::fabs (out.l[first]) < 1e-4f)
        ++first;
    CHECK ((int)first - 100 == lat, "the impulse comes out %d samples later (latency %d)", (int)first - 100, lat);
    std::printf ("    latency %d samples (%.2f ms at 48 kHz)\n", lat, lat / 48.0);
}

TEST (fuzz_finite)
{
    uint32_t seed = 12345;
    auto rnd = [&] {
        seed = seed * 1664525u + 1013904223u;
        return (seed >> 8) / 16777216.0;
    };
    bool finite = true;
    for (int round = 0; round < 40; ++round)
    {
        auto e = engine ();
        for (uint32_t id = 0; id < kTailBase; ++id)
            e->setParam (id, toPlain (id, rnd ()));
        if (round % 3 == 0)
        {
            e->setParam (kInput, 48.0);
            e->setParam (kMakeup, 72.0);
            e->setParam (kTailBase + pk::kTailOn, 0.0);
        }
        e->reset ();
        Sig in;
        const size_t n = (size_t)(0.3 * kSr);
        in.l.resize (n);
        in.r.resize (n);
        for (size_t i = 0; i < n; ++i)
        {
            const double kind = std::fmod (i / kSr, 0.1);
            const double v = kind < 0.02 ? (rnd () * 2.0 - 1.0) : kind < 0.05 ? 0.0 : (rnd () * 2.0 - 1.0) * 1e-6;
            in.l[i] = (float)v;
            in.r[i] = (float)(round % 2 ? -v : v * 0.3);
        }
        Sig out;
        out.l.resize (n);
        out.r.resize (n);
        for (size_t pos = 0; pos < n;)
        {
            const int m = std::min<int> ((int)(n - pos), 1 + (int)(rnd () * 700));
            if (rnd () < 0.2)
            {
                const uint32_t id = (uint32_t)(rnd () * kTailBase);
                e->setParam (id, toPlain (id, rnd ()));
            }
            e->process (in.l.data () + pos, in.r.data () + pos, out.l.data () + pos, out.r.data () + pos, m);
            pos += (size_t)m;
        }
        for (size_t i = 0; i < n; ++i)
            finite = finite && std::isfinite (out.l[i]) && std::isfinite (out.r[i]);
    }
    CHECK (finite, "no NaN or inf, whatever the settings");
}

TEST (cpu)
{
    Sig in = snare (10.0);
    uint32_t seed = 1;
    for (size_t i = 0; i < in.l.size (); ++i)
    {
        seed = seed * 1664525u + 1013904223u;
        in.r[i] = in.l[i] + (float)((int32_t)seed / 2147483648.0 * 0.05);
    }
    // CPU time (other programs running do not count), the best of three renders, the defaults: 6 bands,
    // stereo, the saturator on
    double secs = 1e9;
    for (int i = 0; i < 3; ++i)
    {
        auto e = engine ();
        const std::clock_t t0 = std::clock ();
        run (*e, in, 333);
        secs = std::min (secs, (double)(std::clock () - t0) / CLOCKS_PER_SEC);
    }
    std::printf ("    CPU: %.2f%% of one core (6 bands, stereo, 48 kHz, saturator on)\n", 100.0 * secs / 10.0);
    CHECK (secs / 10.0 < 0.10, "too slow");
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
