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
    for (size_t i = a; i < b; ++i)
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
    e->setParam (kDrive, 0.0);   // the defaults are a preset (Drive 14 dB, Color on);
    e->setParam (kColorOn, 0.0); // tests start neutral
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
    CHECK (t.info (kDrive).def == 14.0 && t.info (kColorOn).def == 1.0, "defaults: the usual settings");
    CHECK (t.toText (kCurve, kSinoidFold) == "Sinoid Fold", "%s", t.toText (kCurve, kSinoidFold).c_str ());
    CHECK (t.info (kCurve).choices.size () == kNumCurves, "curve count");
}

TEST (curves)
{
    // every curve is odd, passes quiet signals unchanged, and (except the fold) never exceeds +-1
    for (int c = 0; c < kNumCurves; ++c)
    {
        ShaperSettings s;
        s.curve = c;
        CHECK (std::fabs (shape (0.0, s)) < 1e-12, "curve %d at zero", c);
        CHECK (std::fabs (shape (1e-3, s) / 1e-3 - 1.0) < 2e-3, "curve %d slope at zero: %g", c, shape (1e-3, s) / 1e-3);
        for (double x = -10.0; x <= 10.0; x += 0.037)
        {
            CHECK (std::fabs (shape (x, s) + shape (-x, s)) < 1e-9, "curve %d odd at %f", c, x);
            if (c != kSinoidFold)
                CHECK (std::fabs (shape (x, s)) <= 1.0 + 1e-9, "curve %d exceeds 1 at %f: %f", c, x, shape (x, s));
        }
        // monotonic where they should be
        if (c != kSinoidFold)
        {
            double prev = -2.0;
            for (double x = -3.0; x <= 3.0; x += 0.01)
            {
                const double y = shape (x, s);
                CHECK (y >= prev - 1e-9, "curve %d not monotonic at %f", c, x);
                prev = y;
            }
        }
    }
    // the clips are linear below the clipping point, the saturations are not
    ShaperSettings s;
    for (int c : {kAnalogClip, kDigitalClip})
    {
        s.curve = c;
        CHECK (std::fabs (shape (0.45, s) - 0.45) < 1e-12, "curve %d linear at 0.45", c);
    }
    s.curve = kDigitalClip;
    CHECK (shape (0.999, s) == 0.999 && shape (1.5, s) == 1.0, "digital clip");
    s.curve = kMediumCurve;
    CHECK (shape (0.45, s) < 0.44, "medium curve saturates early");
    // Bass Shaper: 0 dB threshold is a hard clip, lower thresholds are softer
    s.curve = kBassShaper;
    s.bassThresholdDb = 0.0;
    CHECK (std::fabs (shape (0.9, s) - 0.9) < 1e-12 && shape (1.3, s) == 1.0, "bass shaper at 0 dB = hard clip");
    s.bassThresholdDb = -20.0;
    CHECK (std::fabs (shape (0.05, s) - 0.05) < 1e-12, "linear below threshold");
    CHECK (shape (0.9, s) < 0.85 && shape (0.9, s) > 0.6, "soft above it: %f", shape (0.9, s));
    s.bassThresholdDb = -50.0;
    CHECK (shape (0.9, s) < 0.75, "softer still: %f", shape (0.9, s));
    // the fold really folds
    s.curve = kSinoidFold;
    CHECK (shape (M_PI, s) < 1e-9 && shape (M_PI_2, s) > 0.999, "fold");
    // Waveshaper: Drive 0 is a plain clip, Linear widens the linear region, Depth adds ripple
    s.curve = kWaveshaper;
    s.ws.drive = 0.0;
    CHECK (shape (0.7, s) == 0.7 && shape (1.7, s) == 1.0, "waveshaper drive 0");
    s.ws.drive = 1.0;
    s.ws.linear = 0.9;
    CHECK (std::fabs (shape (0.8, s) - 0.8) < 1e-12, "linear region");
    s.ws.linear = 0.2;
    s.ws.curve = 1.0;
    CHECK (shape (0.8, s) > 0.8 && shape (0.8, s) < 1.0, "curved region rounds off towards 1: %f", shape (0.8, s));
    s.ws.curve = 0.0;
    CHECK (std::fabs (shape (0.8, s) - 0.8) < 1e-12 && shape (1.2, s) == 1.0, "curve 0 is a hard clip");
    s.ws.curve = 1.0;
    s.ws.depth = 1.0;
    double ripple = 0.0;
    for (double x = 0.0; x < 0.2; x += 0.001)
        ripple = std::max (ripple, std::fabs (shape (x, s) - x));
    CHECK (ripple > 0.02, "depth adds ripple: %f", ripple);
    s.ws.depth = 0.0;
    s.ws.damp = 1.0;
    CHECK (std::fabs (shape (0.1, s)) < 0.05, "damp flattens around zero: %f", shape (0.1, s));
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
            CHECK (std::fabs (magnitudeDb (lo, 10.0, rate) - g) < 0.2, "shelf gain %f at 10 Hz: %f", g, magnitudeDb (lo, 10.0, rate));
            CHECK (std::fabs (magnitudeDb (pk, 1200.0, rate) - g) < 1e-6, "peak gain %f at centre", g);
        }
    CHECK (std::fabs (colorResponseDb (10000.0, kSr, 0.5, 0.0, 1000.0, 1.0)) < 0.1, "shelf leaves highs alone");
    CHECK (std::fabs (colorResponseDb (20.0, kSr, 0.5, 0.0, 1000.0, 1.0) - 12.0) < 0.3, "+50 %% is +12 dB in the lows");
}

TEST (transparent_at_zero_drive)
{
    // Analog Clip is linear below 0.5: a quiet signal comes out delayed by exactly latency()
    auto in = tones ({{60.0, -14.0}, {1000.0, -14.0}, {9000.0, -20.0}}, 1.0);
    for (bool hq : {true, false})
    {
        auto e = engine (hq);
        auto out = run (*e, in, 333);
        const int lat = e->latency ();
        CHECK (lat > 0 && lat < 200, "latency %d", lat);
        const double err = delayedError (out.l, in.l, (size_t)lat, 4800);
        CHECK (err < (hq ? 2e-3 : 1e-6), "hq %d: reconstruction error %g (latency %d)", hq, err, lat);
        CHECK (e->latency () == engine (!hq)->latency (), "latency is the same with Hi-Quality on and off");
    }
    // the non-oversampled path is sample-exact including a DC offset (no DC filter)
    auto e = engine (false);
    auto dcIn = tones ({{100.0, -20.0}}, 0.5, 0.3);
    auto out = run (*e, dcIn);
    CHECK (delayedError (out.l, dcIn.l, (size_t)e->latency (), 1000) < 1e-6, "dc passes");
}

TEST (drive_adds_harmonics_and_clips_never_exceed_one)
{
    auto in = tones ({{1000.0, -12.0}}, 1.0);
    const size_t a = 24000, b = 48000;
    for (int c = 0; c < kNumCurves; ++c)
    {
        auto e = engine (false);
        e->setParam (kCurve, c);
        e->setParam (kDrive, 18.0);
        auto out = run (*e, in);
        const double h3 = toneDb (out.l, 3000.0, a, b);
        CHECK (h3 > -30.0, "curve %d: third harmonic %f dB", c, h3);
        if (c != kSinoidFold)
            CHECK (peakOf (out.l, a, b) <= 1.0 + 1e-6, "curve %d peak %f", c, peakOf (out.l, a, b));
        // and nothing at all when the signal stays in the linear region
        e = engine (false);
        e->setParam (kCurve, c);
        e->setParam (kDrive, c == kAnalogClip || c == kDigitalClip || c == kWaveshaper || c == kBassShaper ? -6.0 : -40.0);
        out = run (*e, in);
        CHECK (toneDb (out.l, 3000.0, a, b) < -90.0, "curve %d: quiet signal is clean: %f dB", c, toneDb (out.l, 3000.0, a, b));
    }
    // the fold pulls the fundamental down once the signal wraps
    auto e = engine (false);
    e->setParam (kCurve, kSinoidFold);
    e->setParam (kDrive, 24.0);
    auto out = run (*e, tones ({{1000.0, -12.0}}, 1.0));
    CHECK (toneDb (out.l, 1000.0, a, b) < -6.0, "folded fundamental %f dB", toneDb (out.l, 1000.0, a, b));
    CHECK (peakOf (out.l, a, b) <= 1.0 + 1e-6, "fold peak %f", peakOf (out.l, a, b));
}

TEST (bass_shaper_threshold)
{
    auto in = tones ({{60.0, -3.0}}, 1.0);
    const size_t a = 24000, b = 48000;
    auto measure = [&] (double thr) {
        auto e = engine (false);
        e->setParam (kCurve, kBassShaper);
        e->setParam (kBassThreshold, thr);
        e->setParam (kDrive, 6.0);
        auto out = run (*e, in);
        return toneDb (out.l, 180.0, a, b) - toneDb (out.l, 60.0, a, b);
    };
    const double hard = measure (0.0), mid = measure (-20.0), soft = measure (-50.0);
    CHECK (hard > mid && mid > soft, "third harmonic falls as the threshold drops: %f %f %f", hard, mid, soft);
    auto e = engine (false);
    e->setParam (kCurve, kBassShaper);
    e->setParam (kBassThreshold, 0.0);
    e->setParam (kDrive, 6.0);
    auto out = run (*e, in);
    auto e2 = engine (false);
    e2->setParam (kCurve, kDigitalClip);
    e2->setParam (kDrive, 6.0);
    auto out2 = run (*e2, in);
    double diff = 0;
    for (size_t i = a; i < b; ++i)
        diff = std::max (diff, (double)std::fabs (out.l[i] - out2.l[i]));
    CHECK (diff < 1e-6, "0 dB threshold equals the digital clip: %g", diff);
}

TEST (post_clip_and_output)
{
    // negative Amt Lo lets a driven bass tone come out above 0 dB; the post clip stops that
    auto in = tones ({{60.0, -1.0}}, 1.0);
    const size_t a = 24000, b = 48000;
    auto make = [&] (int post, double outDb) {
        auto e = engine (false);
        e->setParam (kCurve, kMediumCurve);
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
        e->setParam (kCurve, kMediumCurve);
        e->setParam (kDrive, 12.0);
        e->setParam (kColorOn, 1.0);
        e->setParam (kColorLo, amtLo);
        auto out = run (*e, bass);
        return toneDb (out.l, 180.0, a, b) - toneDb (out.l, 60.0, a, b);
    };
    CHECK (h3 (-1.0) < h3 (0.0) - 15.0, "Amt Lo -100 %% cleans the bass: %f vs %f", h3 (-1.0), h3 (0.0));
    CHECK (h3 (0.5) > h3 (0.0) + 0.5, "Amt Lo +50 %% saturates it more: %f vs %f", h3 (0.5), h3 (0.0));
    // Amt Hi does the same around Freq
    auto mid = tones ({{2000.0, -1.0}}, 1.0);
    auto h3mid = [&] (double amtHi) {
        auto e = engine (false);
        e->setParam (kCurve, kMediumCurve);
        e->setParam (kDrive, 12.0);
        e->setParam (kColorOn, 1.0);
        e->setParam (kColorHi, amtHi);
        e->setParam (kColorFreq, 2000.0);
        auto out = run (*e, mid);
        return toneDb (out.l, 6000.0, a, b) - toneDb (out.l, 2000.0, a, b);
    };
    CHECK (h3mid (-1.0) < h3mid (0.0) - 15.0, "Amt Hi -100 %%: %f vs %f", h3mid (-1.0), h3mid (0.0));
}

TEST (hi_quality_reduces_aliasing)
{
    // a saturated 10 kHz tone: its 5th harmonic (50 kHz) aliases to 2 kHz at 48 kHz. (A hard clip
    // at full drive is a square wave whose 19th harmonic still aliases at 4x; tanh falls off.)
    auto in = tones ({{10000.0, -6.0}}, 1.0);
    const size_t a = 24000, b = 48000;
    auto alias = [&] (bool hq) {
        auto e = engine (hq);
        e->setParam (kCurve, kMediumCurve);
        e->setParam (kDrive, 12.0);
        auto out = run (*e, in);
        return toneDb (out.l, 2000.0, a, b);
    };
    const double off = alias (false), on = alias (true);
    CHECK (off > -40.0, "aliasing without oversampling: %f dB", off);
    CHECK (on < off - 30.0, "Hi-Quality suppresses it: %f vs %f dB", on, off);
}

TEST (dry_wet_and_dc_filter)
{
    auto in = tones ({{100.0, -6.0}}, 1.0, 0.4);
    auto e = engine (false);
    e->setParam (kCurve, kDigitalClip);
    e->setParam (kDrive, 24.0);
    e->setParam (kDryWet, 0.0);
    e->reset ();
    auto out = run (*e, in);
    CHECK (delayedError (out.l, in.l, (size_t)e->latency (), 4800) < 1e-6, "dry");
    e = engine (false);
    e->setParam (kDrive, -6.0); // stays linear: the offset passes
    out = run (*e, in);
    CHECK (std::fabs (meanOf (out.l, 24000, 48000) - 0.2) < 0.01, "offset passes without the filter: %f", meanOf (out.l, 24000, 48000));
    e->setParam (kDcFilter, 1.0);
    e->reset ();
    out = run (*e, in);
    CHECK (std::fabs (meanOf (out.l, 24000, 48000)) < 0.01, "DC filter removes it: %f", meanOf (out.l, 24000, 48000));
    CHECK (std::fabs (toneDb (out.l, 100.0, 24000, 48000) + 12.0) < 0.2, "and keeps the tone: %f", toneDb (out.l, 100.0, 24000, 48000));
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
    auto e = engine (true);
    e->setParam (kCurve, kWaveshaper);
    e->setParam (kColorOn, 1.0);
    e->setParam (kPostClip, kPostSoft);
    e->setParam (kDrive, 12.0);
    auto in = tones ({{55.0, -6.0}, {1000.0, -12.0}, {8000.0, -20.0}}, 10.0);
    const auto t0 = std::chrono::steady_clock::now ();
    run (*e, in);
    const double secs = std::chrono::duration<double> (std::chrono::steady_clock::now () - t0).count ();
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
