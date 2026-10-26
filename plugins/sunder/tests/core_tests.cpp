// Headless tests for the Sunder DSP. Run: ./sunder_tests [filter]
// Against the Softube model's measured numbers (germcal, at the model's own positions: setModelPositions): the
// static gain reduction at two settings, the output stage's gain over Drive and Feedback and its low bump, the
// Mix law; then the FET's second harmonic (always in), Link, Mid/Side, the latency, switches, a NaN; the steps
// (Drive and Feedback 1 .. 10 over the positions 1 .. 15, extrapolated above 10, stable at the top), the bass
// enhancer ahead of the compressor (Bass 0: nothing of it runs; its latency), and the CPU budget. The enhancer
// alone is tested against bubr's measurements in bass_tests.cpp.
#include "pluginkit/testing/CpuClock.h"
#include "Engine.h"
#include "Params.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdio>
#include <cstring>
#include <functional>
#include <limits>
#include <memory>
#include <string>
#include <vector>

using namespace sunder;

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
double db (double v) { return 20.0 * std::log10 (std::max (v, 1e-20)); }
bool finite (const std::vector<float>& x)
{
    for (float v : x)
        if (!std::isfinite (v))
            return false;
    return true;
}
std::vector<float> sine (double hz, double seconds, double amp)
{
    std::vector<float> x ((size_t)(seconds * kSr));
    for (size_t i = 0; i < x.size (); ++i)
        x[i] = (float)(amp * std::sin (2.0 * kPi * hz * (double)i / kSr));
    return x;
}
// a detuned saw pair at 55 Hz with a little noise (a bass): about -14 dBFS RMS at gain 1
std::vector<float> bassLine (double seconds, double gain = 1.0)
{
    std::vector<float> x ((size_t)(seconds * kSr));
    uint32_t s = 1;
    for (size_t i = 0; i < x.size (); ++i)
    {
        const double t = (double)i / kSr;
        const double a = 55.0 * t, b = 55.0 * 1.012 * t + 0.3;
        s = s * 1664525u + 1013904223u;
        x[i] = (float)(gain * (0.15 * ((2.0 * (a - std::floor (a)) - 1.0) + (2.0 * (b - std::floor (b)) - 1.0)) +
                               0.01 * ((int32_t)s / 2147483648.0)));
    }
    return x;
}

// an engine at the defaults, prepared; tweak (when given) sets parameters first
std::unique_ptr<Engine> engine (const std::function<void (Engine&)>& tweak = {})
{
    auto e = std::make_unique<Engine> ();
    e->prepare (kSr, 512);
    if (tweak)
        tweak (*e);
    e->reset ();
    return e;
}
// runs stereo input in blocks (right = left when not given)
std::vector<float> run (Engine& e, const std::vector<float>& x, std::vector<float>* right = nullptr, int block = 480,
                        const std::vector<float>* inRight = nullptr)
{
    std::vector<float> l (x.size ()), r (x.size ());
    const std::vector<float>& xr = inRight ? *inRight : x;
    for (size_t a = 0; a < x.size (); a += (size_t)block)
    {
        const int n = (int)std::min ((size_t)block, x.size () - a);
        e.process (x.data () + a, xr.data () + a, l.data () + a, r.data () + a, n);
    }
    if (right)
        *right = r;
    return l;
}
// the level of frequency f in x[a, b) (a Blackman-Harris window, as an amplitude)
double toneLevel (const std::vector<float>& x, size_t a, size_t b, double f)
{
    const size_t n = b - a;
    std::complex<double> acc = 0.0;
    double wsum = 0.0;
    for (size_t i = 0; i < n; ++i)
    {
        const double t = 2.0 * kPi * (double)i / (double)(n - 1);
        const double w = 0.35875 - 0.48829 * std::cos (t) + 0.14128 * std::cos (2 * t) - 0.01168 * std::cos (3 * t);
        acc += w * (double)x[a + i] * std::polar (1.0, -2.0 * kPi * f * (double)i / kSr);
        wsum += w;
    }
    return 2.0 * std::abs (acc) / wsum;
}
// the gain (dB) of a steady sine at hz and level dBFS through e, measured over its last 0.3 s
double gainAt (Engine& e, double hz, double levelDb, double seconds = 1.5)
{
    const double amp = std::pow (10.0, levelDb / 20.0);
    const auto y = run (e, sine (hz, seconds, amp));
    return db (toneLevel (y, y.size () - (size_t)(0.3 * kSr), y.size (), hz) / amp);
}

} // namespace

TEST (static_compression_is_the_models)
{
    // a 1 kHz sine at the timing the model was measured at (Attack 3, Release Fast), Drive 0: its gain
    // reduction against the same sine 50 dB down, as the Softube model gave it
    struct Case
    {
        double ratio;
        int curve;
        double level, gr;
    };
    const Case cases[] = {{4.0, kGermSoft, -30.0, -0.45},   {4.0, kGermSoft, -20.0, -5.05},   {4.0, kGermSoft, -10.0, -12.93},
                          {4.0, kGermSoft, 0.0, -19.28},    {11.0, kZenerHard, -30.0, -8.24}, {11.0, kZenerHard, -20.0, -15.16},
                          {11.0, kZenerHard, -10.0, -20.68}, {1.0, kRSoft, -20.0, -1.46},      {1.0, kRSoft, 0.0, -15.81}};
    for (const auto& c : cases)
    {
        auto setup = [&c] (Engine& e) {
            e.setParam (kCurve, c.curve);
            e.setParam (kAttack, 3.0);
            e.setParam (kRelease, 0.0);
            e.setModelPositions (c.ratio, 0.0, 5.0); // (Drive 0: below the steps)
        };
        auto quiet = engine (setup), loud = engine (setup);
        const double g0 = gainAt (*quiet, 1000.0, -50.0), g = gainAt (*loud, 1000.0, c.level);
        const double gr = g - g0 + (double)staticGrDb (c.curve, c.ratio, -50.0);
        CHECK (std::fabs (gr - c.gr) < 0.35, "Ratio %.0f, %s, %.0f dBFS: %.2f dB (the model: %.2f)", c.ratio,
               paramTable ().toText (kCurve, c.curve).c_str (), c.level, gr, c.gr);
    }
}

TEST (output_stage_is_the_models)
{
    // a quiet 1 kHz sine (no compression): the whole gain, the compressor's 1.52 dB at rest included, as the
    // model gave it over Drive and Feedback
    struct Case
    {
        double drive, feedback, gain;
    };
    const Case cases[] = {{5, 5, -2.43}, {0, 5, -16.85}, {10, 10, 18.84}, {10, 0, -36.88}, {5, 10, 3.44}, {8, 7, 7.76}, {2, 3, -14.52}};
    for (const auto& c : cases)
    {
        auto e = engine ([&c] (Engine& e) { e.setModelPositions (4.0, c.drive, c.feedback); });
        const double g = gainAt (*e, 1000.0, -60.0, 1.0);
        CHECK (std::fabs (g - c.gain) < 0.35, "Drive %.0f, Feedback %.0f: %.2f dB at 1 kHz (the model: %.2f)", c.drive, c.feedback, g, c.gain);
    }
    // Feedback's bass: at Drive 5, Feedback 10 the model's bump is about 7 dB above 1 kHz at 30 to 40 Hz, and
    // falls away steeply below
    auto e = engine ([] (Engine& e) { e.setModelPositions (4.0, 5.0, 10.0); });
    const double g1k = gainAt (*e, 1000.0, -60.0, 1.0);
    e->reset ();
    const double g35 = gainAt (*e, 35.0, -60.0, 3.0);
    e->reset ();
    const double g10 = gainAt (*e, 10.0, -60.0, 6.0);
    CHECK (g35 - g1k > 5.5 && g35 - g1k < 9.0, "35 Hz %+.1f dB against 1 kHz", g35 - g1k);
    CHECK (g10 - g1k < -12.0, "10 Hz %+.1f dB against 1 kHz", g10 - g1k);
}

TEST (mix_blends_before_the_output_stage)
{
    // at rest the uncompressed path is 1.52 dB louder than the compressed one; Mix blends them linearly
    double g[3];
    const double mixes[3] = {1.0, 0.5, 0.0};
    for (int k = 0; k < 3; ++k)
    {
        auto e = engine ([&] (Engine& e) { e.setParam (kMix, mixes[k]); });
        g[k] = gainAt (*e, 1000.0, -60.0, 1.0);
    }
    CHECK (std::fabs ((g[2] - g[0]) - 1.52) < 0.05, "Mix 0 against 100 %%: %+.2f dB", g[2] - g[0]);
    const double half = db (0.5 * std::pow (10.0, g[0] / 20.0) + 0.5 * std::pow (10.0, g[2] / 20.0));
    CHECK (std::fabs (g[1] - half) < 0.05, "Mix 50 %%: %.2f dB (linear blend %.2f)", g[1], half);
    // deep compression: Mix 0 is untouched by it
    auto mix0 = [] (Engine& e) {
        e.setParam (kMix, 0.0);
        e.setParam (kSaturation, 0.0);
    };
    auto a = engine (mix0), b = engine (mix0);
    const double quiet = gainAt (*a, 1000.0, -60.0), loud = gainAt (*b, 1000.0, -6.0);
    CHECK (std::fabs (loud - quiet) < 0.1, "Mix 0: %.2f dB at -6 dBFS against -60 dBFS", loud - quiet);
}

TEST (the_fet_adds_a_second_harmonic)
{
    // with a silent sidechain (Ext SC) nothing compresses, so nothing ripples; Saturation off: the FET's second
    // harmonic (pinchr's Dirty, always in now) is k A / 2 (k 0.11 at rest) at -12 dBFS, as the model's was; Dirty's
    // old switch (retired) changes nothing
    const double amp = std::pow (10.0, -12.0 / 20.0);
    const auto x = sine (220.0, 1.0, amp);
    const std::vector<float> silent (x.size (), 0.0f);
    for (bool dirty : {false, true})
    {
        auto e = engine ([dirty] (Engine& e) {
            e.setParam (kExtSc, 1.0);
            e.setParam (kDirty, dirty ? 0.0 : 1.0);
            e.setParam (kSaturation, 0.0); // (the FET alone)
        });
        std::vector<float> l (x.size ()), r (x.size ());
        for (size_t a = 0; a < x.size (); a += 480)
        {
            const int n = (int)std::min<size_t> (480, x.size () - a);
            e->process (x.data () + a, x.data () + a, l.data () + a, r.data () + a, n, silent.data () + a, silent.data () + a);
        }
        const size_t a = l.size () - (size_t)(0.5 * kSr);
        const double h1 = toneLevel (l, a, l.size (), 220.0), h2 = toneLevel (l, a, l.size (), 440.0), h3 = toneLevel (l, a, l.size (), 660.0);
        const double want = db (0.11 * amp / 2.0);
        CHECK (std::fabs (db (h2 / h1) - want) < 1.0, "H2 %.1f dB (k A / 2 = %.1f)", db (h2 / h1), want);
        CHECK (db (h3 / h1) < -80.0, "no H3 (%.1f dB)", db (h3 / h1));
        CHECK (paramTable ().info (kDirty).retired, "Dirty retired");
    }
}

TEST (saturation_is_germanium)
{
    // 0 %: the measured linear stage exactly; quiet signals pass the same at any Saturation; a hot 100 Hz sine
    // gets mostly second harmonic, more with more Saturation, and at the same output level more with high
    // Feedback (little feedback) than low; the stage holds its headroom
    auto h = [] (double sat, double fb, double drive, double level, double& h2, double& h3) {
        auto e = engine ([&] (Engine& e) {
            e.setParam (kSaturation, sat);
            e.setModelPositions (4.0, drive, fb);
            e.setParam (kExtSc, 1.0); // (a silent sidechain: no compression)
        });
        const double amp = std::pow (10.0, level / 20.0);
        const auto x = sine (100.0, 1.0, amp);
        const std::vector<float> silent (x.size (), 0.0f);
        std::vector<float> l (x.size ()), r (x.size ());
        for (size_t a = 0; a < x.size (); a += 480)
        {
            const int n = (int)std::min<size_t> (480, x.size () - a);
            e->process (x.data () + a, x.data () + a, l.data () + a, r.data () + a, n, silent.data () + a, silent.data () + a);
        }
        const size_t a = l.size () - (size_t)(0.5 * kSr);
        const double h1 = toneLevel (l, a, l.size (), 100.0);
        h2 = db (toneLevel (l, a, l.size (), 200.0) / h1);
        h3 = db (toneLevel (l, a, l.size (), 300.0) / h1);
        return db (h1 / amp);
    };
    double h2, h3, h2b, h3b;
    h (0.0, 5.0, 5.0, -6.0, h2, h3);
    const double fet = db (0.11 * std::pow (10.0, -6.0 / 20.0) / 2.0); // (the FET's own, the_fet_adds_a_second_harmonic)
    CHECK (std::fabs (h2 - fet) < 1.5 && h3 < -90.0, "0 %%: the FET's H2 alone (%.1f, want %.1f), H3 %.1f dB", h2, fet, h3);
    const double gQuietOff = h (0.0, 5.0, 5.0, -50.0, h2, h3), gQuietOn = h (1.0, 5.0, 5.0, -50.0, h2, h3);
    CHECK (std::fabs (gQuietOn - gQuietOff) < 0.05, "a quiet sine: %.2f dB at 100 %% against 0 %%", gQuietOn - gQuietOff);
    h (0.5, 5.0, 5.0, -6.0, h2, h3);
    h (1.0, 5.0, 5.0, -6.0, h2b, h3b);
    CHECK (h2 > -60.0 && h2 > h3 && h2b > h2 + 6.0, "-6 dBFS: H2 %.1f (H3 %.1f) at 50 %%, H2 %.1f at 100 %%", h2, h3, h2b);
    // Feedback 2 against 9 at the same level out of the stage (Drive compensates the gain)
    const double gLow = h (0.0, 2.0, 9.0, -50.0, h2, h3), gHigh = h (0.0, 9.0, 4.0, -50.0, h2, h3);
    h (0.75, 2.0, 9.0, -20.0 - gLow, h2, h3);
    h (0.75, 9.0, 4.0, -20.0 - gHigh, h2b, h3b);
    CHECK (h2b > h2 + 6.0, "-20 dBFS out: H2 %.1f dB at Feedback 9, %.1f at Feedback 2", h2b, h2);
    // very hot: bounded near the headroom
    for (double pos : {10.0, 15.0})
    {
        auto e = engine ([pos] (Engine& e) {
            e.setParam (kSaturation, 1.0);
            e.setModelPositions (4.0, pos, pos);
        });
        const auto y = run (*e, sine (100.0, 1.0, 1.0));
        double peak = 0.0;
        for (size_t i = 24000; i < y.size (); ++i)
            peak = std::max (peak, (double)std::fabs (y[i]));
        CHECK (finite (y) && peak < 1.0, "Drive and Feedback at %.0f, 100 %%: peak %.2f", pos, peak);
    }
}

TEST (link_and_mid_side)
{
    // a loud left and a quiet right: linked, both get the left's gain reduction; unlinked, the right none
    const auto loud = sine (1000.0, 1.5, 0.5), quiet = sine (1000.0, 1.5, 0.003);
    for (bool link : {true, false})
    {
        auto e = engine ([link] (Engine& e) { e.setParam (kLink, link ? 1.0 : 0.0); });
        std::vector<float> r;
        const auto l = run (*e, loud, &r, 480, &quiet);
        const size_t a = l.size () - (size_t)(0.3 * kSr);
        const double gl = db (toneLevel (l, a, l.size (), 1000.0) / 0.5), gr = db (toneLevel (r, a, r.size (), 1000.0) / 0.003);
        if (link)
            CHECK (std::fabs (gl - gr) < 0.2, "linked: left %.1f dB, right %.1f dB", gl, gr);
        else
            CHECK (gr - gl > 10.0, "unlinked: left %.1f dB, right %.1f dB", gl, gr);
    }
    // Mid/Side: a mono input has no side, so both channels come out the same
    auto e = engine ([] (Engine& e) {
        e.setParam (kMode, kMidSide);
        e.setParam (kLink, 0.0);
    });
    std::vector<float> r;
    const auto l = run (*e, bassLine (1.0), &r);
    double diff = 0.0;
    for (size_t i = 0; i < l.size (); ++i)
        diff = std::max (diff, (double)std::fabs (l[i] - r[i]));
    CHECK (diff < 1e-5, "Mid/Side, mono in: left and right differ by %.2g", diff);
}

TEST (ext_sc_listens_to_the_sidechain)
{
    // a quiet 3 kHz tone compressed by a loud 1 kHz sidechain: Ext SC on, it follows the sidechain; off (or no
    // sidechain), it ignores it; SC Gain -24 dB takes most of it away
    const auto probe = sine (3000.0, 1.5, 0.003), key = sine (1000.0, 1.5, 0.5);
    auto gainWith = [&] (bool ext, double scGain, bool connect) {
        auto e = engine ([&] (Engine& e) {
            e.setParam (kExtSc, ext ? 1.0 : 0.0);
            e.setParam (kScGain, scGain);
        });
        std::vector<float> l (probe.size ()), r (probe.size ());
        for (size_t a = 0; a < probe.size (); a += 480)
        {
            const int n = (int)std::min<size_t> (480, probe.size () - a);
            e->process (probe.data () + a, probe.data () + a, l.data () + a, r.data () + a, n, connect ? key.data () + a : nullptr,
                        connect ? key.data () + a : nullptr);
        }
        return db (toneLevel (l, l.size () - (size_t)(0.3 * kSr), l.size (), 3000.0) / 0.003);
    };
    const double off = gainWith (false, 0.0, true), on = gainWith (true, 0.0, true), none = gainWith (true, 0.0, false),
                 low = gainWith (true, -24.0, true);
    // the model at Ratio 4, Germ Soft: a -6 dBFS sine reduces the gain by about 16 dB
    CHECK (off - on > 13.0 && off - on < 19.0, "Ext SC: %.1f dB of gain reduction from the sidechain", off - on);
    CHECK (std::fabs (none - off) < 0.05, "Ext SC with no sidechain routed: %.2f dB", none - off);
    CHECK (off - low < 3.0, "SC Gain -24 dB: %.1f dB of gain reduction", off - low);
}

TEST (latency_is_reported)
{
    // an impulse comes out at the latency the engine reports (the output stage's response peaks at once)
    auto e = engine ([] (Engine& e) { e.setModelPositions (4.0, 5.0, 0.0); });
    const int lat = e->latency ();
    CHECK (lat > 0 && lat < 300, "latency %d", lat);
    std::vector<float> x ((size_t)kSr, 0.0f);
    x[1000] = 0.01f;
    const auto y = run (*e, x);
    size_t peak = 0;
    for (size_t i = 0; i < y.size (); ++i)
        if (std::fabs (y[i]) > std::fabs (y[peak]))
            peak = i;
    CHECK ((int)peak - 1000 == lat, "the impulse at %d, the latency %d", (int)peak - 1000, lat);
}

TEST (switches_do_not_blow_up)
{
    // every choice and switch moved every 0.1 s on a hot bass: finite and bounded
    auto e = engine ();
    const auto x = bassLine (3.0, 3.0);
    std::vector<float> y (x.size ()), r (x.size ());
    for (size_t a = 0; a < x.size (); a += 480)
    {
        const int k = (int)(a / 4800);
        e->setParam (kCurve, k % kNumCurves);
        e->setParam (kScFilter, k % kNumScFilters);
        e->setParam (kLink, (k / 2) % 2);
        e->setParam (kMode, (k / 3) % 2);
        e->setParam (kDrive, 1 + k % 10);
        e->setParam (kFeedback, 1 + (k * 3) % 10);
        e->setParam (kRatio, 1.0 + (k * 7) % 10);
        e->setParam (kBass, (k % 3) * 0.5);
        e->setParam (kBassOversample, (k / 4) % 3);
        e->process (x.data () + a, x.data () + a, y.data () + a, r.data () + a, 480);
    }
    CHECK (finite (y) && finite (r), "finite");
    double peak = 0.0;
    for (float v : y)
        peak = std::max (peak, (double)std::fabs (v));
    CHECK (peak < 30.0, "peak %.2f", peak);
}

TEST (steps_span_the_positions)
{
    // Drive and Feedback: steps 1 .. 10 over pinchr's positions 1 .. 15; Ratio 1 .. 10 (pinchr's 1 .. 10)
    CHECK (stagePosition (1) == 1.0 && stagePosition (10) == 15.0 && std::fabs (stagePosition (4) - (1.0 + 3.0 * 14.0 / 9.0)) < 1e-12,
           "positions %f %f", stagePosition (1), stagePosition (10));
    CHECK (stepNearest (5.0) == 4 && stepNearest (0.0) == 1 && stepNearest (-3.0) == 1 && stepNearest (10.0) == 7 && stepNearest (15.0) == 10 &&
               stepNearest (20.0) == 10,
           "the nearest steps");
    const auto& t = paramTable ();
    CHECK (t.info (kDrive).min == 1.0 && t.info (kDrive).max == 10.0 && t.info (kFeedback).min == 1.0 && t.info (kFeedback).max == 10.0 &&
               t.info (kRatio).min == 1.0 && t.info (kRatio).max == 10.0 && t.info (kDrive).stepCount () == 9 && t.info (kFeedback).stepCount () == 9 &&
               t.info (kRatio).stepCount () == 9,
           "1 .. 10 in whole steps");
    // a step is its position: the same sound as the position set directly
    for (int step : {1, 4, 10})
    {
        auto a = engine ([step] (Engine& e) {
            e.setParam (kDrive, step);
            e.setParam (kFeedback, step);
        });
        auto b = engine ([step] (Engine& e) { e.setModelPositions (4.0, stagePosition (step), stagePosition (step)); });
        const auto x = bassLine (0.5);
        const auto ya = run (*a, x), yb = run (*b, x);
        CHECK (ya == yb, "step %d is position %.3f", step, stagePosition (step));
    }
    // the defaults: Drive 4 and Feedback 3 (pinchr's 5 / 5 the nearest in level and tone: within 0.5 dB at 1 kHz)
    auto now = engine (), old = engine ([] (Engine& e) { e.setModelPositions (4.0, 5.0, 5.0); });
    const double gNow = gainAt (*now, 1000.0, -60.0, 1.0), gOld = gainAt (*old, 1000.0, -60.0, 1.0);
    CHECK (t.info (kDrive).def == 4.0 && t.info (kFeedback).def == 3.0 && t.info (kRatio).def == 4.0 && std::fabs (gNow - gOld) < 0.5,
           "the defaults: %.2f dB at 1 kHz against pinchr's %.2f", gNow, gOld);
}

TEST (the_top_steps_are_stable)
{
    // above the measured 10 the stage is extrapolated: its sections stay inside the table's span, the gain grows
    // with Drive and Feedback, and the engine stays finite and bounded at the top with any Saturation
    double last = -1e9;
    for (double pos = 10.0; pos <= 15.0; pos += 0.5)
    {
        const StageParams s = stageAt (pos, pos);
        const double f[5] = {s.fa, s.fb, s.fc, s.fz, s.fh}, q[5] = {s.qa, s.qb, s.qc, s.qz, s.qh};
        bool inside = true;
        for (int k = 0; k < 5; ++k)
            inside = inside && f[k] >= 3.0 && f[k] <= 40000.0 && q[k] >= 0.1 && q[k] <= 2.5 && std::isfinite (f[k]) && std::isfinite (q[k]);
        const double g = std::abs (stageResponse (s, 1000.0));
        CHECK (inside && g > last, "position %.1f: sections inside the span, the gain up (%.2f dB)", pos, 20.0 * std::log10 (g));
        last = g;
        // the response's peak stays near the measured top's (no section runs away)
        double peak = 0.0;
        for (double hz = 10.0; hz < 20000.0; hz *= 1.05)
            peak = std::max (peak, std::abs (stageResponse (s, hz)));
        CHECK (20.0 * std::log10 (peak / g) < 10.0, "position %.1f: peak %.1f dB over 1 kHz", pos, 20.0 * std::log10 (peak / g));
    }
    // the top steps with a hot input: finite; with Saturation, held near the stage's headroom
    for (double sat : {0.0, 0.5, 1.0})
    {
        auto e = engine ([sat] (Engine& e) {
            e.setParam (kDrive, 10);
            e.setParam (kFeedback, 10);
            e.setParam (kSaturation, sat);
            e.setParam (kInput, 12.0);
        });
        const auto y = run (*e, bassLine (2.0, 2.0));
        double peak = 0.0;
        for (size_t i = 24000; i < y.size (); ++i)
            peak = std::max (peak, (double)std::fabs (y[i]));
        CHECK (finite (y) && (sat == 0.0 ? peak <= 64.0 : peak < 2.0), "Drive 10, Feedback 10, Saturation %.0f %%: peak %.2f", sat * 100, peak);
    }
}

TEST (bass_zero_is_the_compressor_alone)
{
    // Bass 0: the enhancer does not run (whatever its other settings), the sound is the same to the bit
    const auto x = bassLine (1.0);
    auto plain = engine ();
    auto set = engine ([] (Engine& e) {
        e.setParam (kBassBoost, 1.1);
        e.setParam (kBassFreq, 90.0);
        e.setParam (kBassIntensity, 1.0);
        e.setParam (kBassGate, -60.0);
        e.setParam (kBassLowCut, 80.0);
    });
    const auto a = run (*plain, x), b = run (*set, x);
    CHECK (a == b && !set->bassRan () && set->latency () == plain->latency (), "Bass 0: the same output, nothing of the enhancer ran");
    // Bass up: it runs, the low end grows (a quiet 55 Hz sine, uncompressed: Mix 0), the latency is the same
    auto quiet = engine ([] (Engine& e) { e.setParam (kMix, 0.0); });
    auto up = engine ([] (Engine& e) {
        e.setParam (kMix, 0.0);
        e.setParam (kBass, 1.0);
        e.setParam (kBassFreq, 55.0);
        e.setParam (kBassGate, -60.0);
    });
    const auto tone = sine (55.0, 1.5, 0.03);
    const auto q0 = run (*quiet, tone), c = run (*up, tone);
    const size_t from = c.size () / 2;
    const double g0 = db (toneLevel (q0, from, q0.size (), 55.0)), g1 = db (toneLevel (c, from, c.size (), 55.0));
    CHECK (up->bassRan () && up->latency () == plain->latency () && g1 > g0 + 6.0, "Bass 100 %%: 55 Hz at %.1f dB against %.1f", g1, g0);
    run (*up, x);
    // turned back to 0: it glides out, then stops running, and the sound is the compressor's alone again
    up->setParam (kBass, 0.0);
    run (*up, bassLine (0.5));
    CHECK (!up->bassRan (), "back at 0: it stopped");
    // Bass Oversample: its oversampler's latency on top (2x, 4x), reported while it is held; at Bass 0 the sound is
    // the compressor's delayed by it, exactly
    for (int os : {1, 2})
    {
        auto e = engine ([os] (Engine& e) { e.setParam (kBassOversample, os); });
        const int extra = e->latency () - plain->latency ();
        CHECK (extra > 0 && extra < 200, "Bass Oversample %s: %d samples more", os == 1 ? "2x" : "4x", extra);
        const auto y = run (*e, x);
        bool same = true;
        for (size_t i = (size_t)extra; i < y.size (); ++i)
            same = same && y[i] == a[i - (size_t)extra];
        CHECK (same && !e->bassRan (), "Bass Oversample %s at Bass 0: the compressor's sound, %d samples later", os == 1 ? "2x" : "4x", extra);
    }
}

TEST (recovers_from_nan)
{
    auto e = engine ();
    auto x = bassLine (1.0);
    x[1000] = std::numeric_limits<float>::quiet_NaN ();
    x[1001] = std::numeric_limits<float>::infinity ();
    const auto y = run (*e, x);
    CHECK (finite (std::vector<float> (y.begin () + 24000, y.end ())), "finite again after a NaN in the input");
}

TEST (cpu_budget)
{
    // 10 s of stereo bass: the defaults, and the same with Soft Clip and Hard Clip on and the bass enhancer at
    // 100 % (4x); the best of three
    auto x = bassLine (10.0);
    for (bool heavy : {false, true})
    {
        double secs = 1e9;
        std::vector<float> l;
        for (int i = 0; i < 3; ++i)
        {
            auto e = engine ([heavy] (Engine& e) {
                if (heavy)
                {
                    e.setParam (kSoftClip, 1.0);
                    e.setParam (kHardClip, 1.0);
                    e.setParam (kBass, 1.0);
                    e.setParam (kBassOversample, kBassOs4x);
                }
            });
            const pk::testing::CpuClock t0 = pk::testing::cpuClock ();
            l = run (*e, x, nullptr, 512);
            secs = std::min (secs, (double)(pk::testing::cpuClock () - t0) / pk::testing::kCpuClocksPerSec);
        }
        CHECK (finite (l), "finite");
        std::printf ("    CPU: %.2f%% of one core (%s)\n", 100.0 * secs / 10.0, heavy ? "the clips on, Bass 100 % at 4x" : "the defaults");
        CHECK (secs / 10.0 < (heavy ? 0.25 : 0.08), "too slow");
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
