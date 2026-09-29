// Headless tests for the Smacheratr DSP. Run: ./smacheratr_tests [filter]
#include "Color.h"
#include "Engine.h"
#include "Params.h"
#include "Shaper.h"

#include <chrono>
#include <cmath>
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
    CHECK (t.info (kPreLimit).def == 0.0 && t.info (kPreLimitThreshold).def == -6.0, "pre-limit off, at -6 dB");
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
        const auto t0 = std::chrono::steady_clock::now ();
        run (*e, in);
        secs = std::min (secs, std::chrono::duration<double> (std::chrono::steady_clock::now () - t0).count ());
    }
    std::printf ("    CPU: %.2f%% of one core (stereo, Hi-Quality)\n", 100.0 * secs / 10.0);
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
