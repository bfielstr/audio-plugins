// Headless tests for the Smacheratr DSP. Run: ./smacheratr_tests [filter]
#include "Color.h"
#include "ClarityBand.h"
#include "Engine.h"
#include "Tail.h"
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
               std::string (v[pk::kTailExtClarityFreq].name) == "Saturator Clarity Frequency" &&
               v[pk::kTailExtClarityFreq].def == 250.0 && v[pk::kTailExtColorOn].def == 0.0,
           "extended tail parameters");
    CHECK (tailFieldOf (kClarityWidth) == (int)(pk::kTailFields + pk::kTailExtClarityWidth) && tailFieldOf (kDryWet) == pk::kTailMix,
           "Smacheratr IDs to tail fields");
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
