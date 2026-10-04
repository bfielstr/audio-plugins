// Headless tests for the Smacheratr DSP. Run: ./smacheratr_tests [filter]
#include "Color.h"
#include "ClarityBand.h"
#include "Engine.h"
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

static std::unique_ptr<Engine> engine (bool hiQuality = true)
{
    auto e = std::make_unique<Engine> ();
    e->setParam (kHiQuality, hiQuality ? 1.0 : 0.0);
    e->setParam (kColorOn, 0.0); // the defaults have Color on; tests start neutral
    e->prepare (kSr, 512);
    return e;
}

// ---------------------------------------------------------------------------
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
        CHECK (lat > 48 && lat < 200, "latency %d (look-ahead + oversampling)", lat);
        const double err = delayedError (out.l, in.l, (size_t)lat, 4800);
        CHECK (err < (hq ? 2e-3 : 1e-6), "hq %d: reconstruction error %g (latency %d)", hq, err, lat);
        CHECK (e->latency () == engine (!hq)->latency (), "latency is the same with Hi-Quality on and off");
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
    // with Hi-Quality the downsampling filter rings a little past the curve
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
    // driven into Soft or Hard Clip, nothing after the clip takes the output back over 0 dBFS: not Hi-Quality's
    // downsampling filter, Mid/Side back to left / right, Gentlr, the dry part of a mix or Output
    struct Case
    {
        bool hiq, ms, gentlr;
        double mix, outDb;
        const char* name;
    };
    const Case cases[] = {{true, false, false, 1.0, 0.0, "Hi-Quality"},  {false, true, false, 1.0, 0.0, "Mid/Side"},
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

TEST (hi_quality_reduces_aliasing)
{
    // a saturated 10 kHz tone: its 5th harmonic (50 kHz) aliases to 2 kHz at 48 kHz
    auto in = tones ({{10000.0, -6.0}}, 1.0);
    const size_t a = 24000, b = 48000;
    auto alias = [&] (bool hq) {
        auto e = engine (hq);
        e->setParam (kDrive, 12.0);
        auto out = run (*e, in);
        return toneDb (out.l, 2000.0, a, b);
    };
    const double off = alias (false), on = alias (true);
    CHECK (off > -50.0, "aliasing without oversampling: %f dB", off);
    CHECK (on < off - 20.0, "Hi-Quality suppresses it: %f vs %f dB", on, off);
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
    // oversampled or not, the region lines up with the rest (the latency does not change): Hi-Quality
    // on and off give nearly the same output
    const Sig onLow = render (true, false);
    double diff = 0.0, sum = 0.0;
    for (size_t i = 24000; i < on.l.size (); ++i)
    {
        diff += (on.l[i] - onLow.l[i]) * (on.l[i] - onLow.l[i]);
        sum += on.l[i] * on.l[i];
    }
    CHECK (std::sqrt (diff / sum) < 0.05, "Hi-Quality on vs off with the region driven: %.3f", std::sqrt (diff / sum));
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
    std::printf ("    CPU: %.2f%% of one core (stereo, Hi-Quality)\n", 100.0 * secs / 10.0);
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
    // and without Hi-Quality, the other bands working)
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
