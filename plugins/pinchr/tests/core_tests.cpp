// Headless tests for the Pinchr DSP. Run: ./pinchr_tests [filter]
// Against the Softube model's measured numbers (germcal): the static gain reduction at two settings, the
// output stage's gain over Drive and Feedback and its low bump, the Mix law; then Dirty's second harmonic,
// Link, Mid/Side, the latency, switches, a NaN, and the CPU budget.
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

using namespace pinchr;

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
std::vector<float> bass (double seconds, double gain = 1.0)
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
            e.setParam (kRatio, c.ratio);
            e.setParam (kCurve, c.curve);
            e.setParam (kAttack, 3.0);
            e.setParam (kRelease, 0.0);
            e.setParam (kDrive, 0.0);
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
        auto e = engine ([&c] (Engine& e) {
            e.setParam (kDrive, c.drive);
            e.setParam (kFeedback, c.feedback);
        });
        const double g = gainAt (*e, 1000.0, -60.0, 1.0);
        CHECK (std::fabs (g - c.gain) < 0.35, "Drive %.0f, Feedback %.0f: %.2f dB at 1 kHz (the model: %.2f)", c.drive, c.feedback, g, c.gain);
    }
    // Feedback's bass: at Drive 5, Feedback 10 the model's bump is about 7 dB above 1 kHz at 30 to 40 Hz, and
    // falls away steeply below
    auto e = engine ([] (Engine& e) { e.setParam (kFeedback, 10.0); });
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

TEST (dirty_adds_a_second_harmonic)
{
    // with a silent sidechain (Ext SC) nothing compresses, so nothing ripples; Saturation off: Dirty's second harmonic is
    // k A / 2 (k 0.11 at rest) at -12 dBFS, as the model's was, and Clean has none
    const double amp = std::pow (10.0, -12.0 / 20.0);
    const auto x = sine (220.0, 1.0, amp);
    const std::vector<float> silent (x.size (), 0.0f);
    for (bool dirty : {false, true})
    {
        auto e = engine ([dirty] (Engine& e) {
            e.setParam (kExtSc, 1.0);
            e.setParam (kDirty, dirty ? 1.0 : 0.0);
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
        if (dirty)
        {
            const double want = db (0.11 * amp / 2.0);
            CHECK (std::fabs (db (h2 / h1) - want) < 1.0, "Dirty: H2 %.1f dB (k A / 2 = %.1f)", db (h2 / h1), want);
            CHECK (db (h3 / h1) < -80.0, "Dirty: no H3 (%.1f dB)", db (h3 / h1));
        }
        else
            CHECK (db (h2 / h1) < -90.0 && db (h3 / h1) < -90.0, "Clean: H2 %.1f dB, H3 %.1f dB", db (h2 / h1), db (h3 / h1));
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
            e.setParam (kFeedback, fb);
            e.setParam (kDrive, drive);
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
    CHECK (h2 < -90.0 && h3 < -90.0, "0 %%: H2 %.1f, H3 %.1f dB", h2, h3);
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
    auto e = engine ([] (Engine& e) {
        e.setParam (kSaturation, 1.0);
        e.setParam (kDrive, 10.0);
        e.setParam (kFeedback, 10.0);
    });
    const auto y = run (*e, sine (100.0, 1.0, 1.0));
    double peak = 0.0;
    for (size_t i = 24000; i < y.size (); ++i)
        peak = std::max (peak, (double)std::fabs (y[i]));
    CHECK (finite (y) && peak < 1.0, "Drive 10, Feedback 10, 100 %%: peak %.2f", peak);
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
    const auto l = run (*e, bass (1.0), &r);
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
    auto e = engine ([] (Engine& e) { e.setParam (kFeedback, 0.0); });
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
    const auto x = bass (3.0, 3.0);
    std::vector<float> y (x.size ()), r (x.size ());
    for (size_t a = 0; a < x.size (); a += 480)
    {
        const int k = (int)(a / 4800);
        e->setParam (kCurve, k % kNumCurves);
        e->setParam (kScFilter, k % kNumScFilters);
        e->setParam (kDirty, k % 2);
        e->setParam (kLink, (k / 2) % 2);
        e->setParam (kMode, (k / 3) % 2);
        e->setParam (kDrive, k % 11);
        e->setParam (kFeedback, (k * 3) % 11);
        e->setParam (kRatio, 1.0 + (k * 7) % 11);
        e->process (x.data () + a, x.data () + a, y.data () + a, r.data () + a, 480);
    }
    CHECK (finite (y) && finite (r), "finite");
    double peak = 0.0;
    for (float v : y)
        peak = std::max (peak, (double)std::fabs (v));
    CHECK (peak < 30.0, "peak %.2f", peak);
}

TEST (recovers_from_nan)
{
    auto e = engine ();
    auto x = bass (1.0);
    x[1000] = std::numeric_limits<float>::quiet_NaN ();
    x[1001] = std::numeric_limits<float>::infinity ();
    const auto y = run (*e, x);
    CHECK (finite (std::vector<float> (y.begin () + 24000, y.end ())), "finite again after a NaN in the input");
}

TEST (cpu_budget)
{
    // 10 s of stereo bass: the defaults, and the same with the end saturator on; the best of three
    auto x = bass (10.0);
    for (bool heavy : {false, true})
    {
        double secs = 1e9;
        std::vector<float> l;
        for (int i = 0; i < 3; ++i)
        {
            auto e = engine ([heavy] (Engine& e) {
                if (heavy)
                    e.setParam ((uint32_t)kTailBase + pk::kTailOn, 1.0);
            });
            const pk::testing::CpuClock t0 = pk::testing::cpuClock ();
            l = run (*e, x, nullptr, 512);
            secs = std::min (secs, (double)(pk::testing::cpuClock () - t0) / pk::testing::kCpuClocksPerSec);
        }
        CHECK (finite (l), "finite");
        std::printf ("    CPU: %.2f%% of one core (%s)\n", 100.0 * secs / 10.0, heavy ? "the defaults, the end saturator on" : "the defaults");
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
