// Headless tests for the Smacheratr DSP. Run: ./smacheratr_tests [filter]
#include "Color.h"
#include "ClarityBand.h"
#include "Engine.h"
#include "Glue.h"
#include "NoOverlap.h"
#include "Tail.h"
#include "Params.h"
#include "Shaper.h"

#include <array>
#include <chrono>
#include <cmath>
#include <ctime>
#include <cstring>
#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <vector>

using namespace smacheratr;

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

static Sig tones (std::vector<std::pair<double, double>> freqDb, double secs, double dc = 0.0)
{
    Sig s;
    const size_t n = (size_t)(secs * kSr);
    s.l.assign (n, (float)dc);
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

static double peakOf (const std::vector<float>& x, size_t a, size_t b)
{
    double pk = 0;
    for (size_t i = a; i < b && i < x.size (); ++i)
        pk = std::max (pk, (double)std::fabs (x[i]));
    return pk;
}

static double meanOf (const std::vector<float>& x, size_t a, size_t b)
{
    double s = 0;
    for (size_t i = a; i < b; ++i)
        s += x[i];
    return s / (double)(b - a);
}

// Largest difference between out and in delayed by `lat`, from `from` on.
static double delayedError (const std::vector<float>& out, const std::vector<float>& in, size_t lat, size_t from)
{
    double err = 0;
    for (size_t i = std::max (from, lat); i < out.size (); ++i)
        err = std::max (err, (double)std::fabs (out[i] - in[i - lat]));
    return err;
}

// An engine at Oversampling `mode` (OversamplingMode).
static std::unique_ptr<Engine> engineOs (int mode)
{
    auto e = std::make_unique<Engine> ();
    e->setParam (kOversampling, mode);
    e->setParam (kColorOn, 0.0); // the defaults have Color on; tests start neutral
    e->prepare (kSr, 512);
    return e;
}
// hiQuality: 4x (the default) or Off (what the old Hi-Quality switch's on and off were)
static std::unique_ptr<Engine> engine (bool hiQuality = true) { return engineOs (hiQuality ? kOs4x : kOsOff); }

// ---------------------------------------------------------------------------
// The half-band stage as it was before it went polyphase (Oversampler.cpp): the plain FIR at the 2x
// rate over a ring buffer, the up-sampler fed a zero every other sample. Kept as the reference.
class RefHalfband
{
public:
    void design (double passEdge, double attenDb, int maxTaps)
    {
        auto besselI0 = [] (double x) {
            double sum = 1.0, term = 1.0;
            const double hx = x * 0.5;
            for (int k = 1; k < 64; ++k)
            {
                term *= (hx / k) * (hx / k);
                sum += term;
                if (term < 1e-14 * sum)
                    break;
            }
            return sum;
        };
        const double df = std::max (0.005, 0.5 - passEdge);
        const double beta = attenDb > 50.0   ? 0.1102 * (attenDb - 8.7)
                            : attenDb >= 21.0 ? 0.5842 * std::pow (attenDb - 21.0, 0.4) + 0.07886 * (attenDb - 21.0)
                                              : 0.0;
        int n = (int)std::ceil ((attenDb - 8.0) / (2.285 * 2.0 * M_PI * df)) + 1;
        n = std::clamp (n, 5, maxTaps);
        n = 4 * ((n + 2) / 4) + 1;
        h.assign ((size_t)n, 0.0f);
        const int M = (n - 1) / 2;
        const double i0b = besselI0 (beta);
        double sum = 0.0;
        for (int m = 0; m < n; ++m)
        {
            const double t = m - M;
            const double sinc = t == 0.0 ? 1.0 : std::sin (M_PI * 0.5 * t) / (M_PI * 0.5 * t);
            const double r = 2.0 * m / (n - 1) - 1.0;
            const double w = besselI0 (beta * std::sqrt (std::max (0.0, 1.0 - r * r))) / i0b;
            h[(size_t)m] = (float)(0.5 * sinc * w);
            sum += h[(size_t)m];
        }
        for (auto& v : h)
            v = (float)(v / sum);
        int size = 1;
        while (size < n)
            size <<= 1;
        for (auto* b : {&upHist, &downHist})
            b->assign ((size_t)size, 0.0f);
        mask = size - 1;
    }
    void up (const float* in, float* out, int n)
    {
        for (int i = 0; i < n; ++i)
        {
            push (upHist, upPos, 2.0f * in[i]);
            out[2 * i] = run (upHist, upPos);
            push (upHist, upPos, 0.0f);
            out[2 * i + 1] = run (upHist, upPos);
        }
    }
    void down (const float* in, float* out, int n)
    {
        for (int i = 0; i < n; ++i)
        {
            push (downHist, downPos, in[2 * i]);
            out[i] = run (downHist, downPos);
            push (downHist, downPos, in[2 * i + 1]);
        }
    }

private:
    void push (std::vector<float>& b, int& pos, float x)
    {
        b[(size_t)pos] = x;
        pos = (pos + 1) & mask;
    }
    float run (const std::vector<float>& b, int pos) const
    {
        float acc = 0.0f;
        int i = (pos - 1) & mask;
        for (float tap : h)
        {
            acc += tap * b[(size_t)i];
            i = (i - 1) & mask;
        }
        return acc;
    }
    std::vector<float> h, upHist, downHist;
    int mask = 0, upPos = 0, downPos = 0;
};

TEST (oversampler_matches_plain_fir)
{
    // the polyphase half-band stages compute what the plain FIR computed (the same sums in the same
    // order: on x86 to the bit), for blocks of every length, at every rate
    double worst = 0.0;
    for (double sr : {44100.0, 48000.0, 96000.0, 192000.0})
    {
        const double pass = std::min (20000.0, 0.46 * sr);
        for (double edge : {pass / sr, pass / (2.0 * sr)}) // Oversampler's two stages
        {
            Halfband2x a;
            RefHalfband b;
            a.design (edge, 80.0, 257);
            b.design (edge, 80.0, 257);
            a.reset ();
            uint32_t seed = 9;
            std::vector<float> x (600), u1 (1200), u2 (1200), d1 (600), d2 (600);
            for (int blk = 0; blk < 40; ++blk)
            {
                const int n = blk % 5 == 0 ? 1 : (blk % 5 == 1 ? 3 : (blk % 5 == 2 ? 64 : (blk % 5 == 3 ? 257 : 600)));
                for (int i = 0; i < n; ++i)
                {
                    seed = seed * 1664525u + 1013904223u;
                    x[(size_t)i] = (float)((int32_t)seed / 2147483648.0);
                }
                a.up (x.data (), u1.data (), n);
                b.up (x.data (), u2.data (), n);
                for (int i = 0; i < 2 * n; ++i)
                {
                    worst = std::max (worst, (double)std::fabs (u1[(size_t)i] - u2[(size_t)i]));
                    u1[(size_t)i] = u2[(size_t)i] = std::tanh (2.0f * u2[(size_t)i]); // something in between
                }
                a.down (u1.data (), d1.data (), n);
                b.down (u2.data (), d2.data (), n);
                for (int i = 0; i < n; ++i)
                    worst = std::max (worst, (double)std::fabs (d1[(size_t)i] - d2[(size_t)i]));
            }
        }
    }
    std::printf ("    largest difference %.3g\n", worst);
    CHECK (worst < 1e-6, "%g", worst);
}

TEST (params)
{
    const auto& t = paramTable ();
    CHECK (t.size () == kNumParams, "size");
    for (uint32_t id = 0; id < kNumParams; ++id)
    {
        double v;
        CHECK (t.fromText (id, t.toText (id, t.info (id).def), v), "%s", t.info (id).name);
    }
    CHECK (t.toText (kColorWidth, 1.0) == "1.00", "%s", t.toText (kColorWidth, 1.0).c_str ());
    CHECK (t.toText (kColorLo, -0.5) == "-50 %", "%s", t.toText (kColorLo, -0.5).c_str ());
    CHECK (std::fabs (colorDb (-0.5) + 12.0) < 1e-9, "amount -> dB");
    CHECK (t.info (kDrive).def == 0.0 && t.info (kColorOn).def == 1.0, "defaults: Drive 0 dB, Color on");
    CHECK (t.info (kPreLimit).def == 1.0 && t.info (kPreLimitThreshold).def == -6.0, "pre-limit on, at -6 dB");
}

TEST (analog_curve)
{
    // odd, linear up to 0.5, smooth knee, never past +-1
    CHECK (analogClip (0.0) == 0.0 && std::fabs (analogClip (0.45) - 0.45) < 1e-12, "linear below 0.5");
    CHECK (analogClip (1.5) == 1.0 && analogClip (10.0) == 1.0 && analogClip (-10.0) == -1.0, "clips at 1");
    double prev = -2.0;
    for (double x = -4.0; x <= 4.0; x += 0.001)
    {
        const double y = analogClip (x);
        CHECK (std::fabs (y + analogClip (-x)) < 1e-12, "odd at %f", x);
        CHECK (y >= prev - 1e-12 && std::fabs (y) <= 1.0 + 1e-12, "monotonic and bounded at %f", x);
        prev = y;
    }
    // the knee has no corner: the slope is continuous at 0.5 and 1.5
    auto slope = [] (double x) { return (analogClip (x + 1e-6) - analogClip (x - 1e-6)) / 2e-6; };
    CHECK (std::fabs (slope (0.5) - 1.0) < 1e-3 && std::fabs (slope (1.5)) < 1e-3, "smooth knee: %f %f", slope (0.5), slope (1.5));
}

TEST (color_filters_are_exact_inverses)
{
    for (double rate : {48000.0, 192000.0})
        for (double g : {-24.0, -6.0, 3.0, 18.0})
        {
            const auto lo = lowShelf (rate, kColorLowHz, g), loInv = lowShelf (rate, kColorLowHz, -g);
            const auto pk = peak (rate, 1200.0, g, 2.0), pkInv = peak (rate, 1200.0, -g, 2.0);
            for (double hz : {30.0, 100.0, 400.0, 1200.0, 5000.0, 15000.0})
            {
                CHECK (std::fabs (magnitudeDb (lo, hz, rate) + magnitudeDb (loInv, hz, rate)) < 1e-9, "shelf %f dB at %f Hz", g, hz);
                CHECK (std::fabs (magnitudeDb (pk, hz, rate) + magnitudeDb (pkInv, hz, rate)) < 1e-9, "peak %f dB at %f Hz", g, hz);
            }
        }
    CHECK (std::fabs (colorResponseDb (10000.0, kSr, 0.5, 0.0, 1000.0, 1.0)) < 0.1, "shelf leaves highs alone");
    CHECK (std::fabs (colorResponseDb (20.0, kSr, 0.5, 0.0, 1000.0, 1.0) - 12.0) < 0.3, "+50 %% is +12 dB in the lows");
}

TEST (transparent_at_zero_drive)
{
    // the curve is linear below 0.5: a quiet signal comes out delayed by exactly latency()
    auto in = tones ({{60.0, -14.0}, {1000.0, -14.0}, {9000.0, -20.0}}, 1.0);
    for (bool hq : {true, false})
    {
        auto e = engine (hq);
        auto out = run (*e, in, 333);
        const int lat = e->latency ();
        CHECK (lat >= 48 + (hq ? 1 : 0) && lat < 200, "latency %d (look-ahead, and oversampling with hq)", lat);
        const double err = delayedError (out.l, in.l, (size_t)lat, 4800);
        CHECK (err < (hq ? 2e-3 : 1e-6), "hq %d: reconstruction error %g (latency %d)", hq, err, lat);
    }
    // the latency is the look-ahead's and the oversampler's: none with Off, 2x the first half-band stage's
    // alone (most of 4x's: the second stage is short), and the same while a setting is held
    {
        const int look = (int)std::lround (0.001 * kSr);
        auto off = engineOs (kOsOff), x2 = engineOs (kOs2x), x4 = engineOs (kOs4x);
        std::printf ("    latency at 48 kHz: Off %d, 2x %d, 4x %d samples\n", off->latency (), x2->latency (), x4->latency ());
        CHECK (off->latency () == look, "Off: the look-ahead alone (%d)", off->latency ());
        CHECK (x2->latency () > look && x2->latency () < x4->latency (), "2x between: %d (4x %d)", x2->latency (), x4->latency ());
        CHECK (engine ()->latency () == x4->latency (), "the default is 4x");
        // each one exact: a quiet signal comes out delayed by just that
        for (int mode : {(int)kOsOff, (int)kOs2x, (int)kOs4x})
        {
            auto e = engineOs (mode);
            const int lat = e->latency ();
            auto out = run (*e, in, 333);
            const double err = delayedError (out.l, in.l, (size_t)lat, 4800);
            CHECK (err < (mode == kOsOff ? 1e-6 : 2e-3) && e->latency () == lat, "mode %d: error %g at latency %d", mode, err, lat);
        }
        // switched while running: the next block runs at the new latency (and the meters say so)
        Meters m;
        auto e = engineOs (kOs4x);
        e->setMeters (&m);
        CHECK (m.latency.load () == x4->latency (), "published at 4x: %d", m.latency.load ());
        run (*e, in, 333);
        e->setParam (kOversampling, kOs2x);
        CHECK (e->latency () == x2->latency (), "reports 2x's once set");
        run (*e, in, 333);
        CHECK (m.latency.load () == x2->latency (), "runs at 2x's: %d", m.latency.load ());
        auto out = run (*e, in, 333);
        CHECK (delayedError (out.l, in.l, (size_t)x2->latency (), 4800) < 2e-3, "and is exact at it");
    }
    // the pre-limiter below its threshold changes nothing either, and keeps the latency
    auto e = engine (false);
    const int lat = e->latency ();
    e->setParam (kPreLimit, 1.0);
    e->setParam (kPreLimitThreshold, -6.0);
    auto out = run (*e, in, 333);
    CHECK (e->latency () == lat && delayedError (out.l, in.l, (size_t)lat, 4800) < 1e-6, "pre-limit under its threshold");
}

TEST (fully_dry_skips_the_curve_and_keeps_the_latency)
{
    // dry/wet 0: the dry signal, delayed; opening it again fades the curve in from silence
    auto in = tones ({{300.0, -6.0}}, 1.0);
    auto e = engine (true);
    e->setParam (kDrive, 24.0);
    e->setParam (kDryWet, 0.0);
    e->reset ();
    auto out = run (*e, in);
    CHECK (delayedError (out.l, in.l, (size_t)e->latency (), 0) < 1e-7, "dry and delayed");
    e->setParam (kDryWet, 1.0);
    auto wet = run (*e, in);
    bool finite = true;
    double jump = 0;
    for (size_t i = 1; i < wet.l.size (); ++i)
    {
        finite &= std::isfinite (wet.l[i]);
        jump = std::max (jump, (double)std::fabs (wet.l[i] - wet.l[i - 1]));
    }
    CHECK (finite && jump < 0.5, "no click when it opens: %f", jump);
    CHECK (toneDb (wet.l, 900.0, 24000, 48000) > -40.0, "and it saturates: %f", toneDb (wet.l, 900.0, 24000, 48000));
}

TEST (drive_adds_harmonics_and_the_curve_holds_one)
{
    auto in = tones ({{1000.0, -12.0}}, 1.0);
    const size_t a = 24000, b = 48000;
    auto e = engine (false);
    e->setParam (kDrive, 18.0);
    auto out = run (*e, in);
    CHECK (toneDb (out.l, 3000.0, a, b) > -30.0, "third harmonic %f dB", toneDb (out.l, 3000.0, a, b));
    CHECK (peakOf (out.l, a, b) <= 1.0 + 1e-6, "peak %f", peakOf (out.l, a, b));
    // with 4x oversampling the downsampling filter rings a little past the curve
    e = engine (true);
    e->setParam (kDrive, 18.0);
    out = run (*e, in);
    CHECK (peakOf (out.l, a, b) <= 1.05, "hi-quality peak %f", peakOf (out.l, a, b));
    e = engine (false);
    e->setParam (kDrive, -6.0);
    out = run (*e, in);
    CHECK (toneDb (out.l, 3000.0, a, b) < -90.0, "quiet signal is clean: %f dB", toneDb (out.l, 3000.0, a, b));
}

TEST (post_clip_never_leaves_above_0_dbfs)
{
    // driven into Soft or Hard Clip, nothing after the clip takes the output back over 0 dBFS: not the oversampler's
    // downsampling filter, Mid/Side back to left / right, Gentlr, the dry part of a mix or Output
    struct Case
    {
        bool hiq, ms, gentlr;
        double mix, outDb;
        const char* name;
    };
    const Case cases[] = {{true, false, false, 1.0, 0.0, "4x oversampling"},  {false, true, false, 1.0, 0.0, "Mid/Side"},
                          {true, true, false, 1.0, 0.0, "both"},         {false, false, true, 1.0, 0.0, "Gentlr"},
                          {true, false, false, 0.5, 0.0, "half dry"},    {true, false, false, 1.0, 6.0, "Output +6 dB"}};
    uint32_t seed = 3;
    auto rnd = [&] {
        seed = seed * 1664525u + 1013904223u;
        return (float)((seed >> 8) / 8388608.0 - 1.0);
    };
    const int N = 48000 * 2;
    std::vector<float> l (N), r (N);
    for (int i = 0; i < N; ++i)
    {
        l[i] = 0.6f * (rnd () + rnd () + rnd ());
        r[i] = 0.6f * (rnd () + rnd () + rnd ());
    }
    for (const Case& c : cases)
    {
        for (int post : {(int)kPostOff, (int)kPostSoft, (int)kPostHard})
        {
            auto e = engine (c.hiq);
            e->setParam (kDrive, 24.0);
            e->setParam (kPostClip, post);
            e->setParam (kMidSide, c.ms ? 1.0 : 0.0);
            e->setParam (kClarity, c.gentlr ? 1.0 : 0.0);
            e->setParam (kDryWet, c.mix);
            e->setParam (kOutput, c.outDb);
            std::vector<float> ol (N), orr (N);
            for (int p = 0; p < N; p += 512)
                e->process (l.data () + p, r.data () + p, ol.data () + p, orr.data () + p, std::min (512, N - p));
            float pk = 0.0f;
            for (int i = 0; i < N; ++i)
                pk = std::max (pk, std::max (std::fabs (ol[i]), std::fabs (orr[i])));
            if (post != kPostOff)
                CHECK (pk <= 1.0f, "%s, %s Clip: peak %.2f dBFS", c.name, post == kPostHard ? "Hard" : "Soft",
                       20.0 * std::log10 (pk));
            else
                std::printf ("    %s, no post clip: peak %.2f dBFS\n", c.name, 20.0 * std::log10 (pk));
        }
    }
}

TEST (pre_limiter_holds_transients_before_the_drive)
{
    // a quiet tone with a loud burst: without the limiter the burst is driven far past the knee
    // (and squared); with it the burst enters the curve no further than the limited level
    Sig in = tones ({{200.0, -24.0}}, 1.0);
    for (size_t i = 24000; i < 26400; ++i) // 50 ms at 0 dBFS
        in.l[i] = in.r[i] = (float)std::sin (2.0 * M_PI * 200.0 * i / kSr);
    Meters m;
    auto e = engine (false);
    e->setMeters (&m);
    e->setParam (kDrive, 12.0);
    e->setParam (kPreLimit, 1.0);
    e->setParam (kPreLimitThreshold, -18.0);
    float worst = 0.0f;
    for (size_t pos = 0; pos + 480 <= in.l.size (); pos += 480)
    {
        std::vector<float> ol (480), orr (480);
        e->process (in.l.data () + pos, in.r.data () + pos, ol.data (), orr.data (), 480);
        worst = std::max (worst, m.inPeak.load ());
    }
    // -18 dB limit + 12 dB drive: nothing enters the curve above -6 dB (0.5, the linear region)
    CHECK (worst < 0.53f, "driven input held at %.3f (want <= 0.5)", worst);
    e->setParam (kPreLimit, 0.0);
    e->reset ();
    worst = 0.0f;
    for (size_t pos = 0; pos + 480 <= in.l.size (); pos += 480)
    {
        std::vector<float> ol (480), orr (480);
        e->process (in.l.data () + pos, in.r.data () + pos, ol.data (), orr.data (), 480);
        worst = std::max (worst, m.inPeak.load ());
    }
    CHECK (worst > 3.5f, "without it the burst is driven to %.2f", worst);
}

TEST (post_clip_and_output)
{
    // negative Amt Lo lets a driven bass tone come out above 0 dB; the post clip stops that
    auto in = tones ({{60.0, -1.0}}, 1.0);
    const size_t a = 24000, b = 48000;
    auto make = [&] (int post, double outDb) {
        auto e = engine (false);
        e->setParam (kDrive, 12.0);
        e->setParam (kColorOn, 1.0);
        e->setParam (kColorLo, -1.0);
        e->setParam (kPostClip, post);
        e->setParam (kOutput, outDb);
        return run (*e, in);
    };
    CHECK (peakOf (make (kPostOff, 0.0).l, a, b) > 1.5, "hot without post clip: %f", peakOf (make (kPostOff, 0.0).l, a, b));
    CHECK (peakOf (make (kPostHard, 0.0).l, a, b) <= 1.0 + 1e-6, "hard clip ceiling");
    CHECK (peakOf (make (kPostSoft, 0.0).l, a, b) <= 1.0 + 1e-6, "soft clip ceiling");
    const double p6 = peakOf (make (kPostHard, -6.0).l, a, b);
    CHECK (std::fabs (p6 - 0.5012) < 0.003, "never above the Output level: %f", p6);
}

TEST (color_shapes_where_the_saturation_happens)
{
    // with no saturation the colour filters cancel exactly (quiet enough to stay linear after +18 dB)
    auto in = tones ({{60.0, -40.0}, {2000.0, -40.0}}, 1.0);
    for (bool hq : {false, true})
    {
        auto e = engine (hq);
        e->setParam (kColorOn, 1.0);
        e->setParam (kColorLo, 0.75);
        e->setParam (kColorHi, -0.75);
        e->setParam (kColorFreq, 2000.0);
        e->setParam (kColorWidth, 0.5);
        auto out = run (*e, in);
        const double err = delayedError (out.l, in.l, (size_t)e->latency (), 24000);
        CHECK (err < 1e-4, "hq %d: colour cancels: %g", hq, err);
    }
    // negative Amt Lo keeps a driven bass tone clean (the harmonic relative to the fundamental drops)
    auto bass = tones ({{60.0, -1.0}}, 1.0);
    const size_t a = 24000, b = 48000;
    auto h3 = [&] (double amtLo) {
        auto e = engine (false);
        e->setParam (kDrive, 12.0);
        e->setParam (kColorOn, 1.0);
        e->setParam (kColorLo, amtLo);
        auto out = run (*e, bass);
        return toneDb (out.l, 180.0, a, b) - toneDb (out.l, 60.0, a, b);
    };
    CHECK (h3 (-1.0) < h3 (0.0) - 15.0, "Amt Lo -100 %% cleans the bass: %f vs %f", h3 (-1.0), h3 (0.0));
    CHECK (h3 (0.5) > h3 (0.0) + 0.5, "Amt Lo +50 %% saturates it more: %f vs %f", h3 (0.5), h3 (0.0));
}

TEST (oversampling_reduces_aliasing)
{
    // a saturated 10 kHz tone: its 5th harmonic (50 kHz) aliases to 2 kHz at 48 kHz (at 2x, where 96 kHz
    // folds it to 46 kHz, the downsampler takes it out; the 7th, 70 kHz, folds to 26 kHz there and then
    // to 22 kHz, and the 9th, 90 kHz, to 6 kHz: 2x leaves the high harmonics' aliases that 4x clears)
    auto in = tones ({{10000.0, -6.0}}, 1.0);
    const size_t a = 24000, b = 48000;
    // the energy of everything that is not the tone or a harmonic below Nyquist (the aliases), relative
    // to the tone, in dB
    auto aliasDb = [&] (int mode) {
        auto e = engineOs (mode);
        e->setParam (kDrive, 18.0);
        auto out = run (*e, in);
        double rest = 0.0;
        for (double f = 500.0; f < 23000.0; f += 500.0)
            if (std::fmod (f, 10000.0) != 0.0)
                rest += std::pow (10.0, toneDb (out.l, f, a, b) / 10.0);
        return 10.0 * std::log10 (rest + 1e-30) - toneDb (out.l, 10000.0, a, b);
    };
    const double off = aliasDb (kOsOff), x2 = aliasDb (kOs2x), x4 = aliasDb (kOs4x);
    std::printf ("    alias energy against the tone: Off %.1f, 2x %.1f, 4x %.1f dB\n", off, x2, x4);
    CHECK (off > -50.0, "aliasing without oversampling: %f dB", off);
    CHECK (x2 < off - 10.0, "2x suppresses it: %f vs %f dB", x2, off);
    CHECK (x4 < x2 - 6.0, "4x the most: %f vs %f dB", x4, x2);
    // the 5th harmonic's alias alone, as before: 4x takes it at least 20 dB below no oversampling
    auto alias2k = [&] (int mode) {
        auto e = engineOs (mode);
        e->setParam (kDrive, 12.0);
        return toneDb (run (*e, in).l, 2000.0, a, b);
    };
    CHECK (alias2k (kOs4x) < alias2k (kOsOff) - 20.0, "4x suppresses the 2 kHz alias: %f vs %f dB", alias2k (kOs4x), alias2k (kOsOff));
}

TEST (dry_wet_and_dc_filter)
{
    auto in = tones ({{100.0, -6.0}}, 1.0, 0.4);
    auto e = engine (false);
    e->setParam (kDrive, 24.0);
    e->setParam (kDryWet, 0.0);
    e->reset ();
    auto out = run (*e, in);
    CHECK (delayedError (out.l, in.l, (size_t)e->latency (), 4800) < 1e-6, "dry");
    e = engine (false);
    e->setParam (kDrive, -12.0); // stays linear: the offset passes
    e->setParam (kPreLimit, 0.0);  // (the pre-limiter would hold the offset tone down)
    out = run (*e, in);
    CHECK (std::fabs (meanOf (out.l, 24000, 48000) - 0.1) < 0.01, "offset passes without the filter: %f", meanOf (out.l, 24000, 48000));
    e->setParam (kDcFilter, 1.0);
    e->reset ();
    out = run (*e, in);
    CHECK (std::fabs (meanOf (out.l, 24000, 48000)) < 0.01, "DC filter removes it: %f", meanOf (out.l, 24000, 48000));
}

TEST (mid_side_keeps_the_width_when_driven)
{
    // a loud mid (220 Hz) and a quiet side (3.3 kHz, opposite in the two channels), driven hard: left /
    // right saturation squashes the side along with the mid, mid / side saturation keeps it
    const size_t n = 48000;
    Sig in;
    in.l.resize (n);
    in.r.resize (n);
    for (size_t i = 0; i < n; ++i)
    {
        const double t = (double)i / kSr, m = 0.7 * std::sin (2.0 * M_PI * 220.0 * t), s = 0.12 * std::sin (2.0 * M_PI * 3300.0 * t);
        in.l[i] = (float)(m + s);
        in.r[i] = (float)(m - s);
    }
    auto sideToMid = [&] (bool ms) {
        auto e = engine ();
        e->setParam (kDrive, 24.0);
        e->setParam (kMidSide, ms ? 1.0 : 0.0);
        auto out = run (*e, in);
        std::vector<float> mid (n), side (n);
        for (size_t i = 0; i < n; ++i)
        {
            mid[i] = 0.5f * (out.l[i] + out.r[i]);
            side[i] = 0.5f * (out.l[i] - out.r[i]);
        }
        return toneDb (side, 3300.0, 24000, n) - toneDb (mid, 220.0, 24000, n);
    };
    const double lr = sideToMid (false), ms = sideToMid (true);
    CHECK (ms > lr + 3.0, "Mid/Side keeps the side: %.1f dB vs %.1f dB (left / right)", ms, lr);
}

TEST (clarity_keeps_the_low_mids_clean)
{
    // a bass at 80 Hz and a note at 320 Hz, driven 14 dB: with Clarity the 320 Hz note is lower after
    // the curve and there is less intermodulation between them; gently driven
    // Clarity leaves it alone
    auto in = tones ({{80.0, -8.0}, {320.0, -12.0}}, 1.0);
    auto measure = [&] (double drive, bool clarity, double f) {
        auto e = engine ();
        e->setParam (kDrive, drive);
        e->setParam (kClarity, clarity ? 1.0 : 0.0);
        auto out = run (*e, in);
        return toneDb (out.l, f, 24000, 48000);
    };
    CHECK (measure (14.0, true, 320.0) < measure (14.0, false, 320.0) - 3.5, "pushed: the low mids come down: %.1f vs %.1f dB",
           measure (14.0, true, 320.0), measure (14.0, false, 320.0));
    {
        // the meter shows the cut
        Meters m;
        auto e = engine ();
        e->setMeters (&m);
        e->setParam (kDrive, 14.0);
        e->setParam (kClarity, 1.0);
        run (*e, in);
        CHECK (m.clarityDb.load () < -3.0 && m.clarityDb.load () >= -8.0, "Clarity meter %.1f dB", m.clarityDb.load ());
    }
    // the curve is symmetric, so its intermodulation is odd-order: 320 -+ 2 x 80 = 160 and 480 Hz
    for (double f : {160.0, 480.0})
    {
        std::printf ("    %.0f Hz: %.1f dB with Clarity, %.1f dB without\n", f, measure (14.0, true, f), measure (14.0, false, f));
        CHECK (measure (14.0, true, f) < measure (14.0, false, f) - 2.0, "less intermodulation at %.0f Hz: %.1f vs %.1f dB", f,
               measure (14.0, true, f), measure (14.0, false, f));
    }
    CHECK (std::fabs (measure (-12.0, true, 320.0) - measure (-12.0, false, 320.0)) < 0.5, "gentle: untouched (%.2f vs %.2f dB)",
           measure (-12.0, true, 320.0), measure (-12.0, false, 320.0));
}

TEST (clarity_band_shape_and_moves)
{
    // the band: 0 dB at its peak, 12 dB/oct below its low edge, 6 dB/oct above its high edge
    const ClarityBand band = clarityBand (48000.0, 250.0, 2.0);
    double peak = -100.0;
    for (double hz = 50.0; hz < 2000.0; hz *= 1.02)
        peak = std::max (peak, clarityBandDb (band, hz, 48000.0));
    CHECK (std::fabs (peak) < 0.1, "peak %.2f dB", peak);
    const double lowSlope = clarityBandDb (band, band.lowHz / 4, 48000.0) - clarityBandDb (band, band.lowHz / 8, 48000.0);
    const double highSlope = clarityBandDb (band, band.highHz * 4, 48000.0) - clarityBandDb (band, band.highHz * 8, 48000.0);
    CHECK (std::fabs (lowSlope - 12.0) < 1.0 && std::fabs (highSlope - 6.0) < 1.0, "slopes %.1f / %.1f dB per octave", lowSlope,
           highSlope);
    CHECK (std::fabs (band.lowHz - 125.0) < 0.5 && std::fabs (band.highHz - 500.0) < 0.5, "2 octaves around 250 Hz: %.0f - %.0f Hz",
           band.lowHz, band.highHz);

    // moved down to 60 Hz, Clarity leaves a 320 Hz note alone and takes the 80 Hz bass down instead
    auto in = tones ({{80.0, -8.0}, {320.0, -12.0}}, 1.0);
    auto measure = [&] (bool clarity, double center, double f) {
        auto e = engine ();
        e->setParam (kDrive, 14.0);
        e->setParam (kClarity, clarity ? 1.0 : 0.0);
        e->setParam (kClarityFreq, center);
        e->setParam (kClarityWidth, 1.0);
        auto out = run (*e, in);
        return toneDb (out.l, f, 24000, 48000);
    };
    const double bassCut = measure (false, 60.0, 80.0) - measure (true, 60.0, 80.0);
    const double noteCut = measure (false, 60.0, 320.0) - measure (true, 60.0, 320.0);
    std::printf ("    band at 60 Hz: 80 Hz down %.1f dB, 320 Hz down %.1f dB\n", bassCut, noteCut);
    CHECK (bassCut > 2.0 && bassCut > noteCut + 1.5, "the band follows its centre: %.1f vs %.1f dB", bassCut, noteCut);
}

TEST (clarity_range_limits_the_cut)
{
    // a band pushed far over: Range caps the cut (0 dB: none), 8 dB by default
    auto in = tones ({{80.0, -2.0}, {250.0, -2.0}}, 0.5);
    auto cutWith = [&] (double range) {
        Meters m;
        auto e = engine ();
        e->setMeters (&m);
        e->setParam (kDrive, 24.0);
        e->setParam (kClarity, 1.0);
        e->setParam (kClarityRange, range);
        run (*e, in);
        return (double)m.clarityDb.load ();
    };
    CHECK (std::fabs (cutWith (0.0)) < 1e-6, "Range 0: no cut (%.2f dB)", cutWith (0.0));
    CHECK (std::fabs (cutWith (8.0) + 8.0) < 0.01, "held at 8 dB: %.2f", cutWith (8.0));
    CHECK (cutWith (24.0) < -12.0 && cutWith (24.0) >= -24.0, "Range 24 lets it go further: %.2f", cutWith (24.0));
    CHECK (paramTable ().info (kClarityRange).def == 8.0 && paramTable ().info (kClarityRange).max == 24.0, "8 dB by default, up to 24");
}

TEST (clarity_full_range)
{
    // Clarity Frequency covers 20 Hz - 20 kHz; a value saved with the old 20 - 500 Hz range keeps its Hz
    const auto& t = paramTable ();
    CHECK (t.info (kClarityFreq).min == 20.0 && t.info (kClarityFreq).max == 20000.0 && t.info (kClarityFreq).def == 250.0,
           "20 Hz - 20 kHz, 250 Hz by default");
    for (double hz : {20.0, 80.0, 250.0, 500.0})
    {
        const double oldNorm = std::log (hz / 20.0) / std::log (25.0);
        const double now = toPlain (kClarityFreq, clarityFreqFromNarrowRange (oldNorm));
        CHECK (std::fabs (now - hz) < 0.01 * hz, "%.0f Hz saved before -> %.1f Hz", hz, now);
    }
    // the band works up high too
    auto in = tones ({{4000.0, -6.0}, {200.0, -12.0}}, 0.5);
    auto at = [&] (bool clarity, double f) {
        auto e = engine ();
        e->setParam (kDrive, 18.0);
        e->setParam (kClarity, clarity ? 1.0 : 0.0);
        e->setParam (kClarityFreq, 4000.0);
        e->setParam (kClarityWidth, 1.0);
        auto out = run (*e, in);
        return toneDb (out.l, f, 12000, 24000);
    };
    CHECK (at (true, 4000.0) < at (false, 4000.0) - 2.0, "a 4 kHz band comes down: %.1f vs %.1f dB", at (true, 4000.0),
           at (false, 4000.0));
}

TEST (clarity_second_band)
{
    // a low-mid and a harsh upper tone, driven hard: band 1 at 250 Hz and band 2 at 3 kHz each take
    // their own tone down; band 2 alone (band 1's Range at 0) leaves the low mids and reports its own cut
    auto in = tones ({{250.0, -6.0}, {3000.0, -6.0}}, 0.5);
    auto run2 = [&] (bool b1, bool b2, Meters* m) {
        auto e = engine ();
        if (m)
            e->setMeters (m);
        e->setParam (kDrive, 18.0);
        e->setParam (kClarity, b1 || b2 ? 1.0 : 0.0); // one button; a band works while its Range is above 0
        e->setParam (kClarityRange, b1 ? 8.0 : 0.0);
        e->setParam (kClarityWidth, 1.0);
        e->setParam (kClarity2Range, b2 ? 8.0 : 0.0);
        e->setParam (kClarity2Freq, 3000.0);
        e->setParam (kClarity2Width, 1.0);
        return run (*e, in);
    };
    const auto none = run2 (false, false, nullptr), both = run2 (true, true, nullptr);
    Meters m2;
    const auto only2 = run2 (false, true, &m2);
    auto db = [] (const Sig& s, double f) { return toneDb (s.l, f, 12000, 24000); };
    std::printf ("    250 Hz: %.1f -> %.1f dB, 3 kHz: %.1f -> %.1f dB (band 2 alone: %.1f / %.1f)\n", db (none, 250.0),
                 db (both, 250.0), db (none, 3000.0), db (both, 3000.0), db (only2, 250.0), db (only2, 3000.0));
    CHECK (db (both, 250.0) < db (none, 250.0) - 2.0 && db (both, 3000.0) < db (none, 3000.0) - 2.0, "both bands cut");
    CHECK (db (only2, 3000.0) < db (none, 3000.0) - 2.0 && db (only2, 250.0) > db (none, 250.0) - 1.0,
           "band 2 alone: its own band only");
    CHECK (m2.clarity2Db.load () < -1.0 && m2.clarityDb.load () == 0.0f, "band 2's meter: %.1f dB", m2.clarity2Db.load ());
    CHECK (paramTable ().info (kClarity2Range).def == 0.0 && paramTable ().info (kClarity2Freq).def == 3000.0,
           "band 2 does nothing by default (Range 0), at 3 kHz");
    // states from before one Clarity button mean the same
    {
        double on1 = 0.0, r1 = 1.0 / 3.0, r2 = 1.0 / 3.0;
        clarityToOneButton (on1, r1, 1.0, r2); // band 2 was on on its own
        CHECK (on1 == 1.0 && r1 == 0.0 && r2 == 1.0 / 3.0, "band 2 alone: Clarity on, band 1's Range 0");
        on1 = 1.0, r1 = 1.0 / 3.0, r2 = 1.0 / 3.0;
        clarityToOneButton (on1, r1, 0.0, r2); // band 2 was off
        CHECK (on1 == 1.0 && r1 == 1.0 / 3.0 && r2 == 0.0, "band 2 off: its Range 0");
    }
}

TEST (sub_and_high_without_buttons)
{
    // the Sub and High bands have no button: they work while Gentlr is on and their Range is above 0 dB,
    // and start at 0 (no cut), as band 2
    const auto& t = paramTable ();
    CHECK (t.info (kClaritySubRange).def == 0.0 && t.info (kClarityHighRange).def == 0.0, "Sub and High Range: 0 dB by default");
    CHECK (!claritySubOn (1.0, 0.0) && claritySubOn (1.0, 0.5) && !claritySubOn (0.0, 8.0) && !clarityHighOn (1.0, 0.0) &&
               clarityHighOn (1.0, 6.0) && !clarityHighOn (0.0, 6.0),
           "a band works while Gentlr is on and its Range is above 0 dB");
    std::vector<pk::ParamInfo> v2, v3;
    addTailExt2Params (v2, 0);
    addTailExt3Params (v3, 0);
    CHECK (v2[pk::kTailExt2SubRange].def == 0.0 && v3[pk::kTailExt3HighRange].def == 0.0, "and in every plug-in's end saturator");

    // a state from before (normalized values): off -> Range 0; on -> its Range, or the old default
    // (8 dB for Sub, 6 dB for High) when the state has none
    const double sub8 = toNormalized (kClaritySubRange, 8.0), high6 = toNormalized (kClarityHighRange, 6.0);
    CHECK (std::fabs (sub8 - 8.0 / 24.0) < 1e-12 && std::fabs (high6 - 6.0 / 24.0) < 1e-12 && kSubRangeBeforeDb == 8.0 &&
               kHighRangeBeforeDb == 6.0,
           "the old defaults");
    struct Case
    {
        double on, range;
        bool hasRange;
        double want;
    };
    for (const Case& c : {Case {0.0, 0.5, true, 0.0}, Case {0.0, 0.0, false, 0.0}, Case {1.0, 0.5, true, 0.5}, Case {1.0, 0.0, false, sub8},
                          Case {1.0, 0.0, true, 0.0}})
    {
        double r = c.range;
        subHighToRange (c.on, r, c.hasRange, sub8);
        CHECK (r == c.want, "on %.0f, Range %.2f (%s): %.4f, want %.4f", c.on, c.range, c.hasRange ? "saved" : "missing", r, c.want);
    }
    // both bands by their IDs (here Smacheratr's own); a missing button was off
    {
        std::array<double, kNumParams> norm {};
        std::array<bool, kNumParams> has {};
        norm[kClaritySub] = 1.0, has[kClaritySub] = true; // Sub on, its Range not saved: the old 8 dB
        norm[kClarityHighRange] = 0.75, has[kClarityHighRange] = true; // High's button not saved (off), a Range
        subHighStateToRange (norm, has, kClaritySub, kClaritySubRange, kClarityHigh, kClarityHighRange);
        CHECK (norm[kClaritySubRange] == sub8 && norm[kClarityHighRange] == 0.0, "Sub on: 8 dB; High (off): 0 dB");
        norm[kClarityHigh] = 1.0, has[kClarityHigh] = true;
        norm[kClarityHighRange] = 0.75;
        has[kClarityHighRange] = false; // (a High band on with no Range: the old 6 dB)
        subHighStateToRange (norm, has, kClaritySub, kClaritySubRange, kClarityHigh, kClarityHighRange);
        CHECK (norm[kClarityHighRange] == high6, "High on, its Range missing: 6 dB");
    }
    // a plug-in's end saturator (its third block at 100, its fourth at 200): the Ranges marked present
    {
        std::array<double, 300> norm {};
        std::array<bool, 300> has {};
        norm[100 + pk::kTailExt2Sub] = 1.0, has[100 + pk::kTailExt2Sub] = true;
        norm[100 + pk::kTailExt2SubRange] = 0.5, has[100 + pk::kTailExt2SubRange] = true;
        norm[200 + pk::kTailExt3High] = 0.0, has[200 + pk::kTailExt3High] = true;
        norm[200 + pk::kTailExt3HighRange] = 0.5, has[200 + pk::kTailExt3HighRange] = true;
        tailSubHighToRange (norm, has, 100, 200);
        CHECK (norm[100 + pk::kTailExt2SubRange] == 0.5 && norm[200 + pk::kTailExt3HighRange] == 0.0 && has[100 + pk::kTailExt2SubRange] &&
                   has[200 + pk::kTailExt3HighRange],
               "the tail's Sub (on) keeps its Range, High (off) gets 0");
        std::array<double, 300> n2 {};
        std::array<bool, 300> h2 {};
        tailSubHighToRange (n2, h2, 100, 200); // (a state from before the blocks: both off)
        CHECK (n2[100 + pk::kTailExt2SubRange] == 0.0 && n2[200 + pk::kTailExt3HighRange] == 0.0 && h2[100 + pk::kTailExt2SubRange],
               "a state without the blocks: both at 0");
    }
}

TEST (tail_has_every_control)
{
    // the saturator at the end of the other plug-ins: its extended fields reach Smacheratr (here Clarity
    // on a hard-driven bass and low-mid note turns the low mids down)
    auto in = tones ({{80.0, -8.0}, {320.0, -12.0}}, 1.0);
    auto render = [&] (bool clarity) {
        Tail t;
        t.prepare (48000.0, 512);
        t.setParam (pk::kTailOn, 1.0);
        t.setParam (pk::kTailMix, 1.0);
        t.setParam (pk::kTailDrive, 14.0);
        t.setParam (pk::kTailFields + pk::kTailExtClarity, clarity ? 1.0 : 0.0);
        Sig out = in;
        for (size_t pos = 0; pos < out.l.size (); pos += 512)
        {
            const int n = (int)std::min<size_t> (512, out.l.size () - pos);
            t.process (out.l.data () + pos, out.r.data () + pos, n);
        }
        return toneDb (out.l, 320.0, 24000, 48000);
    };
    const double off = render (false), on = render (true);
    CHECK (on < off - 3.0, "Clarity through the tail: %.1f vs %.1f dB", on, off);
    // the parameters match Smacheratr's, with Saturator in front of the names
    std::vector<pk::ParamInfo> v;
    addTailExtParams (v, 100);
    CHECK (v.size () == pk::kTailExtFields && v[pk::kTailExtClarityFreq].id == 100 + pk::kTailExtClarityFreq &&
               std::string (v[pk::kTailExtClarityFreq].name) == "Saturator Gentlr Frequency" &&
               v[pk::kTailExtClarityFreq].def == 250.0 && v[pk::kTailExtColorOn].def == 0.0,
           "extended tail parameters");
    CHECK (tailFieldOf (kClarityWidth) == (int)(pk::kTailFields + pk::kTailExtClarityWidth) && tailFieldOf (kDryWet) == pk::kTailMix,
           "Smacheratr IDs to tail fields");
    // the third block: Gentlr's Advanced mode
    std::vector<pk::ParamInfo> v2;
    addTailExt2Params (v2, 200);
    CHECK (v2.size () == pk::kTailExt2Fields && v2[pk::kTailExt2Threshold].id == 200 + pk::kTailExt2Threshold &&
               std::string (v2[pk::kTailExt2Advanced].name) == "Saturator Gentlr Advanced" &&
               std::string (v2[pk::kTailExt2Threshold2].name) == "Saturator Gentlr 2 Threshold" &&
               v2[pk::kTailExt2Threshold].def == -18.0 && v2[pk::kTailExt2Advanced].def == 0.0 && v2[pk::kTailExt2Drive].def == 0.0,
           "Gentlr's Advanced block");
    const uint32_t ext2Field = pk::kTailFields + pk::kTailExtFields;
    CHECK (tailFieldOf (kClarity2Threshold) == (int)(ext2Field + pk::kTailExt2Threshold2) &&
               tailFieldOf (kClarityDriveAmount) == (int)(ext2Field + pk::kTailExt2DriveAmount),
           "its Smacheratr IDs to tail fields");
    const TailBases bases {10, 50, 90, 130};
    CHECK (tailParamOf (pk::kTailDrive, bases) == 10 + pk::kTailDrive &&
               tailParamOf (pk::kTailFields + pk::kTailExtColorLo, bases) == 50 + pk::kTailExtColorLo &&
               tailParamOf (ext2Field + pk::kTailExt2Drive, bases) == 90 + pk::kTailExt2Drive &&
               tailParamOf (kTailExt3First + pk::kTailExt3NoOverlap, bases) == 130 + pk::kTailExt3NoOverlap,
           "tail fields to a plug-in's IDs");
    for (uint32_t f = 0; f < kTailAllFields; ++f)
        CHECK (tailFieldIn (tailParamOf (f, bases), bases) == (int)f, "tail field %u round trip", f);
    CHECK (tailFieldIn (9, bases) == -1 && tailFieldIn (130 + pk::kTailExt3Fields, bases) == -1, "not a tail parameter");
    for (uint32_t id = 0; id < kNumParams; ++id)
    {
        const int f = tailFieldOf (id);
        const bool own = id == kClarity2; // (unused) and the tail's Mix is Dry/Wet: everything else has a field
        CHECK (f >= 0 || own, "Smacheratr %u (%s) has a tail field", id, paramTable ().info (id).name);
    }
    // through the tail: Advanced with a low Threshold cuts where the plain Gentlr (-18 dB) would not
    auto renderT = [&] (bool advanced) {
        Tail t;
        t.prepare (48000.0, 512);
        t.setParam (pk::kTailOn, 1.0);
        t.setParam (pk::kTailMix, 1.0);
        t.setParam (pk::kTailPreLimit, 0.0);
        t.setParam (pk::kTailFields + pk::kTailExtClarity, 1.0);
        t.setParam (ext2Field + pk::kTailExt2Advanced, advanced ? 1.0 : 0.0);
        t.setParam (ext2Field + pk::kTailExt2Threshold, -48.0);
        Sig out = tones ({{250.0, -30.0}}, 0.5);
        for (size_t pos = 0; pos < out.l.size (); pos += 512)
            t.process (out.l.data () + pos, out.r.data () + pos, (int)std::min<size_t> (512, out.l.size () - pos));
        return toneDb (out.l, 250.0, 12000, 24000);
    };
    CHECK (renderT (true) < renderT (false) - 4.0, "Advanced through the tail: %.1f vs %.1f dB", renderT (true), renderT (false));
}

// ---------------------------------------------------------------------------
// Gentlr's Advanced mode (Clarity is called Gentlr now; the IDs keep the old names)

TEST (gentlr_advanced_off_is_gentlr_as_before)
{
    // Advanced off: the Thresholds and the region Drive do nothing, to the bit; and Advanced on with
    // both Thresholds at -18 dB (their default) and the Drive off is the same Gentlr too
    auto in = tones ({{80.0, -8.0}, {320.0, -12.0}, {3000.0, -10.0}}, 0.5);
    auto render = [&] (bool hq, bool advanced, double thr1, double thr2, bool drive) {
        auto e = engine (hq);
        e->setParam (kDrive, 14.0);
        e->setParam (kClarity, 1.0);
        e->setParam (kClarity2Range, 10.0);
        e->setParam (kClarityAdvanced, advanced ? 1.0 : 0.0);
        e->setParam (kClarityThreshold, thr1);
        e->setParam (kClarity2Threshold, thr2);
        e->setParam (kClarityDrive, drive ? 1.0 : 0.0);
        e->setParam (kClarityDriveAmount, 30.0);
        return run (*e, in, 333);
    };
    for (bool hq : {true, false})
    {
        const Sig plain = render (hq, false, -18.0, -18.0, false);
        auto same = [&] (const Sig& s) {
            return std::memcmp (s.l.data (), plain.l.data (), s.l.size () * sizeof (float)) == 0 &&
                   std::memcmp (s.r.data (), plain.r.data (), s.r.size () * sizeof (float)) == 0;
        };
        CHECK (same (render (hq, false, -50.0, -3.0, true)), "hq %d: Advanced off ignores the Thresholds and the Drive", hq);
        CHECK (same (render (hq, true, -18.0, -18.0, false)), "hq %d: Advanced at -18 dB is the plain Gentlr", hq);
        CHECK (!same (render (hq, true, -40.0, -18.0, false)), "hq %d: a Threshold does something with Advanced on", hq);
    }
    const auto& t = paramTable ();
    CHECK (t.info (kClarityAdvanced).def == 0.0 && t.info (kClarityThreshold).def == kClarityThresholdDb &&
               t.info (kClarity2Threshold).def == kClarityThresholdDb && t.info (kClarityDrive).def == 0.0 &&
               t.info (kClarityDriveAmount).def == 12.0,
           "defaults: Advanced off, Thresholds -18 dB, Drive off at 12 dB");
    CHECK (t.info (kClarityThreshold).min == -60.0 && t.info (kClarityThreshold).max == 0.0, "Threshold -60 .. 0 dB");
    CHECK (std::string (t.info (kClarity).name) == "Gentlr" && std::string (t.info (kClarityThreshold).name) == "Gentlr Threshold",
           "called Gentlr");
}

TEST (gentlr_threshold)
{
    // a 250 Hz tone in band 1 at -12 dB (Drive 0): its level as Gentlr measures it is about -10 dB (the
    // detector rides its peaks: fast up, slow down); below the Threshold no cut, 3 dB for every 5 over
    // it, and the Range once it is far enough over
    auto in = tones ({{250.0, -12.0}}, 1.0);
    auto measure = [&] (bool advanced, double threshold, float* level = nullptr) {
        Meters m;
        auto e = engine ();
        e->setMeters (&m);
        e->setParam (kPreLimit, 0.0);
        e->setParam (kClarity, 1.0);
        e->setParam (kClarityRange, 12.0);
        e->setParam (kClarityAdvanced, advanced ? 1.0 : 0.0);
        e->setParam (kClarityThreshold, threshold);
        run (*e, in);
        if (level)
            *level = m.clarityLevelDb.load ();
        return (double)m.clarityDb.load ();
    };
    float level = 0.0f;
    const double above = measure (true, -6.0, &level);
    std::printf ("    level %.1f dB; cut at Threshold -6: %.2f, -22: %.2f, -40: %.2f dB; without Advanced (-18): %.2f dB\n", level,
                 above, measure (true, -22.0), measure (true, -40.0), measure (false, -40.0));
    CHECK (level > -12.5f && level < -8.5f, "the band's level: %.1f dB", level);
    CHECK (above == 0.0, "under the Threshold: no cut (%.2f dB)", above);
    CHECK (std::fabs (measure (true, -22.0) + 0.6 * (level + 22.0)) < 0.5, "over it: 3 dB for every 5 (%.2f at %.1f dB over)",
           measure (true, -22.0), level + 22.0);
    CHECK (std::fabs (measure (true, -40.0) + 12.0) < 0.01, "far over: the Range (%.2f)", measure (true, -40.0));
    CHECK (std::fabs (measure (false, -40.0) + 0.6 * (level + 18.0)) < 0.5, "without Advanced it starts at -18 dB (%.2f)",
           measure (false, -40.0));
    // the law itself
    CHECK (clarityCutDb (-20.0, -18.0, 8.0) == 0.0 && std::fabs (clarityCutDb (-13.0, -18.0, 8.0) - 3.0) < 1e-12 &&
               clarityCutDb (10.0, -18.0, 8.0) == 8.0,
           "3 dB for every 5 over, up to the Range");
    // the band's level on the meter goes away with the band
    Meters m;
    auto e = engine ();
    e->setMeters (&m);
    e->setParam (kClarity, 0.0);
    run (*e, in);
    CHECK (m.clarityLevelDb.load () == -120.0f && m.clarity2LevelDb.load () == -120.0f, "no level while the band is off");
}

TEST (gentlr_region_drive)
{
    // a tone in band 1 (250 Hz) and one far above it (5.1 kHz), both quiet enough for the curve to
    // leave them alone (Drive 0): the region Drive gives the band harmonics and hardly touches the other
    auto in = tones ({{250.0, -14.0}, {5100.0, -14.0}}, 1.0);
    auto render = [&] (bool drive, bool hq = true) {
        auto e = engine (hq);
        e->setParam (kPreLimit, 0.0);
        e->setParam (kClarity, 1.0);
        e->setParam (kClarityWidth, 1.0);
        e->setParam (kClarityAdvanced, 1.0);
        e->setParam (kClarityThreshold, -40.0);
        e->setParam (kClarityDrive, drive ? 1.0 : 0.0);
        e->setParam (kClarityDriveAmount, 30.0);
        return run (*e, in);
    };
    const Sig off = render (false), on = render (true);
    auto db = [] (const Sig& s, double f) { return toneDb (s.l, f, 24000, 48000); };
    std::printf ("    750 Hz (3rd harmonic): %.1f -> %.1f dB, 5.1 kHz: %.2f -> %.2f dB, 250 Hz: %.2f -> %.2f dB\n", db (off, 750.0),
                 db (on, 750.0), db (off, 5100.0), db (on, 5100.0), db (off, 250.0), db (on, 250.0));
    CHECK (db (on, 750.0) > db (off, 750.0) + 20.0 && db (on, 750.0) > -50.0, "harmonics in the band's region: %.1f vs %.1f dB",
           db (on, 750.0), db (off, 750.0));
    CHECK (std::fabs (db (on, 5100.0) - db (off, 5100.0)) < 0.5, "the tone outside the band: %.2f vs %.2f dB", db (on, 5100.0),
           db (off, 5100.0));
    // oversampled or not, the region lines up with the rest: 4x and Off give nearly the same output, Off's
    // earlier by the oversampler's latency
    const Sig onLow = render (true, false);
    const size_t shift = (size_t)(engine (true)->latency () - engine (false)->latency ());
    double diff = 0.0, sum = 0.0;
    for (size_t i = 24000; i < on.l.size (); ++i)
    {
        diff += (on.l[i] - onLow.l[i - shift]) * (on.l[i] - onLow.l[i - shift]);
        sum += on.l[i] * on.l[i];
    }
    CHECK (std::sqrt (diff / sum) < 0.05, "4x vs Off with the region driven: %.3f", std::sqrt (diff / sum));
}

TEST (gentlr_region_drive_keeps_the_latency_and_does_not_click)
{
    // a quiet band passes the region Drive untouched (the curve is linear there), and the latency stays
    auto quiet = tones ({{60.0, -40.0}, {250.0, -40.0}, {1000.0, -40.0}, {9000.0, -46.0}}, 0.5);
    for (bool hq : {true, false})
    {
        auto render = [&] (bool drive, int* latency) {
            auto e = engine (hq);
            e->setParam (kPreLimit, 0.0);
            e->setParam (kClarity, 1.0);
            e->setParam (kClarityAdvanced, 1.0);
            e->setParam (kClarityThreshold, -60.0);
            e->setParam (kClarityDrive, drive ? 1.0 : 0.0);
            e->setParam (kClarityDriveAmount, 12.0);
            auto out = run (*e, quiet, 333);
            *latency = e->latency ();
            return out;
        };
        int latOff = 0, latOn = 0;
        const Sig off = render (false, &latOff), on = render (true, &latOn);
        CHECK (latOn == latOff && latOn == engine (hq)->latency (), "hq %d: the latency stays %d (%d)", hq, latOff, latOn);
        double err = 0.0;
        for (size_t i = 4800; i < on.l.size (); ++i)
            err = std::max (err, (double)std::fabs (on.l[i] - off.l[i]));
        CHECK (err < 1e-6, "hq %d: quiet through the region Drive: %g", hq, err);
    }
    // switching the Drive (or Advanced) on and off while a loud band plays fades it in and out
    auto in = tones ({{250.0, -10.0}}, 1.5);
    auto maxStep = [] (const Sig& s, size_t a, size_t b) {
        double m = 0.0;
        for (size_t i = std::max<size_t> (a, 1); i < b; ++i)
            m = std::max (m, (double)std::fabs (s.l[i] - s.l[i - 1]));
        return m;
    };
    for (uint32_t sw : {kClarityDrive, kClarityAdvanced})
    {
        auto e = engine ();
        e->setParam (kPreLimit, 0.0);
        e->setParam (kClarity, 1.0);
        e->setParam (kClarityAdvanced, 1.0);
        e->setParam (kClarityThreshold, -40.0);
        e->setParam (kClarityDrive, 1.0);
        e->setParam (kClarityDriveAmount, 36.0);
        Sig out;
        out.l.resize (in.l.size ());
        out.r.resize (in.r.size ());
        for (size_t pos = 0; pos < in.l.size (); pos += 256)
        {
            if (pos >= 24000 && pos < 48000)
                e->setParam (sw, 0.0);
            else
                e->setParam (sw, 1.0);
            const int n = (int)std::min<size_t> (256, in.l.size () - pos);
            e->process (in.l.data () + pos, in.r.data () + pos, out.l.data () + pos, out.r.data () + pos, n);
        }
        const double steady = std::max (maxStep (out, 12000, 23000), maxStep (out, 38000, 47000));
        const double around = std::max (maxStep (out, 23000, 30000), maxStep (out, 47000, 54000));
        std::printf ("    %s switched: largest step %.4f (steady %.4f)\n", paramTable ().info (sw).name, around, steady);
        CHECK (around < steady * 1.1 + 1e-3, "%s: no click when switched (%.4f vs %.4f in steady state)", paramTable ().info (sw).name,
               around, steady);
    }
}

TEST (fuzz_and_automation)
{
    uint32_t seed = 11;
    auto rnd = [&] {
        seed = seed * 1664525u + 1013904223u;
        return (double)((seed >> 8) & 0xFFFFFF) / 16777216.0;
    };
    for (int iter = 0; iter < 40; ++iter)
    {
        Engine e;
        e.prepare (iter % 3 == 0 ? 96000.0 : (iter % 3 == 1 ? 44100.0 : kSr), iter % 2 ? 64 : 512);
        Sig in;
        in.l.resize (30000);
        in.r.resize (30000);
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
    auto in = tones ({{55.0, -6.0}, {1000.0, -12.0}, {8000.0, -20.0}}, 10.0);
    // the best of three, so a busy CI machine does not fail it
    double secs = 1e9;
    for (int k = 0; k < 3; ++k)
    {
        auto e = engine (true);
        e->setParam (kColorOn, 1.0);
        e->setParam (kPreLimit, 1.0);
        e->setParam (kPostClip, kPostSoft);
        e->setParam (kDrive, 12.0);
        const std::clock_t t0 = std::clock (); // CPU time: other programs running do not count
        run (*e, in);
        secs = std::min (secs, (double)(std::clock () - t0) / CLOCKS_PER_SEC);
    }
    std::printf ("    CPU: %.2f%% of one core (stereo, 4x oversampling)\n", 100.0 * secs / 10.0);
    CHECK (secs / 10.0 < 0.05, "too slow");
}

// ---------------------------------------------------------------------------
// Gentlr's Sub band: the bottom of the spectrum up to where it tapers off (20 - 100 Hz)

TEST (gentlr_bands_at_the_ends_are_shelves)
{
    // a band reaching an end of the spectrum runs flat past it instead of dipping back up
    auto db = [] (const ClarityBand& b, double hz) { return clarityBandDb (b, hz, kSr); };
    const ClarityBand mid = clarityBand (kSr, 250.0, 2.0), low = clarityBand (kSr, 40.0, 2.0), high = clarityBand (kSr, 12000.0, 2.0);
    CHECK (!mid.lowShelf && !mid.highShelf && db (mid, 20.0) < -15.0 && db (mid, 10000.0) < -15.0, "a band in the middle: a bell");
    CHECK (low.lowShelf && !low.highShelf && std::fabs (db (low, 5.0)) < 0.5 && std::fabs (db (low, 20.0)) < 0.5,
           "its low edge at 20 Hz or below: a low shelf, flat to the bottom (%.2f dB at 5 Hz)", db (low, 5.0));
    CHECK (high.highShelf && !high.lowShelf && std::fabs (db (high, 20000.0)) < 0.5 && db (high, 3000.0) < -6.0,
           "its high edge at 20 kHz or above: a high shelf, flat to the top (%.2f dB at 20 kHz)", db (high, 20000.0));
    // and the engine compresses a 10 Hz rumble with the Sub band (a shelf now), which it no longer filtered out
    auto e = engine ();
    e->setParam (kPreLimit, 0.0);
    e->setParam (kDrive, 12.0);
    e->setParam (kClarity, 1.0);
    e->setParam (kClarityRange, 0.0);
    e->setParam (kClaritySubRange, 8.0);
    Meters m;
    e->setMeters (&m);
    run (*e, tones ({{10.0, -8.0}}, 1.5));
    CHECK (m.claritySubDb.load () < -3.0, "a 10 Hz rumble is in the Sub band (%.1f dB cut)", m.claritySubDb.load ());
}

TEST (gentlr_sub_band)
{
    // the band's shape: a shelf, flat from the bottom up to the slider's frequency, where its cut starts
    // to let go (as it sounds: with the band's phase)
    for (double taper : {20.0, 40.0, 100.0})
    {
        const ClarityBand b = subBand (kSr, taper);
        auto db = [&] (double hz) { return clarityBandDb (b, hz, kSr); };
        double peak = -200.0;
        for (double hz = 1.0; hz < 400.0; hz *= 1.02)
            peak = std::max (peak, db (hz));
        CHECK (peak > 0.0 && peak < 1.5, "taper %.0f Hz: a little rise under the corner, under 1.5 dB (%.2f)", taper, peak);
        auto cut = [&] (double hz) { return clarityCutAtDb (b, hz, kSr, -8.0); };
        CHECK (std::fabs (cut (1.0) + 8.0) < 0.05, "taper %.0f Hz: a shelf, the whole cut at the bottom (%.2f dB at 1 Hz)", taper, cut (1.0));
        CHECK (cut (taper) < -6.8 && cut (taper) > -9.5, "taper %.0f Hz: within about 1 dB of the cut at the taper (%.2f dB)", taper, cut (taper));
        CHECK (cut (4.0 * taper) > -1.5, "taper %.0f Hz: two octaves up, nearly none (%.2f dB)", taper, cut (4.0 * taper));
        CHECK (cut (1000.0) > -0.2, "taper %.0f Hz: 1 kHz untouched (%.2f dB)", taper, cut (1000.0));
    }
    {
        const ClarityBand b = subBand (kSr, 100.0);
        CHECK (clarityBandDb (b, 20.0, kSr) > -4.5, "it reaches down to 20 Hz (%.1f dB)", clarityBandDb (b, 20.0, kSr));
    }
    const auto& t = paramTable ();
    CHECK (t.info (kClaritySub).def == 0.0 && t.info (kClaritySubFreq).def == 40.0 && t.info (kClaritySubFreq).min == 20.0 &&
               t.info (kClaritySubFreq).max == 100.0 && t.info (kClaritySubRange).def == 0.0 && t.info (kClaritySubThreshold).def == -18.0,
           "defaults: Range 0 dB (it cuts nothing), starts to taper at 40 Hz (20 - 100 Hz), Threshold -18 dB");
    CHECK (std::string (t.info (kClaritySub).name) == "Gentlr Sub (unused)", "the Sub band's old button: unused");

    // a 40 Hz bass and a 1 kHz note, driven hard: Sub cuts the bass, not the note, and only while on
    auto in = tones ({{40.0, -8.0}, {1000.0, -14.0}}, 1.5);
    auto measure = [&] (bool gentlr, bool sub, double taper, double f, Meters* m = nullptr, double range = 8.0) {
        auto e = engine ();
        if (m)
            e->setMeters (m);
        e->setParam (kPreLimit, 0.0);
        e->setParam (kDrive, 12.0);
        e->setParam (kClarity, gentlr ? 1.0 : 0.0);
        e->setParam (kClarityRange, 0.0); // the other bands off: only Sub
        e->setParam (kClaritySubFreq, taper);
        e->setParam (kClaritySubRange, sub ? range : 0.0); // (no button: Range 0 is off)
        auto out = run (*e, in);
        return toneDb (out.l, f, 48000, 72000);
    };
    Meters m;
    const double subOn = measure (true, true, 40.0, 40.0, &m), subOff = measure (true, false, 40.0, 40.0);
    std::printf("    40 Hz: %.1f dB with Sub, %.1f dB without; Sub cut %.1f dB, level %.1f dB\n", subOn, subOff, m.claritySubDb.load (),
                m.claritySubLevelDb.load ());
    CHECK (subOn < subOff - 2.0, "Sub turns the bass down: %.1f vs %.1f dB", subOn, subOff);
    CHECK (m.claritySubDb.load () < -2.0 && m.claritySubDb.load () >= -8.0 - 1e-3, "the Sub meter shows the cut (%.1f dB, Range 8)",
           m.claritySubDb.load ());
    {
        // (the note alone: with the bass, cutting it frees headroom in the curve for the note, as any compressor does)
        auto note = tones ({{1000.0, -14.0}}, 1.5);
        auto noteDb = [&] (bool sub) {
            auto e = engine ();
            e->setParam (kPreLimit, 0.0);
            e->setParam (kDrive, 12.0);
            e->setParam (kClarity, 1.0);
            e->setParam (kClarityRange, 0.0);
            e->setParam (kClaritySubRange, sub ? 8.0 : 0.0);
            return toneDb (run (*e, note).l, 1000.0, 48000, 72000);
        };
        CHECK (noteDb (true) == noteDb (false), "the 1 kHz note is left alone: %.2f vs %.2f dB", noteDb (true), noteDb (false));
    }
    CHECK (measure (false, true, 40.0, 40.0) == subOff, "Gentlr off: Sub does nothing");
    CHECK (measure (true, true, 40.0, 40.0, nullptr, 0.0) == subOff, "Range 0 dB: Sub does nothing");
    {
        // its old button is unused: on with Range 0 nothing, off with a Range the band works
        auto withButton = [&] (double button, double range) {
            auto e = engine ();
            e->setParam (kPreLimit, 0.0);
            e->setParam (kDrive, 12.0);
            e->setParam (kClarity, 1.0);
            e->setParam (kClarityRange, 0.0);
            e->setParam (kClaritySub, button);
            e->setParam (kClaritySubRange, range);
            return toneDb (run (*e, in).l, 40.0, 48000, 72000);
        };
        CHECK (withButton (1.0, 0.0) == subOff && withButton (0.0, 8.0) == subOn, "the Sub band's old button does nothing");
    }
    CHECK (measure (true, true, 40.0, 40.0, nullptr, 3.0) > subOn + 1.0, "a smaller Range cuts less");

    // the slider moves where the taper starts: a loud 90 Hz tone is cut with the taper at 100 Hz, much
    // less with it at 25 Hz
    auto in90 = tones ({{90.0, -8.0}}, 1.5);
    auto cut90 = [&] (double taper, bool sub) {
        auto e = engine ();
        e->setParam (kPreLimit, 0.0);
        e->setParam (kDrive, 12.0);
        e->setParam (kClarity, 1.0);
        e->setParam (kClarityRange, 0.0);
        e->setParam (kClaritySubRange, sub ? 8.0 : 0.0);
        e->setParam (kClaritySubFreq, taper);
        return toneDb (run (*e, in90).l, 90.0, 48000, 72000);
    };
    const double off90 = cut90 (100.0, false), at100 = cut90 (100.0, true) - off90, at25 = cut90 (25.0, true) - off90;
    std::printf ("    90 Hz: %.1f dB with the taper at 100 Hz, %.1f dB at 25 Hz\n", at100, at25);
    CHECK (at100 < at25 - 3.0, "the slider sets what reaches the band: %.1f dB at 100 Hz, %.1f dB at 25 Hz", at100, at25);

    // Sub at Range 0 is what Gentlr was before it: bit for bit, and the bands 1 / 2 do not care about it
    auto render = [&] (bool sub, double taper, double threshold = -18.0) {
        auto e = engine ();
        e->setParam (kDrive, 14.0);
        e->setParam (kClarity, 1.0);
        e->setParam (kClarity2Range, 6.0);
        e->setParam (kClarityAdvanced, 1.0);
        e->setParam (kClaritySubRange, sub ? 8.0 : 0.0);
        e->setParam (kClaritySubFreq, taper);
        e->setParam (kClaritySubThreshold, threshold);
        return run (*e, in).l;
    };
    CHECK (render (false, 40.0) == render (false, 90.0, -50.0), "Range 0: Sub ignores its other controls, to the bit");
    CHECK (render (true, 40.0) != render (false, 40.0), "Sub with a Range changes the sound");

    // Advanced: the Sub band's Threshold
    auto cutAt = [&] (double threshold) {
        Meters mm;
        auto e = engine ();
        e->setMeters (&mm);
        e->setParam (kPreLimit, 0.0);
        e->setParam (kDrive, -6.0); // (a level of about -12 dB)
        e->setParam (kClarity, 1.0);
        e->setParam (kClarityRange, 0.0);
        e->setParam (kClaritySubRange, 8.0);
        e->setParam (kClarityAdvanced, 1.0);
        e->setParam (kClaritySubThreshold, threshold);
        run (*e, in);
        return (double)mm.claritySubDb.load ();
    };
    CHECK (cutAt (-3.0) == 0.0 && cutAt (-50.0) < -7.9, "Advanced: its Threshold sets where it cuts (%.2f / %.2f dB)", cutAt (-3.0), cutAt (-50.0));

    // the region Drive works on the Sub band too, and stays finite
    {
        auto e = engine ();
        e->setParam (kDrive, 12.0);
        e->setParam (kClarity, 1.0);
        e->setParam (kClarityRange, 0.0);
        e->setParam (kClaritySubRange, 8.0);
        e->setParam (kClarityAdvanced, 1.0);
        e->setParam (kClarityDrive, 1.0);
        e->setParam (kClarityDriveAmount, 24.0);
        auto out = run (*e, in);
        bool finite = true;
        for (float x : out.l)
            finite = finite && std::isfinite (x) && std::fabs (x) < 4.0f;
        CHECK (finite, "the region Drive on the Sub band is finite");
        CHECK (out.l != render (true, 40.0), "and does something");
    }

    // through the tail: the four fields are in the third block
    const uint32_t ext2Field = pk::kTailFields + pk::kTailExtFields;
    CHECK (tailFieldOf (kClaritySub) == (int)(ext2Field + pk::kTailExt2Sub) && tailFieldOf (kClaritySubFreq) == (int)(ext2Field + pk::kTailExt2SubFreq) &&
               tailFieldOf (kClaritySubRange) == (int)(ext2Field + pk::kTailExt2SubRange) &&
               tailFieldOf (kClaritySubThreshold) == (int)(ext2Field + pk::kTailExt2SubThreshold),
           "Sub's IDs to tail fields");
    std::vector<pk::ParamInfo> v2;
    addTailExt2Params (v2, 200);
    CHECK (std::string (v2[pk::kTailExt2Sub].name) == "Saturator Gentlr Sub (unused)" && v2[pk::kTailExt2SubFreq].def == 40.0 &&
               v2[pk::kTailExt2SubRange].def == 0.0 &&
               v2[pk::kTailExt2SubFreq].id == 200 + pk::kTailExt2SubFreq,
           "Sub in the tail's third block");
    auto renderT = [&] (bool sub) {
        Tail tail;
        tail.prepare (48000.0, 512);
        tail.setParam (pk::kTailOn, 1.0);
        tail.setParam (pk::kTailMix, 1.0);
        tail.setParam (pk::kTailPreLimit, 0.0);
        tail.setParam (pk::kTailDrive, 12.0);
        tail.setParam (pk::kTailFields + pk::kTailExtClarity, 1.0);
        tail.setParam (pk::kTailFields + pk::kTailExtClarityRange, 0.0);
        tail.setParam (ext2Field + pk::kTailExt2SubRange, sub ? 8.0 : 0.0);
        Sig out = in;
        for (size_t pos = 0; pos < out.l.size (); pos += 512)
            tail.process (out.l.data () + pos, out.r.data () + pos, (int)std::min<size_t> (512, out.l.size () - pos));
        return toneDb (out.l, 40.0, 48000, 72000);
    };
    CHECK (renderT (true) < renderT (false) - 2.0, "Sub through the tail: %.1f vs %.1f dB", renderT (true), renderT (false));
}

// ---------------------------------------------------------------------------
// Gentlr's High band: from where it tapers off (2 - 16 kHz) to the top of the spectrum, the Sub band's
// mirror

TEST (gentlr_high_band)
{
    // the band's shape: a shelf, flat from the slider's frequency up to the top, its cut letting go below
    for (double taper : {2000.0, 7000.0, 12000.0})
    {
        const ClarityBand b = highBand (kSr, taper);
        CHECK (b.highShelf && !b.lowShelf && b.lowHz == taper && b.highHz == 20000.0, "taper %.0f Hz: a shelf to the top (drawn to 20 kHz)", taper);
        double peak = -200.0;
        for (double hz = 200.0; hz < 0.5 * kSr; hz *= 1.02)
            peak = std::max (peak, clarityBandDb (b, hz, kSr));
        CHECK (peak > 0.0 && peak < 1.5, "taper %.0f Hz: a little rise over the corner, under 1.5 dB (%.2f)", taper, peak);
        auto cut = [&] (double hz) { return clarityCutAtDb (b, hz, kSr, -8.0); };
        CHECK (std::fabs (cut (23999.0) + 8.0) < 0.05, "taper %.0f Hz: the whole cut at the top (%.2f dB at Nyquist)", taper, cut (23999.0));
        CHECK (cut (taper) < -6.8 && cut (taper) > -9.5, "taper %.0f Hz: within about 1 dB of the cut at the taper (%.2f dB)", taper, cut (taper));
        // (about half the cut an octave down; a little less near the top, where the low-pass is squeezed towards Nyquist)
        CHECK (cut (0.5 * taper) < (taper < 3000.0 ? -2.5 : -1.8) && cut (0.5 * taper) > -6.0, "taper %.0f Hz: about half the cut an octave down (%.2f dB)",
               taper, cut (0.5 * taper));
        CHECK (cut (0.25 * taper) > -1.5, "taper %.0f Hz: two octaves down, nearly none (%.2f dB)", taper, cut (0.25 * taper));
        CHECK (cut (100.0) > -0.05, "taper %.0f Hz: 100 Hz untouched (%.2f dB)", taper, cut (100.0));
    }
    {
        // the mirror of the Sub band: the same cut the same distance from the taper, the other way
        const ClarityBand hi = highBand (kSr, 2000.0), lo = subBand (kSr, 40.0);
        for (double ratio : {0.25, 0.5, 1.0})
        {
            const double h = clarityCutAtDb (hi, 2000.0 * ratio, kSr, -8.0), l = clarityCutAtDb (lo, 40.0 / ratio, kSr, -8.0);
            CHECK (std::fabs (h - l) < 0.3, "x%.2f: High %.2f dB, Sub %.2f dB", ratio, h, l);
        }
    }
    const ClarityBand clamped = highBand (kSr, 50000.0);
    CHECK (clamped.lowHz == 16000.0, "the taper is held to 16 kHz (%.0f)", clamped.lowHz);
    const auto& t = paramTable ();
    CHECK (t.info (kClarityHigh).def == 0.0 && t.info (kClarityHighFreq).def == 7000.0 && t.info (kClarityHighFreq).min == 2000.0 &&
               t.info (kClarityHighFreq).max == 16000.0 && t.info (kClarityHighRange).def == 0.0 &&
               t.info (kClarityHighThreshold).def == -18.0 && t.info (kClarityNoOverlap).def == 0.0,
           "defaults: Range 0 dB (it cuts nothing), starts to taper at 7 kHz (2 - 16 kHz), Threshold -18 dB; No Overlap off");
    CHECK (std::string (t.info (kClarityHigh).name) == "Gentlr High (unused)" && std::string (t.info (kClarityNoOverlap).name) == "Gentlr No Overlap",
           "called Gentlr High (its old button, unused) / No Overlap");

    // a loud 10 kHz fizz and a 500 Hz note, driven hard: High cuts the fizz, not the note
    auto in = tones ({{10000.0, -8.0}, {500.0, -14.0}}, 1.5);
    auto render = [&] (bool gentlr, bool high, double taper, double range, Meters* m = nullptr) {
        auto e = engine ();
        if (m)
            e->setMeters (m);
        e->setParam (kPreLimit, 0.0);
        e->setParam (kDrive, 12.0);
        e->setParam (kClarity, gentlr ? 1.0 : 0.0);
        e->setParam (kClarityRange, 0.0); // the other bands off: only High
        e->setParam (kClarityHighFreq, taper);
        e->setParam (kClarityHighRange, high ? range : 0.0); // (no button: Range 0 is off)
        return run (*e, in);
    };
    Meters m;
    const Sig on = render (true, true, 7000.0, 8.0, &m), off = render (true, false, 7000.0, 8.0);
    const double onDb = toneDb (on.l, 10000.0, 48000, 72000), offDb = toneDb (off.l, 10000.0, 48000, 72000);
    std::printf ("    10 kHz: %.1f dB with High, %.1f dB without; High cut %.1f dB, level %.1f dB\n", onDb, offDb, m.clarityHighDb.load (),
                 m.clarityHighLevelDb.load ());
    CHECK (onDb < offDb - 2.0, "High turns the fizz down: %.1f vs %.1f dB", onDb, offDb);
    CHECK (m.clarityHighDb.load () < -2.0 && m.clarityHighDb.load () >= -8.0 - 1e-3, "the High meter shows the cut (%.1f dB, Range 8)",
           m.clarityHighDb.load ());
    CHECK (m.claritySubDb.load () == 0.0f && m.clarityDb.load () == 0.0f, "the other bands' meters stay at 0");
    {
        auto note = tones ({{500.0, -14.0}}, 1.5);
        auto noteDb = [&] (bool high) {
            auto e = engine ();
            e->setParam (kPreLimit, 0.0);
            e->setParam (kDrive, 12.0);
            e->setParam (kClarity, 1.0);
            e->setParam (kClarityRange, 0.0);
            e->setParam (kClarityHighRange, high ? 6.0 : 0.0);
            return toneDb (run (*e, note).l, 500.0, 48000, 72000);
        };
        CHECK (std::fabs (noteDb (true) - noteDb (false)) < 0.05, "the 500 Hz note is left alone: %.2f vs %.2f dB", noteDb (true), noteDb (false));
    }
    CHECK (render (false, true, 7000.0, 8.0).l == render (false, false, 7000.0, 8.0).l, "Gentlr off: High does nothing");
    CHECK (render (true, true, 7000.0, 0.0).l == off.l, "Range 0 dB: High does nothing");
    CHECK (toneDb (render (true, true, 7000.0, 3.0).l, 10000.0, 48000, 72000) > onDb + 1.0, "a smaller Range cuts less");
    {
        // the slider moves where the taper starts: a loud 3 kHz tone is cut with the taper at 2 kHz, much
        // less with it at 12 kHz
        auto in3 = tones ({{3000.0, -8.0}}, 1.5);
        auto at = [&] (double taper, bool high) {
            auto e = engine ();
            e->setParam (kPreLimit, 0.0);
            e->setParam (kDrive, 12.0);
            e->setParam (kClarity, 1.0);
            e->setParam (kClarityRange, 0.0);
            e->setParam (kClarityHighRange, high ? 6.0 : 0.0);
            e->setParam (kClarityHighFreq, taper);
            return toneDb (run (*e, in3).l, 3000.0, 48000, 72000);
        };
        const double off3 = at (2000.0, false), at2k = at (2000.0, true) - off3, at12k = at (12000.0, true) - off3;
        std::printf ("    3 kHz: %.1f dB with the taper at 2 kHz, %.1f dB at 12 kHz\n", at2k, at12k);
        CHECK (at2k < at12k - 3.0, "the slider sets what reaches the band: %.1f dB at 2 kHz, %.1f dB at 12 kHz", at2k, at12k);
    }

    // High at Range 0 is what Gentlr was before it: bit for bit, whatever its other controls say (with
    // and without 4x oversampling, the other bands working)
    auto in2 = tones ({{80.0, -8.0}, {320.0, -12.0}, {3000.0, -10.0}, {9000.0, -12.0}}, 0.5);
    auto renderAll = [&] (bool hq, bool high, double taper, double range, double threshold) {
        auto e = engine (hq);
        e->setParam (kDrive, 14.0);
        e->setParam (kClarity, 1.0);
        e->setParam (kClarity2Range, 6.0);
        e->setParam (kClaritySubRange, 8.0);
        e->setParam (kClarityAdvanced, 1.0);
        e->setParam (kClarityHighFreq, taper);
        e->setParam (kClarityHighRange, high ? range : 0.0);
        e->setParam (kClarityHighThreshold, threshold);
        return run (*e, in2, 333);
    };
    for (bool hq : {true, false})
    {
        const Sig plain = renderAll (hq, false, 7000.0, 6.0, -18.0), moved = renderAll (hq, false, 2500.0, 20.0, -50.0);
        CHECK (std::memcmp (plain.l.data (), moved.l.data (), plain.l.size () * sizeof (float)) == 0 &&
                   std::memcmp (plain.r.data (), moved.r.data (), plain.r.size () * sizeof (float)) == 0,
               "hq %d: High at Range 0 ignores its other controls, to the bit", hq);
        CHECK (renderAll (hq, true, 7000.0, 6.0, -18.0).l != plain.l, "hq %d: High with a Range changes the sound", hq);
    }
    {
        // and with High at Range 0 the engine is the one from before the High band: the same output as
        // with the High band's parameters at their defaults (which is what a new instance has)
        auto e1 = engine (), e2 = engine ();
        for (auto* e : {e1.get (), e2.get ()})
        {
            e->setParam (kDrive, 14.0);
            e->setParam (kClarity, 1.0);
        }
        e2->setParam (kClarityHighFreq, 3000.0);
        e2->setParam (kClarityHighRange, 0.0);
        e2->setParam (kClarityHigh, 1.0); // (its old button: unused)
        CHECK (run (*e1, in2).l == run (*e2, in2).l, "High at Range 0 (its default) sounds as before it, its old button or not");
    }

    // Advanced: the High band's Threshold
    auto cutAt = [&] (double threshold) {
        Meters mm;
        auto e = engine ();
        e->setMeters (&mm);
        e->setParam (kPreLimit, 0.0);
        e->setParam (kDrive, -6.0);
        e->setParam (kClarity, 1.0);
        e->setParam (kClarityRange, 0.0);
        e->setParam (kClarityHighRange, 8.0);
        e->setParam (kClarityAdvanced, 1.0);
        e->setParam (kClarityHighThreshold, threshold);
        run (*e, in);
        return (double)mm.clarityHighDb.load ();
    };
    CHECK (cutAt (-3.0) == 0.0 && cutAt (-50.0) < -7.9, "Advanced: its Threshold sets where it cuts (%.2f / %.2f dB)", cutAt (-3.0), cutAt (-50.0));

    // through the tail: the fourth block
    CHECK (tailFieldOf (kClarityHigh) == (int)(kTailExt3First + pk::kTailExt3High) &&
               tailFieldOf (kClarityHighFreq) == (int)(kTailExt3First + pk::kTailExt3HighFreq) &&
               tailFieldOf (kClarityHighRange) == (int)(kTailExt3First + pk::kTailExt3HighRange) &&
               tailFieldOf (kClarityHighThreshold) == (int)(kTailExt3First + pk::kTailExt3HighThreshold) &&
               tailFieldOf (kClarityNoOverlap) == (int)(kTailExt3First + pk::kTailExt3NoOverlap),
           "the High band's and No Overlap's IDs to tail fields");
    std::vector<pk::ParamInfo> v3;
    addTailExt3Params (v3, 300);
    CHECK (v3.size () == pk::kTailExt3Fields && std::string (v3[pk::kTailExt3High].name) == "Saturator Gentlr High (unused)" &&
               v3[pk::kTailExt3HighFreq].def == 7000.0 && v3[pk::kTailExt3HighFreq].id == 300 + pk::kTailExt3HighFreq &&
               v3[pk::kTailExt3HighRange].def == 0.0 &&
               std::string (v3[pk::kTailExt3NoOverlap].name) == "Saturator Gentlr No Overlap" && v3[pk::kTailExt3NoOverlap].def == 0.0,
           "the tail's fourth block");
    auto renderT = [&] (bool high) {
        Tail tail;
        tail.prepare (48000.0, 512);
        tail.setParam (pk::kTailOn, 1.0);
        tail.setParam (pk::kTailMix, 1.0);
        tail.setParam (pk::kTailPreLimit, 0.0);
        tail.setParam (pk::kTailDrive, 12.0);
        tail.setParam (pk::kTailFields + pk::kTailExtClarity, 1.0);
        tail.setParam (pk::kTailFields + pk::kTailExtClarityRange, 0.0);
        tail.setParam (kTailExt3First + pk::kTailExt3HighRange, high ? 6.0 : 0.0);
        Sig out = in;
        for (size_t pos = 0; pos < out.l.size (); pos += 512)
            tail.process (out.l.data () + pos, out.r.data () + pos, (int)std::min<size_t> (512, out.l.size () - pos));
        return toneDb (out.l, 10000.0, 48000, 72000);
    };
    CHECK (renderT (true) < renderT (false) - 2.0, "High through the tail: %.1f vs %.1f dB", renderT (true), renderT (false));
}

// ---------------------------------------------------------------------------
// No Overlap: Gentlr's working bands never cover the same frequencies (NoOverlap.h)

static GentlrLayout layoutOf (double f1, double w1, double f2, double w2, double sub = 0.0, double high = 0.0)
{
    GentlrLayout l;
    l.on[0] = f1 > 0.0;
    l.on[1] = f2 > 0.0;
    l.on[kSubBand] = sub > 0.0;
    l.on[kHighBand] = high > 0.0;
    l.freq[0] = f1, l.width[0] = w1;
    l.freq[1] = f2, l.width[1] = w2;
    l.freq[kSubBand] = sub > 0.0 ? sub : kSubDefaultHz;
    l.freq[kHighBand] = high > 0.0 ? high : kHighDefaultHz;
    return l;
}
static double loEdge (const GentlrLayout& l, int k) { return k == kHighBand ? l.freq[k] : l.freq[k] / std::exp2 (0.5 * l.width[k]); }
static double hiEdge (const GentlrLayout& l, int k) { return k == kSubBand ? l.freq[k] : l.freq[k] * std::exp2 (0.5 * l.width[k]); }

TEST (gentlr_no_overlap_resolves_overlaps)
{
    // bands apart are left exactly as they are
    {
        GentlrLayout l = layoutOf (250.0, 2.0, 3000.0, 2.0, 40.0, 12000.0), was = l;
        resolveOverlaps (l);
        CHECK (std::memcmp (&l, &was, sizeof l) == 0 && !bandsOverlap (l), "bands apart: untouched, to the bit");
        // touching (as the editors leave them) is apart too
        GentlrLayout t = layoutOf (250.0, 2.0, 1000.0, 2.0);
        t.freq[1] = hiEdge (t, 0) * 2.0; // band 2's low edge on band 1's high edge
        const GentlrLayout tw = t;
        resolveOverlaps (t);
        CHECK (std::memcmp (&t, &tw, sizeof t) == 0, "touching bands are apart");
    }
    // two bands overlapping: split at the middle of the overlap (on a log axis), the outer edges kept
    {
        GentlrLayout l = layoutOf (250.0, 2.0, 400.0, 2.0); // 125 - 500 Hz and 200 - 800 Hz
        CHECK (bandsOverlap (l), "they overlap");
        resolveOverlaps (l);
        const double mid = std::sqrt (500.0 * 200.0);
        CHECK (std::fabs (hiEdge (l, 0) - mid) < 1e-6 && std::fabs (loEdge (l, 1) - mid) < 1e-6, "met at the middle, %.1f / %.1f Hz (%.1f)",
               hiEdge (l, 0), loEdge (l, 1), mid);
        CHECK (std::fabs (loEdge (l, 0) - 125.0) < 1e-6 && std::fabs (hiEdge (l, 1) - 800.0) < 1e-6, "the outer edges stay");
        CHECK (!bandsOverlap (l), "apart now");
    }
    // the band order follows the centres, whatever the band numbers
    {
        GentlrLayout l = layoutOf (4000.0, 2.0, 3000.0, 2.0);
        resolveOverlaps (l);
        CHECK (!bandsOverlap (l) && hiEdge (l, 1) <= loEdge (l, 0) * (1.0 + 1e-9), "band 2 below band 1: it stays below");
    }
    // a band inside a wide one: the narrow one is pushed up past the middle, as narrow as a band goes
    {
        GentlrLayout l = layoutOf (1000.0, 4.0, 1200.0, 0.5);
        resolveOverlaps (l);
        CHECK (!bandsOverlap (l), "apart");
        CHECK (l.width[1] >= kMinWidthOct - 1e-9 && l.width[0] >= kMinWidthOct - 1e-9, "no band narrower than 0.5 octaves (%.3f, %.3f)",
               l.width[0], l.width[1]);
    }
    // the shelves: Sub's Freq and High's come down / go up to meet the bands half way, within their ranges
    {
        GentlrLayout l = layoutOf (80.0, 2.0, 5000.0, 2.0, 100.0, 4000.0); // 40 - 160 Hz over Sub's 100 Hz; 2.5 - 10 kHz under High's 4 kHz
        resolveOverlaps (l);
        CHECK (!bandsOverlap (l), "apart");
        CHECK (std::fabs (l.freq[kSubBand] - std::sqrt (100.0 * 40.0)) < 1e-6, "Sub's Freq at the middle (%.2f Hz)", l.freq[kSubBand]);
        CHECK (std::fabs (l.freq[kHighBand] - std::sqrt (4000.0 * 10000.0)) < 1e-6, "High's Freq at the middle (%.0f Hz)", l.freq[kHighBand]);
        CHECK (l.freq[kSubBand] >= kSubMinHz && l.freq[kSubBand] <= kSubMaxHz && l.freq[kHighBand] >= kHighMinHz &&
                   l.freq[kHighBand] <= kHighMaxHz,
               "in their ranges");
    }
    {
        // a band low down: Sub held at 20 Hz, so the band gives up more
        GentlrLayout l = layoutOf (25.0, 2.0, 0.0, 0.0, 30.0);
        resolveOverlaps (l);
        CHECK (!bandsOverlap (l) && l.freq[kSubBand] >= kSubMinHz - 1e-9 && l.freq[0] >= 20.0 - 1e-9, "Sub at %.2f Hz, the band at %.2f Hz",
               l.freq[kSubBand], l.freq[0]);
    }
    // bands that do not work take no part
    {
        GentlrLayout l = layoutOf (250.0, 2.0, 300.0, 2.0);
        l.on[1] = false;
        const GentlrLayout was = l;
        resolveOverlaps (l);
        CHECK (std::memcmp (&l, &was, sizeof l) == 0, "band 2 does not work: nothing to keep apart");
    }
    // everything at once, at random: always apart, always in range, never narrower than a band can be
    uint32_t seed = 12345;
    auto rnd = [&] { return (seed = seed * 1664525u + 1013904223u) / 4294967296.0; };
    int bad = 0;
    for (int i = 0; i < 2000; ++i)
    {
        GentlrLayout l = layoutOf (20.0 * std::pow (1000.0, rnd ()), kMinWidthOct + rnd () * 3.5, 20.0 * std::pow (1000.0, rnd ()),
                                   kMinWidthOct + rnd () * 3.5, rnd () < 0.7 ? 20.0 * std::pow (5.0, rnd ()) : 0.0,
                                   rnd () < 0.7 ? 2000.0 * std::pow (8.0, rnd ()) : 0.0);
        resolveOverlaps (l);
        bool ok = !bandsOverlap (l) && l.freq[kSubBand] >= kSubMinHz - 1e-6 && l.freq[kSubBand] <= kSubMaxHz + 1e-6 &&
                  l.freq[kHighBand] >= kHighMinHz - 1e-6 && l.freq[kHighBand] <= kHighMaxHz + 1e-6;
        for (int k = 0; k < kClarityBands; ++k)
            ok = ok && l.width[k] >= kMinWidthOct - 1e-9 && l.width[k] <= kMaxWidthOct + 1e-9 && l.freq[k] >= 20.0 - 1e-6 && l.freq[k] <= 20000.0 + 1e-6;
        bad += ok ? 0 : 1;
    }
    CHECK (bad == 0, "%d random layouts were not resolved", bad);
}

TEST (gentlr_no_overlap_pushes)
{
    // band 1 dragged up into band 2: band 2's low edge is pushed along (it narrows), its high edge stays
    {
        const GentlrLayout before = layoutOf (250.0, 2.0, 2000.0, 2.0); // 125 - 500 Hz, 1 - 4 kHz
        GentlrLayout l = before;
        l.freq[0] = 1000.0; // now 500 - 2000 Hz
        pushBands (l, 0, before);
        CHECK (l.freq[0] == 1000.0 && l.width[0] == 2.0, "the dragged band goes where it was dragged");
        CHECK (std::fabs (loEdge (l, 1) - 2000.0) < 1e-6 && std::fabs (hiEdge (l, 1) - 4000.0) < 1e-6, "band 2 now %.0f - %.0f Hz",
               loEdge (l, 1), hiEdge (l, 1));
        CHECK (!bandsOverlap (l), "apart");
    }
    // pushed until it is as narrow as a band goes: then it moves as a whole
    {
        const GentlrLayout before = layoutOf (250.0, 2.0, 2000.0, 2.0);
        GentlrLayout l = before;
        l.freq[0] = 2000.0; // 1 - 4 kHz: band 2 cannot be 4 kHz - 4 kHz
        pushBands (l, 0, before);
        CHECK (std::fabs (l.width[1] - kMinWidthOct) < 1e-9 && std::fabs (loEdge (l, 1) - 4000.0) < 1e-6, "band 2: 0.5 octaves from 4 kHz (%.3f, %.0f Hz)",
               l.width[1], loEdge (l, 1));
        CHECK (!bandsOverlap (l), "apart");
    }
    // widening a band pushes both neighbours, and stays centred
    {
        const GentlrLayout before = layoutOf (1000.0, 1.0, 4000.0, 1.0, 100.0);
        GentlrLayout l = before;
        l.width[0] = 4.0; // 250 Hz - 4 kHz
        pushBands (l, 0, before);
        CHECK (l.freq[0] == 1000.0 && l.width[0] == 4.0, "widened as asked (%.1f Hz, %.2f oct)", l.freq[0], l.width[0]);
        CHECK (!bandsOverlap (l) && l.freq[kSubBand] == 100.0, "apart; Sub (at 100 Hz) untouched");
        CHECK (std::fabs (loEdge (l, 1) - 4000.0) < 1e-6, "band 2's low edge pushed to 4 kHz (%.0f)", loEdge (l, 1));
    }
    // the High band at the top of its range cannot move further: the dragged band stops at it
    {
        const GentlrLayout before = layoutOf (1000.0, 1.0, 0.0, 0.0, 0.0, 16000.0);
        GentlrLayout l = before;
        l.freq[0] = 18000.0; // its high edge would be ~25 kHz, past High at its highest (16 kHz)
        pushBands (l, 0, before);
        CHECK (std::fabs (hiEdge (l, 0) - 16000.0) < 1e-6 && l.width[0] == 1.0, "it stops at High's 16 kHz (%.0f Hz), as wide as it was",
               hiEdge (l, 0));
        CHECK (l.freq[kHighBand] == 16000.0 && !bandsOverlap (l), "High stays at 16 kHz; apart");
    }
    {
        // a chain: band 1 pushes band 2, which pushes High (a shelf moves its Freq), which stops at 16 kHz
        const GentlrLayout before = layoutOf (500.0, 1.0, 1000.0, 1.0, 0.0, 2000.0);
        GentlrLayout l = before;
        l.freq[0] = 20000.0;
        pushBands (l, 0, before);
        CHECK (!bandsOverlap (l) && std::fabs (l.freq[kHighBand] - kHighMaxHz) < 1e-6 && std::fabs (l.width[1] - kMinWidthOct) < 1e-9,
               "High at %.0f Hz, band 2 %.2f oct wide", l.freq[kHighBand], l.width[1]);
        CHECK (std::fabs (hiEdge (l, 0) - loEdge (l, 1)) < 1e-6 && std::fabs (hiEdge (l, 1) - 16000.0) < 1e-6, "everything pressed up to it");
    }
    // the Sub band dragged up pushes band 1's low edge; dragged back (from where the drag began) it lets go
    {
        const GentlrLayout before = layoutOf (200.0, 2.0, 3000.0, 2.0, 40.0); // band 1: 100 - 400 Hz
        GentlrLayout l = before;
        l.freq[kSubBand] = 100.0;
        pushBands (l, kSubBand, before);
        CHECK (l.freq[kSubBand] == 100.0 && std::fabs (loEdge (l, 0) - 100.0) < 1e-6 && std::fabs (hiEdge (l, 0) - 400.0) < 1e-6,
               "band 1 now %.0f - %.0f Hz", loEdge (l, 0), hiEdge (l, 0));
        GentlrLayout back = before;
        back.freq[kSubBand] = 50.0;
        pushBands (back, kSubBand, before);
        CHECK (back.freq[0] == before.freq[0] && back.width[0] == before.width[0], "dragged back: band 1 as it was");
    }
    // a band does not jump its neighbour: dragged past it, it pushes it along
    {
        const GentlrLayout before = layoutOf (500.0, 1.0, 1000.0, 1.0);
        GentlrLayout l = before;
        l.freq[1] = 200.0; // band 2 dragged far down, past band 1
        pushBands (l, 1, before);
        CHECK (!bandsOverlap (l) && hiEdge (l, 0) <= loEdge (l, 1) * (1.0 + 1e-9), "band 1 stays below band 2 (%.0f / %.0f Hz)", l.freq[0],
               l.freq[1]);
    }
    // a band that does not work pushes nothing and is not pushed
    {
        GentlrLayout before = layoutOf (250.0, 2.0, 400.0, 2.0);
        before.on[1] = false;
        GentlrLayout l = before;
        l.freq[0] = 400.0;
        pushBands (l, 0, before);
        CHECK (l.freq[1] == before.freq[1] && l.width[1] == before.width[1] && l.freq[0] == 400.0, "band 2 off: left alone");
    }
    // random drags of random bands: always apart (the bands apart before), in range
    uint32_t seed = 777;
    auto rnd = [&] { return (seed = seed * 1664525u + 1013904223u) / 4294967296.0; };
    int bad = 0;
    for (int i = 0; i < 2000; ++i)
    {
        GentlrLayout before = layoutOf (20.0 * std::pow (1000.0, rnd ()), kMinWidthOct + rnd () * 3.5, 20.0 * std::pow (1000.0, rnd ()),
                                        kMinWidthOct + rnd () * 3.5, rnd () < 0.7 ? 20.0 * std::pow (5.0, rnd ()) : 0.0,
                                        rnd () < 0.7 ? 2000.0 * std::pow (8.0, rnd ()) : 0.0);
        resolveOverlaps (before);
        const int k = (int)(rnd () * kGentlrBands) % kGentlrBands;
        GentlrLayout l = before;
        if (k == kSubBand)
            l.freq[k] = 20.0 * std::pow (5.0, rnd ());
        else if (k == kHighBand)
            l.freq[k] = 2000.0 * std::pow (8.0, rnd ());
        else if (rnd () < 0.5)
            l.freq[k] = 20.0 * std::pow (1000.0, rnd ());
        else
            l.width[k] = kMinWidthOct + rnd () * 3.5;
        pushBands (l, k, before);
        bool ok = !bandsOverlap (l) && l.freq[kSubBand] >= kSubMinHz - 1e-6 && l.freq[kSubBand] <= kSubMaxHz + 1e-6 &&
                  l.freq[kHighBand] >= kHighMinHz - 1e-6 && l.freq[kHighBand] <= kHighMaxHz + 1e-6;
        for (int b = 0; b < kClarityBands; ++b)
            ok = ok && l.width[b] >= kMinWidthOct - 1e-9 && l.width[b] <= kMaxWidthOct + 1e-9 && l.freq[b] >= 20.0 - 1e-6 && l.freq[b] <= 20000.0 + 1e-6;
        bad += ok ? 0 : 1;
    }
    CHECK (bad == 0, "%d random drags left bands overlapping or out of range", bad);
}

TEST (gentlr_no_overlap_in_the_engine)
{
    auto in = tones ({{150.0, -8.0}, {400.0, -10.0}, {3000.0, -10.0}, {9000.0, -12.0}}, 0.5);
    auto render = [&] (bool noOverlap, double f1, double w1, double f2, double w2, bool high) {
        auto e = engine ();
        e->setParam (kDrive, 14.0);
        e->setParam (kClarity, 1.0);
        e->setParam (kClarityFreq, f1);
        e->setParam (kClarityWidth, w1);
        e->setParam (kClarity2Freq, f2);
        e->setParam (kClarity2Width, w2);
        e->setParam (kClarity2Range, 6.0);
        e->setParam (kClarityHighRange, high ? 6.0 : 0.0);
        e->setParam (kClarityNoOverlap, noOverlap ? 1.0 : 0.0);
        return run (*e, in, 333).l;
    };
    // off, or with nothing overlapping, No Overlap changes nothing (to the bit)
    CHECK (render (true, 250.0, 2.0, 3000.0, 2.0, true) == render (false, 250.0, 2.0, 3000.0, 2.0, true), "bands apart: the same sound");
    // overlapping bands are kept apart: the sound is what the resolved layout gives
    const auto overlapping = render (true, 250.0, 2.0, 400.0, 2.0, false);
    CHECK (overlapping != render (false, 250.0, 2.0, 400.0, 2.0, false), "overlapping bands: No Overlap moves them");
    GentlrLayout l = layoutOf (250.0, 2.0, 400.0, 2.0);
    resolveOverlaps (l);
    CHECK (overlapping == render (false, l.freq[0], l.width[0], l.freq[1], l.width[1], false), "as if the bands had been set apart");
    // switching it on and off as it plays (automation) stays finite
    auto e = engine ();
    e->setParam (kDrive, 14.0);
    e->setParam (kClarity, 1.0);
    e->setParam (kClarity2Range, 6.0);
    e->setParam (kClarity2Freq, 300.0);
    Sig out;
    out.l.resize (in.l.size ());
    out.r.resize (in.r.size ());
    bool finite = true;
    for (size_t pos = 0, b = 0; pos < in.l.size (); pos += 256, ++b)
    {
        e->setParam (kClarityNoOverlap, (b & 1) ? 1.0 : 0.0);
        e->setParam (kClarityFreq, 200.0 + 30.0 * (double)(b % 7));
        const int n = (int)std::min<size_t> (256, in.l.size () - pos);
        e->process (in.l.data () + pos, in.r.data () + pos, out.l.data () + pos, out.r.data () + pos, n);
    }
    for (float x : out.l)
        finite = finite && std::isfinite (x) && std::fabs (x) < 4.0f;
    CHECK (finite, "No Overlap switched as it plays: finite");
}

// ---------------------------------------------------------------------------
// Glue: two neighbouring bands held at a shared border (Glue.h)

static bool near (double a, double b, double tol = 1e-6) { return std::fabs (a - b) <= tol * std::max (1.0, std::fabs (b)); }

TEST (gentlr_glue_pairs_and_parameters)
{
    // every pair that can meet has a switch, each switch names its two bands; Sub and High never meet
    for (int g = 0; g < kGluePairs; ++g)
        CHECK (glue::pairOf (glue::firstOf (g), glue::secondOf (g)) == g && glue::pairOf (glue::secondOf (g), glue::firstOf (g)) == g,
               "switch %d: its bands", g);
    CHECK (glue::pairOf (kSubBand, kHighBand) == -1 && glue::pairOf (0, 0) == -1, "Sub and High cannot glue");
    CHECK (kSubMaxHz < kHighMinHz, "(they never touch: the Sub band ends below where the High band starts)");
    const auto& t = paramTable ();
    const char* names[kGluePairs] = {"Gentlr Glue 1 / 2", "Gentlr Glue Sub / 1", "Gentlr Glue Sub / 2", "Gentlr Glue 1 / High", "Gentlr Glue 2 / High"};
    for (int g = 0; g < kGluePairs; ++g)
        CHECK (kClarityGlueIds[g] == kClarityGlue12 + (uint32_t)g && t.info (kClarityGlueIds[g]).def == 0.0 &&
                   std::string (t.info (kClarityGlueIds[g]).name) == names[g] && defaultParams ()[kClarityGlueIds[g]] == 0.0 &&
                   tailFieldOf (kClarityGlueIds[g]) == (int)(kTailExt4First + (uint32_t)g),
               "%s: off by default, the tail's fifth block", names[g]);
    std::vector<pk::ParamInfo> v4;
    addTailExt4Params (v4, 200);
    CHECK (v4.size () == pk::kTailExt4Fields && v4[0].id == 200 && std::string (v4[pk::kTailExt4Glue2High].name) == "Saturator Gentlr Glue 2 / High",
           "the tail's fifth block");
    const TailBases bases {10, 20, 50, 70, 200};
    CHECK (tailParamOf (kTailExt4First + pk::kTailExt4GlueSub2, bases) == 200 + pk::kTailExt4GlueSub2 &&
               tailFieldIn (200 + pk::kTailExt4Glue1High, bases) == (int)(kTailExt4First + pk::kTailExt4Glue1High) &&
               tailFieldIn (200 + pk::kTailExt4Fields, bases) == -1,
           "the fifth block's IDs and fields");
    CHECK (kTailAllFields == kTailExt4First + pk::kTailExt4Fields, "the tail takes all five blocks");
}

TEST (gentlr_glue_holds_the_border)
{
    bool none[kGluePairs] {}, all[kGluePairs];
    for (bool& b : all)
        b = true;
    // nothing glued, or glued and already touching: left exactly as it is
    {
        GentlrLayout l = layoutOf (250.0, 2.0, 1500.0, 2.0, 40.0, 9000.0), was = l;
        applyGlue (l, none);
        CHECK (std::memcmp (&l, &was, sizeof l) == 0, "nothing glued: untouched, to the bit");
        GentlrLayout t = layoutOf (250.0, 2.0, 1000.0, 2.0);
        t.freq[1] = hiEdge (t, 0) * 2.0; // touching
        const GentlrLayout tw = t;
        applyGlue (t, all);
        CHECK (std::memcmp (&t, &tw, sizeof t) == 0, "glued and touching: untouched, to the bit");
    }
    // band 1's high edge leads: band 2's low edge follows, its high edge stays (it gets wider)
    {
        bool g[kGluePairs] {};
        g[kGlue12] = true;
        GentlrLayout l = layoutOf (250.0, 2.0, 2000.0, 1.0); // 125 - 500 Hz, 1414 - 2828 Hz
        const double hi2 = hiEdge (l, 1);
        applyGlue (l, g);
        CHECK (near (loEdge (l, 1), 500.0) && near (hiEdge (l, 1), hi2) && near (hiEdge (l, 0), 500.0), "band 2 from %.1f Hz (500), to %.1f (%.1f)",
               loEdge (l, 1), hiEdge (l, 1), hi2);
        CHECK (near (l.freq[0], 250.0) && near (l.width[0], 2.0), "the leader stays");
        // automation moving band 1: band 2's low edge goes with it
        GentlrLayout m = l;
        m.freq[0] = 300.0;
        applyGlue (m, g);
        CHECK (near (loEdge (m, 1), hiEdge (m, 0)) && near (hiEdge (m, 1), hi2), "band 1 moved: the border follows (%.1f / %.1f Hz)",
               hiEdge (m, 0), loEdge (m, 1));
        // the follower at 4 octaves keeps that width and moves along
        GentlrLayout w = layoutOf (60.0, 0.5, 4000.0, 1.0);
        applyGlue (w, g);
        CHECK (near (loEdge (w, 1), hiEdge (w, 0)) && near (w.width[1], kMaxWidthOct), "pulled down past 4 octaves: it moves (%.2f oct)",
               w.width[1]);
    }
    // the band order is the centres': band 2 under band 1 leads band 1's low edge
    {
        bool g[kGluePairs] {};
        g[kGlue12] = true;
        GentlrLayout l = layoutOf (3000.0, 1.0, 400.0, 1.0);
        applyGlue (l, g);
        CHECK (near (loEdge (l, 0), hiEdge (l, 1)) && near (l.freq[1], 400.0), "band 2 below: band 1 follows it");
    }
    // the shelves lead: the Sub band's Freq is band 1's low edge, the High band's Freq band 2's high edge
    {
        bool g[kGluePairs] {};
        g[kGlueSub1] = g[kGlue2High] = true;
        GentlrLayout l = layoutOf (250.0, 2.0, 2500.0, 1.0, 60.0, 8000.0);
        const double hi1 = hiEdge (l, 0), lo2 = loEdge (l, 1);
        applyGlue (l, g);
        CHECK (near (loEdge (l, 0), 60.0) && near (hiEdge (l, 0), hi1) && near (l.freq[kSubBand], 60.0), "band 1 from the Sub band's 60 Hz");
        CHECK (near (hiEdge (l, 1), 8000.0) && near (loEdge (l, 1), lo2) && near (l.freq[kHighBand], 8000.0), "band 2 up to the High band's 8 kHz");
    }
    // a glue waits while its bands are not neighbours (or one does not work)
    {
        bool g[kGluePairs] {};
        g[kGlueSub2] = true;
        GentlrLayout l = layoutOf (250.0, 1.0, 2000.0, 1.0, 40.0), was = l; // band 1 between Sub and band 2
        applyGlue (l, g);
        CHECK (std::memcmp (&l, &was, sizeof l) == 0, "band 1 between them: nothing");
        GentlrLayout o = layoutOf (250.0, 1.0, 0.0, 0.0, 40.0), ow = o;
        g[kGlueSub2] = false;
        g[kGlue12] = true;
        applyGlue (o, g);
        CHECK (std::memcmp (&o, &ow, sizeof o) == 0, "band 2 does not work: nothing");
    }
    // at random, everything glued: every glued border equal (where the widths allow), widths in range
    uint32_t seed = 777;
    auto rnd = [&] { return (seed = seed * 1664525u + 1013904223u) / 4294967296.0; };
    int bad = 0;
    for (int i = 0; i < 2000; ++i)
    {
        GentlrLayout l = layoutOf (20.0 * std::pow (1000.0, rnd ()), kMinWidthOct + rnd () * 3.5, 20.0 * std::pow (1000.0, rnd ()),
                                   kMinWidthOct + rnd () * 3.5, rnd () < 0.7 ? 20.0 * std::pow (5.0, rnd ()) : 0.0,
                                   rnd () < 0.7 ? 2000.0 * std::pow (8.0, rnd ()) : 0.0);
        bool g[kGluePairs];
        for (bool& b : g)
            b = rnd () < 0.5;
        int top = -1; // the band under the High band (by the centres before)
        for (int k = 0; k < kClarityBands; ++k)
            if (l.on[k] && (top < 0 || l.freq[k] > l.freq[top]))
                top = k;
        applyGlue (l, g);
        bool ok = true;
        for (int k = 0; k < kClarityBands; ++k)
            ok = ok && l.width[k] >= kMinWidthOct - 1e-9 && l.width[k] <= kMaxWidthOct + 1e-9 && std::isfinite (l.freq[k]);
        // the High band's border holds (it leads the band under it, whatever happened below)
        if (l.on[kHighBand])
        {
            if (top >= 0 && g[glue::pairOf (top, kHighBand)] && hiEdge (l, top) < l.freq[kHighBand] * 4.0)
                ok = ok && (near (hiEdge (l, top), l.freq[kHighBand], 1e-6) || l.width[top] <= kMinWidthOct + 1e-9 ||
                            l.width[top] >= kMaxWidthOct - 1e-9);
        }
        bad += ok ? 0 : 1;
    }
    CHECK (bad == 0, "%d random glued layouts went out of range or let the High band's border go", bad);
}

TEST (gentlr_glue_follows_drags)
{
    bool g[kGluePairs] {};
    g[kGlue12] = true;
    // band 1 and band 2 touching at 500 Hz
    GentlrLayout start = layoutOf (250.0, 2.0, 1000.0, 2.0);
    // dragging the shared border (band 1's high edge, its low edge kept) up to 700 Hz: band 2 narrows
    {
        GentlrLayout l = start;
        l.freq[0] = std::sqrt (125.0 * 700.0);
        l.width[0] = std::log2 (700.0 / 125.0);
        followGlue (l, 0, start, g);
        CHECK (near (loEdge (l, 1), 700.0) && near (hiEdge (l, 1), 2000.0), "band 2 from %.1f Hz (700) to %.1f (2000)", loEdge (l, 1), hiEdge (l, 1));
    }
    // moving band 1 down an octave: band 2 widens down to it
    {
        GentlrLayout l = start;
        l.freq[0] = 125.0;
        followGlue (l, 0, start, g);
        CHECK (near (loEdge (l, 1), 250.0) && near (hiEdge (l, 1), 2000.0) && near (l.width[1], 3.0), "band 2 widened to 3 octaves (%.2f)", l.width[1]);
    }
    // moving band 1 up into band 2: band 2 narrows to half an octave, then moves along
    {
        GentlrLayout l = start;
        l.freq[0] = 1000.0; // high edge 2 kHz
        followGlue (l, 0, start, g);
        CHECK (near (loEdge (l, 1), 2000.0) && near (l.width[1], kMinWidthOct), "band 2 at 2 kHz, half an octave wide (%.2f)", l.width[1]);
    }
    // detached (switch off): band 2 stays where it is
    {
        bool off[kGluePairs] {};
        GentlrLayout l = start;
        l.freq[0] = 125.0;
        followGlue (l, 0, start, off);
        CHECK (near (l.freq[1], 1000.0) && near (l.width[1], 2.0), "detached: band 2 keeps its place");
    }
    // a chain: Sub glued to band 1, band 1 to band 2; dragging the Sub band's Freq moves band 1's low edge only
    {
        bool c[kGluePairs] {};
        c[kGlueSub1] = c[kGlue12] = true;
        GentlrLayout s = layoutOf (250.0, 2.0, 1000.0, 2.0, 62.5); // Sub up to 62.5 Hz, band 1 from 125 Hz: not touching
        s.freq[kSubBand] = 62.5;
        s.freq[0] = std::sqrt (62.5 * 500.0);
        s.width[0] = 3.0; // band 1 from 62.5 to 500 Hz
        GentlrLayout l = s;
        l.freq[kSubBand] = 80.0;
        followGlue (l, kSubBand, s, c);
        CHECK (near (loEdge (l, 0), 80.0) && near (hiEdge (l, 0), 500.0) && near (loEdge (l, 1), 500.0), "band 1 from 80 Hz, still glued to band 2 at 500");
        // band 1 moved down: held by the Sub band at 20 Hz (its low edge cannot go lower)
        GentlrLayout m = s;
        m.freq[0] = s.freq[0] / 8.0;
        followGlue (m, 0, s, c);
        CHECK (near (loEdge (m, 0), kSubMinHz) && near (m.freq[kSubBand], kSubMinHz) && near (m.width[0], 3.0), "held at %.2f Hz", loEdge (m, 0));
        CHECK (near (loEdge (m, 1), hiEdge (m, 0)), "band 2 still glued to it");
    }
    // the High band: band 2's high edge follows its Freq; band 2 dragged up stops at 16 kHz
    {
        bool c[kGluePairs] {};
        c[kGlue2High] = true;
        GentlrLayout s = layoutOf (250.0, 2.0, 0.0, 2.0, 0.0, 6000.0);
        s.on[1] = true;
        s.freq[1] = 3000.0;
        s.width[1] = 2.0 * std::log2 (6000.0 / 3000.0); // 1.5 - 6 kHz
        GentlrLayout l = s;
        l.freq[kHighBand] = 8000.0;
        followGlue (l, kHighBand, s, c);
        CHECK (near (hiEdge (l, 1), 8000.0) && near (loEdge (l, 1), 1500.0), "band 2 up to the High band's 8 kHz");
        GentlrLayout m = s;
        m.freq[1] = 12000.0;
        followGlue (m, 1, s, c);
        CHECK (near (hiEdge (m, 1), kHighMaxHz) && near (m.freq[kHighBand], kHighMaxHz) && near (m.width[1], 2.0), "held at 16 kHz (%.0f Hz)",
               hiEdge (m, 1));
    }
}

TEST (gentlr_glue_snaps_and_draws_its_borders)
{
    bool none[kGluePairs] {};
    GentlrLayout l = layoutOf (250.0, 2.0, 1000.0, 2.0, 40.0, 7000.0); // band 1 125 - 500, band 2 500 - 2000: touching
    // band 1's high edge near band 2's low edge snaps onto it; too far, nothing
    double target = 0.0;
    CHECK (snapTarget (l, 0, true, std::log2 (510.0), 0.05, none, &target) == 1 && near (target, std::log2 (500.0)), "snaps to band 2");
    CHECK (snapTarget (l, 0, true, std::log2 (600.0), 0.05, none, &target) == -1, "too far: nothing");
    CHECK (snapTarget (l, 0, false, std::log2 (41.0), 0.05, none, &target) == kSubBand && near (target, std::log2 (40.0)), "its low edge to the Sub band");
    CHECK (snapTarget (l, 1, true, std::log2 (7100.0), 0.05, none, &target) == kHighBand, "band 2's high edge to the High band");
    CHECK (snapTarget (l, kSubBand, true, std::log2 (126.0), 0.05, none, &target) == 0, "the Sub band's Freq to band 1");
    bool g[kGluePairs] {};
    g[kGlue12] = true;
    CHECK (snapTarget (l, 0, true, std::log2 (500.0), 0.05, g, &target) == -1, "already glued: no snap");
    // the borders: touching band 1 and band 2 (a dim link), the rest apart; glued ones lit
    GlueBorder b[kGentlrBands];
    int n = glueBorders (l, none, b);
    CHECK (n == 1 && b[0].pair == kGlue12 && !b[0].glued && near (b[0].at, std::log2 (500.0)), "one border, unglued (%d)", n);
    n = glueBorders (l, g, b);
    CHECK (n == 1 && b[0].glued, "glued: lit");
    // with No Overlap on, a glued pair keeps its border where No Overlap leaves bands touching
    bool sg[kGluePairs] {};
    sg[kGlueSub1] = true;
    GentlrLayout o = layoutOf (250.0, 2.0, 400.0, 2.0, 60.0); // band 2 overlapping band 1; band 1 apart from Sub
    applyGlue (o, sg);
    resolveOverlaps (o);
    CHECK (!bandsOverlap (o) && near (loEdge (o, 0), 60.0), "glue and No Overlap together: apart, band 1 glued to Sub (%.2f Hz)", loEdge (o, 0));
}

TEST (gentlr_glue_in_the_engine)
{
    auto in = tones ({{150.0, -8.0}, {400.0, -10.0}, {3000.0, -10.0}, {9000.0, -12.0}}, 0.5);
    auto render = [&] (const bool* g, double f1, double w1, double f2, double w2) {
        auto e = engine ();
        e->setParam (kDrive, 14.0);
        e->setParam (kClarity, 1.0);
        e->setParam (kClarityFreq, f1);
        e->setParam (kClarityWidth, w1);
        e->setParam (kClarity2Freq, f2);
        e->setParam (kClarity2Width, w2);
        e->setParam (kClarity2Range, 6.0);
        for (int k = 0; k < kGluePairs; ++k)
            e->setParam (kClarityGlueIds[k], g[k] ? 1.0 : 0.0);
        return run (*e, in, 333).l;
    };
    bool none[kGluePairs] {}, g[kGluePairs] {};
    g[kGlue12] = true;
    // touching (as the editors leave glued bands): glue changes nothing, to the bit
    const double f2 = 500.0 * 2.0;
    CHECK (render (g, 250.0, 2.0, f2, 2.0) == render (none, 250.0, 2.0, f2, 2.0), "glued and touching: the same sound");
    // apart (automation moved band 1): band 2's low edge follows, as applyGlue says
    const auto glued = render (g, 250.0, 2.0, 2000.0, 1.0);
    GentlrLayout l = layoutOf (250.0, 2.0, 2000.0, 1.0);
    applyGlue (l, g);
    CHECK (glued != render (none, 250.0, 2.0, 2000.0, 1.0) && glued == render (none, l.freq[0], l.width[0], l.freq[1], l.width[1]),
           "apart: as if band 2 had been set to the border");
}

// ---------------------------------------------------------------------------
// Gentlr's band Slope: 12 / 12 (the default), Signature (24 / 12) and Classic (12 / 6, as before)

TEST (gentlr_slopes_shape_the_bands)
{
    const int slopes[3] = {kSlope12, kSlopeSignature, kSlopeClassic};
    const char* names[3] = {"12 / 12", "Signature", "Classic"};
    const double below[3] = {12.0, 24.0, 12.0}, above[3] = {12.0, 12.0, 6.0};
    for (int i = 0; i < 3; ++i)
    {
        const ClarityBand b = clarityBand (kSr, 400.0, 2.0, slopes[i]); // (200 - 800 Hz: away from Nyquist's warping)
        auto db = [&] (double hz) { return clarityBandDb (b, hz, kSr); };
        double peak = -100.0;
        for (double hz = 40.0; hz < 4000.0; hz *= 1.01)
            peak = std::max (peak, db (hz));
        CHECK (std::fabs (peak) < 0.1, "%s: peaks at 0 dB (%.2f)", names[i], peak);
        CHECK (b.hp2On == (slopes[i] == kSlopeSignature) && !b.lowShelf && !b.highShelf, "%s: its sections", names[i]);
        // an octave and two octaves past each edge, and the slope between them (the other side's filter
        // still a little in it, two octaves away)
        const double lo1 = db (b.lowHz / 2), lo2 = db (b.lowHz / 4), hi1 = db (b.highHz * 2), hi2 = db (b.highHz * 4);
        std::printf ("    %-9s an octave / two below: %.1f / %.1f dB, above: %.1f / %.1f dB\n", names[i], lo1, lo2, hi1, hi2);
        CHECK (std::fabs ((lo1 - lo2) - below[i]) < 1.0, "%s: %.0f dB/oct below (%.1f)", names[i], below[i], lo1 - lo2);
        CHECK (std::fabs ((hi1 - hi2) - above[i]) < 1.0, "%s: %.0f dB/oct above (%.1f)", names[i], above[i], hi1 - hi2);
        CHECK (lo1 < -below[i] + 2.0 && lo1 > -below[i] - 4.0 && hi1 < -above[i] + 2.0 && hi1 > -above[i] - 4.0,
               "%s: an octave past each edge about one slope down (%.1f / %.1f dB)", names[i], lo1, hi1);
        // the cut at the band's centre: 12 / 12 is symmetric, its phases cancel there and the cut is the whole
        // Range; the others' deepest cut comes within 1.5 dB of it
        double deepest = 0.0;
        for (double hz = 40.0; hz < 4000.0; hz *= 1.01)
            deepest = std::min (deepest, clarityCutAtDb (b, hz, kSr, -12.0));
        if (slopes[i] == kSlope12)
            CHECK (std::fabs (clarityCutAtDb (b, 400.0, kSr, -12.0) + 12.0) < 0.05, "12 / 12: the whole cut at the centre (%.2f dB)",
                   clarityCutAtDb (b, 400.0, kSr, -12.0));
        CHECK (deepest < -10.5 && deepest > -12.05, "%s: the deepest cut %.2f dB of 12", names[i], deepest);
    }
    // Classic is the shape before the Slope, coefficient for coefficient (and clarityBand's default, for
    // Smoothr's character filters)
    {
        const ClarityBand c = clarityBand (kSr, 300.0, 1.5, kSlopeClassic), d = clarityBand (kSr, 300.0, 1.5);
        const BiquadCoeffs hp = highPass (kSr, c.lowHz, M_SQRT1_2), lp = lowPass1 (kSr, c.highHz);
        // (to the last few bits: MSVC may round an inlined expression differently from the same one computed
        // here; the bit-exact check of the whole render is gentlr_classic_slope_is_the_engine_before)
        auto near = [] (double x, double y) { return std::fabs (x - y) <= 1e-12 * std::max (1.0, std::fabs (y)); };
        auto same = [&] (const BiquadCoeffs& x, const BiquadCoeffs& y) {
            return near (x.b0, y.b0) && near (x.b1, y.b1) && near (x.b2, y.b2) && near (x.a1, y.a1) && near (x.a2, y.a2);
        };
        CHECK (same (c.hp, hp) && same (c.lp, lp) && !c.hp2On && same (d.hp, hp) && same (d.lp, lp) && near (d.norm, c.norm),
               "Classic: a 12 dB/oct Butterworth high-pass and a first-order low-pass, as before");
        const ClarityBand at = clarityBandAt (kSr, 500.0, 1.5, c.norm, kSlopeClassic);
        CHECK (same (at.lp, lowPass1 (kSr, at.highHz)) && !at.hp2On, "and retuned the same way");
        const ClarityBand sig = clarityBandAt (kSr, 500.0, 1.5, 1.0, kSlopeSignature);
        CHECK (sig.hp2On && same (sig.hp2, sig.hp), "Signature retuned: its second section too");
    }
}

TEST (gentlr_slopes_at_the_ends_are_shelves)
{
    // at an end of the spectrum each slope's band is a shelf: flat to the end, the cut exactly the Range
    // there, the side it keeps at that side's slope (12 / 12: 12 and 12; Signature: 24 below, 12 above;
    // Classic: 6 and 6, as before), at the band's level at its edge (-3 dB, Signature's 24 dB side -6 dB)
    const int slopes[3] = {kSlope12, kSlopeSignature, kSlopeClassic};
    const char* names[3] = {"12 / 12", "Signature", "Classic"};
    const double below[3] = {12.0, 24.0, 6.0}, above[3] = {12.0, 12.0, 6.0};
    for (int i = 0; i < 3; ++i)
    {
        const ClarityBand low = clarityBand (kSr, 40.0, 2.0, slopes[i]), high = clarityBand (kSr, 10000.0, 4.0, slopes[i]); // (edges 80 Hz, 2.5 kHz)
        auto db = [] (const ClarityBand& b, double hz) { return clarityBandDb (b, hz, kSr); };
        CHECK (low.lowShelf && !low.highShelf && high.highShelf && !high.lowShelf, "%s: shelves", names[i]);
        CHECK (std::fabs (db (low, 5.0)) < 0.5 && std::fabs (db (low, 20.0)) < 1.0, "%s: the low shelf flat to the bottom (%.2f dB at 5 Hz)",
               names[i], db (low, 5.0));
        CHECK (std::fabs (db (high, 20000.0)) < 1.0 && std::fabs (db (high, 23000.0)) < 0.5, "%s: the high shelf flat to the top (%.2f dB at 20 kHz)",
               names[i], db (high, 20000.0));
        // exact where flat: the whole cut at the very end
        CHECK (std::fabs (clarityCutAtDb (low, 1.0, kSr, -12.0) + 12.0) < 0.05 && std::fabs (clarityCutAtDb (high, 23990.0, kSr, -12.0) + 12.0) < 0.05,
               "%s: the cut exactly the Range at the ends (%.3f / %.3f dB)", names[i], clarityCutAtDb (low, 1.0, kSr, -12.0),
               clarityCutAtDb (high, 23990.0, kSr, -12.0));
        // (three to four octaves out: a critically damped section's knee is long)
        const double lowSlope = db (low, low.highHz * 8) - db (low, low.highHz * 16), highSlope = db (high, high.lowHz / 8) - db (high, high.lowHz / 16);
        CHECK (std::fabs (lowSlope - above[i]) < 1.0 && std::fabs (highSlope - below[i]) < 1.0,
               "%s: the low shelf %.0f dB/oct above its edge (%.1f), the high shelf %.0f below (%.1f)", names[i], above[i], lowSlope, below[i],
               highSlope);
        if (slopes[i] != kSlopeClassic)
        {
            const double edgeLo = db (low, low.highHz), edgeHi = db (high, high.lowHz);
            const double wantHi = slopes[i] == kSlopeSignature ? -6.0 : -3.0;
            CHECK (std::fabs (edgeLo + 3.0) < 0.3 && std::fabs (edgeHi - wantHi) < 0.3, "%s: at its edge -3 / %.0f dB (%.2f / %.2f)", names[i],
                   wantHi, edgeLo, edgeHi);
            // turned down by the full 24 dB, what comes up past the edge stays small (critically damped)
            double lift = 0.0;
            for (double hz = 20.0; hz < 2000.0; hz *= 1.02)
                lift = std::max (lift, clarityCutAtDb (low, hz, kSr, -24.0));
            for (double hz = 100.0; hz < 23000.0; hz *= 1.02)
                lift = std::max (lift, clarityCutAtDb (high, hz, kSr, -24.0));
            CHECK (lift < (slopes[i] == kSlopeSignature ? 2.8 : 1.4), "%s: at most %.2f dB up past a shelf's edge", names[i], lift);
        }
    }
    // the Sub and High bands keep their shape whatever the Slope (they take none)
}

TEST (gentlr_slope_parameter)
{
    const auto& t = paramTable ();
    CHECK (t.info (kClaritySlope).def == kSlope12 && t.toText (kClaritySlope, kSlope12) == "12 / 12" &&
               t.toText (kClaritySlope, kSlopeSignature) == "Signature" && t.toText (kClaritySlope, kSlopeClassic) == "Classic" &&
               std::string (t.info (kClaritySlope).name) == "Gentlr Slope",
           "the Slope: 12 / 12 by default, then Signature and Classic (%s)", t.toText (kClaritySlope, t.info (kClaritySlope).def).c_str ());
    CHECK (kClaritySlope == kClarityNoOverlap + 1 && kClarityGlue12 == kClaritySlope + 1, "appended: ID %u", (unsigned)kClaritySlope);
    CHECK (defaultParams ()[kClaritySlope] == kSlope12, "a new engine: 12 / 12");
    // the end saturators: the last field of the tail's fourth block
    CHECK (tailFieldOf (kClaritySlope) == (int)(kTailExt3First + pk::kTailExt3Slope) && pk::kTailExt3Slope == pk::kTailExt3Fields - 1,
           "the Slope's tail field");
    std::vector<pk::ParamInfo> v3;
    addTailExt3Params (v3, 300);
    CHECK (std::string (v3[pk::kTailExt3Slope].name) == "Saturator Gentlr Slope" && v3[pk::kTailExt3Slope].def == kSlope12,
           "the tail's Slope: 12 / 12 by default");
    CHECK (classicSlopeNorm () == 1.0, "Classic: the last choice");
}

// The engine's output with every Gentlr band at work (both bands, Sub, High, Advanced and the region
// Drive, a band moved halfway), hashed (FNV-1a over the output's bits)
static uint64_t gentlrRenderHash (int slope)
{
    const size_t n = 48000;
    std::vector<float> l (n), r (n), ol (n), orr (n);
    uint32_t seed = 12345;
    for (size_t i = 0; i < n; ++i)
    {
        seed = seed * 1664525u + 1013904223u;
        const double noise = ((seed >> 8) / 16777216.0 - 0.5) * 0.2;
        const double t = (double)i / 48000.0;
        l[i] = (float)(0.4 * std::sin (2.0 * M_PI * 80.0 * t) + 0.3 * std::sin (2.0 * M_PI * 320.0 * t) + 0.2 * std::sin (2.0 * M_PI * 3100.0 * t) + noise);
        r[i] = (float)(0.35 * std::sin (2.0 * M_PI * 110.0 * t) + 0.25 * std::sin (2.0 * M_PI * 9000.0 * t) + noise);
    }
    Engine e;
    e.setParam (kDrive, 12.0);
    e.setParam (kClarity, 1.0);
    e.setParam (kClarityFreq, 300.0);
    e.setParam (kClarityWidth, 1.5);
    e.setParam (kClarityRange, 12.0);
    e.setParam (kClarity2Freq, 3000.0);
    e.setParam (kClarity2Width, 2.5);
    e.setParam (kClarity2Range, 9.0);
    e.setParam (kClaritySubRange, 6.0);
    e.setParam (kClarityHighRange, 6.0);
    e.setParam (kClarityAdvanced, 1.0);
    e.setParam (kClarityThreshold, -30.0);
    e.setParam (kClarity2Threshold, -30.0);
    e.setParam (kClarityDrive, 1.0);
    e.setParam (kClaritySlope, slope);
    e.prepare (48000.0, 512);
    for (size_t p = 0; p < n; p += 512)
    {
        const int m = (int)std::min<size_t> (512, n - p);
        e.process (l.data () + p, r.data () + p, ol.data () + p, orr.data () + p, m);
        if (p == 24064)
            e.setParam (kClarityFreq, 150.0); // (the retune path)
    }
    uint64_t h = 1469598103934665603ull;
    for (const auto* v : {&ol, &orr})
        for (float f : *v)
        {
            uint32_t b;
            std::memcpy (&b, &f, 4);
            for (int i = 0; i < 4; ++i)
            {
                h ^= (b >> (8 * i)) & 0xff;
                h *= 1099511628211ull;
            }
        }
    return h;
}

TEST (gentlr_classic_slope_is_the_engine_before)
{
    // Classic renders bit for bit what the engine rendered before the Slope. The hash was taken from the
    // engine before the Slope (0.11), built the same way; the bits depend on the compiler and its maths
    // library, so it is pinned only for the Linux x86-64 GCC build (the CI's and the usual one), and
    // elsewhere printed
    const uint64_t classic = gentlrRenderHash (kSlopeClassic), twelve = gentlrRenderHash (kSlope12),
                   signature = gentlrRenderHash (kSlopeSignature);
    std::printf ("    Classic %016llx, 12 / 12 %016llx, Signature %016llx\n", (unsigned long long)classic, (unsigned long long)twelve,
                 (unsigned long long)signature);
    CHECK (classic != twelve && classic != signature && twelve != signature, "each slope sounds its own");
    CHECK (gentlrRenderHash (kSlopeClassic) == classic, "the same every time");
#if defined(__linux__) && defined(__x86_64__) && defined(__GNUC__) && !defined(__clang__)
    CHECK (classic == 0xb72650abb487a4f5ull, "Classic: the engine before the Slope, bit for bit (%016llx)", (unsigned long long)classic);
#endif
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
