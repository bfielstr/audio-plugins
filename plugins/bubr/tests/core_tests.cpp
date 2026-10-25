// Headless tests for the Bubr DSP. Run: ./bubr_tests [filter]
// Against Uberbass's measured numbers (bubrcal): the effect's gain at Freq for several Freq, Q-Factor,
// Intensity, Boost and Drive settings, its level when it clips, the Dry/Wet law, the gate's curve and its
// release, the output limiter; then the latency and the dry path's alignment, Output, the clips, a NaN and the
// CPU budget.
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

using namespace bubr;

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


// the effect alone (Dry/Wet +100 %), the gate open
void wetOnly (Engine& e)
{
    e.setParam (kDryWet, 1.0);
    e.setParam (kGate, -60.0);
    e.setParam (kRatio, 1.0);
}

} // namespace

TEST (linear_gain_is_the_references)
{
    // a sine, -60 dBFS (well under the clipper): the effect's gain, as Uberbass gave it (the model is within about 0.5 dB:
    // at Boost 110 the compressor already takes a little of a quiet band)
    struct Case
    {
        double freq, q, intensity, boost, drive;
        bool x2;
        double hz, gainDb;
    };
    const Case cases[] = {
        {40, 0.2, 0.5, 0.55, 0.2, false, 40, 25.57},  {40, 0.2, 0.0, 0.55, 0.2, false, 40, -0.97}, {40, 0.2, 1.0, 0.55, 0.2, false, 40, 35.12},
        {40, 0.2, 0.5, 0.0, 0.2, false, 40, 9.07},    {40, 0.2, 0.5, 1.1, 0.2, false, 40, 42.06},  {40, 0.2, 0.5, 0.55, 1.0, false, 40, 29.81},
        {40, 0.2, 0.5, 0.55, 1.0, true, 40, 33.73},   {80, 1.0, 0.5, 0.55, 0.2, false, 80, 35.5},  {80, 0.0, 0.5, 0.55, 0.2, false, 80, 20.9},
        {40, 0.2, 0.5, 0.55, 0.2, false, 14, 0.0},    {40, 0.2, 0.5, 0.55, 0.2, false, 113, 13.0}, {40, 0.2, 0.5, 0.55, 0.2, false, 226, -5.6},
    };
    for (const auto& c : cases)
    {
        auto e = engine ([&] (Engine& e) {
            wetOnly (e);
            e.setParam (kFreq, c.freq);
            e.setParam (kQFactor, c.q);
            e.setParam (kIntensity, c.intensity);
            e.setParam (kBoost, c.boost);
            e.setParam (kDrive, c.drive);
            e.setParam (kDriveX2, c.x2 ? 1.0 : 0.0);
        });
        const double g = gainAt (*e, c.hz, -60.0, 2.0);
        CHECK (std::fabs (g - c.gainDb) < 0.6, "Freq %.0f Q %.1f Int %.1f Boost %.2f Drive %.1f%s at %.0f Hz: %.2f dB (the reference: %.2f)",
               c.freq, c.q, c.intensity, c.boost, c.drive, c.x2 ? " x2" : "", c.hz, g, c.gainDb);
    }
}

TEST (clipping_is_the_references)
{
    // a 40 Hz sine at Freq 40 into the clipper: the fundamental out (dBFS), as Uberbass gave it
    struct Case
    {
        double intensity, boost, levelDb, outDb, tol;
    };
    const Case cases[] = {{0.5, 0.55, -36.0, -11.1, 1.5}, {0.5, 0.55, -24.0, -8.3, 1.0}, {0.5, 0.55, 0.0, -7.2, 1.0},
                          {0.0, 0.55, -6.0, -10.0, 1.0},  {0.0, 0.55, 12.0, -8.0, 1.0},  {0.5, 0.0, 0.0, -15.3, 1.0}};
    for (const auto& c : cases)
    {
        auto e = engine ([&] (Engine& e) {
            wetOnly (e);
            e.setParam (kIntensity, c.intensity);
            e.setParam (kBoost, c.boost);
        });
        const double out = gainAt (*e, 40.0, c.levelDb, 1.5) + c.levelDb;
        CHECK (std::fabs (out - c.outDb) < c.tol, "Intensity %.1f Boost %.2f, %.0f dBFS in: %.2f dBFS out (the reference: %.2f)", c.intensity,
               c.boost, c.levelDb, out, c.outDb);
    }
}

TEST (dry_wet_law)
{
    // -100 %: the input exactly; 0 %: 1 kHz passes at unity (the effect does not reach it, Low Cut is far below)
    {
        auto e = engine ([] (Engine& e) {
            e.setParam (kDryWet, -1.0);
            e.setParam (kLookahead, 0.0);
        });
        const auto x = sine (1000.0, 0.5, 0.25);
        const auto y = run (*e, x);
        double err = 0.0;
        for (size_t i = 0; i < x.size (); ++i)
            err = std::max (err, (double)std::fabs (y[i] - x[i]));
        CHECK (err < 1e-6, "dry only: the input (%.2g off)", err);
    }
    {
        auto e = engine ();
        const double g = gainAt (*e, 1000.0, -20.0);
        CHECK (std::fabs (g) < 0.05, "0 %%: 1 kHz at %.3f dB", g);
    }
    // +50 %: the dry 6.8 dB down at 1 kHz (the reference's 0.458)
    {
        auto e = engine ([] (Engine& e) { e.setParam (kDryWet, 0.5); });
        const double g = gainAt (*e, 1000.0, -20.0);
        CHECK (std::fabs (g + 6.78) < 0.1, "+50 %%: 1 kHz at %.2f dB", g);
    }
}

TEST (gate_curve_and_release)
{
    // Gate -30 dB, Ratio 4: a 1 kHz sine at -36 dBFS sets the gate 18 dB down, at -24 open
    for (double lv : {-36.0, -33.0, -24.0})
    {
        auto e = engine ();
        run (*e, sine (1000.0, 0.5, std::pow (10.0, lv / 20.0)));
        const double g = db (e->gateGain (0)), want = lv < -30.0 ? 3.0 * (lv + 30.0) : 0.0;
        CHECK (std::fabs (g - want) < 0.3, "%.0f dBFS: the gate at %.2f dB (want %.1f)", lv, g, want);
    }
    // Release 100 ms: the input stops; 100 ms later the gate's gain is down to about e^-1 of open
    {
        auto e = engine ();
        auto x = sine (1000.0, 1.0, 0.3);
        x.resize ((size_t)(1.1 * kSr), 0.0f);
        run (*e, x);
        const double g = e->gateGain (0);
        CHECK (std::fabs (g - std::exp (-1.0)) < 0.03, "100 ms after: %.3f", g);
    }
}

TEST (output_limiter)
{
    // dry only, a 1 kHz sine at +12 dBFS: nothing over 0 dBFS, held at it (both Lookahead settings)
    for (double la : {0.0, 1.0})
    {
        auto e = engine ([la] (Engine& e) {
            e.setParam (kDryWet, -1.0);
            e.setParam (kLookahead, la);
        });
        const auto y = run (*e, sine (1000.0, 1.0, 4.0));
        float peak = 0.0f, late = 0.0f;
        for (size_t i = 0; i < y.size (); ++i)
        {
            peak = std::max (peak, std::fabs (y[i]));
            if (i > y.size () / 2)
                late = std::max (late, std::fabs (y[i]));
        }
        CHECK (peak <= 1.0f && late > 0.98f, "Lookahead %.0f: peak %.4f, held at %.4f", la, peak, late);
    }
    // Output follows it: -12 dB
    {
        auto e = engine ([] (Engine& e) {
            e.setParam (kDryWet, -1.0);
            e.setParam (kOutput, -12.0);
        });
        const auto y = run (*e, sine (1000.0, 1.0, 2.0));
        float late = 0.0f;
        for (size_t i = y.size () / 2; i < y.size (); ++i)
            late = std::max (late, std::fabs (y[i]));
        CHECK (std::fabs (db (late) + 12.0) < 0.1, "Output -12 dB after the limiter: %.2f dBFS", db (late));
    }
}

TEST (latency_and_alignment)
{
    for (int os : {0, 1, 2})
        for (double la : {0.0, 1.0})
        {
            auto e = engine ([&] (Engine& e) {
                e.setParam (kOversample, os);
                e.setParam (kLookahead, la);
                e.setParam (kDryWet, -1.0);
            });
            const int lat = e->latency ();
            std::vector<float> x ((size_t)kSr / 4, 0.0f);
            x[1000] = 0.5f;
            const auto y = run (*e, x);
            size_t at = 0;
            for (size_t i = 0; i < y.size (); ++i)
                if (std::fabs (y[i]) > std::fabs (y[at]))
                    at = i;
            CHECK ((int)at == 1000 + lat && std::fabs (y[at] - 0.5f) < 1e-4, "Oversample %d Lookahead %.0f: latency %d, the dry at %d", os,
                   la, lat, (int)at - 1000);
            if (os == 0)
                CHECK (lat == (la > 0.5 ? 48 : 0), "latency %d", lat);
        }
}

TEST (output_and_clips)
{
    {
        auto e = engine ([] (Engine& e) { e.setParam (kOutput, -60.0); });
        const auto y = run (*e, bass (0.5));
        double s = 0.0;
        for (float v : y)
            s += std::fabs (v);
        CHECK (s == 0.0, "Output -60 dB: silence");
    }
    {
        // the clips: a dry sine at 0.4 passes both untouched
        auto e = engine ([] (Engine& e) {
            e.setParam (kDryWet, -1.0);
            e.setParam (kLookahead, 0.0);
            e.setParam (kSoftClip, 1.0);
            e.setParam (kHardClip, 1.0);
        });
        const auto x = sine (1000.0, 0.2, 0.4);
        const auto y = run (*e, x);
        double err = 0.0;
        for (size_t i = 0; i < x.size (); ++i)
            err = std::max (err, (double)std::fabs (y[i] - x[i]));
        CHECK (err < 1e-6, "Soft Clip and Hard Clip leave 0.4 alone (%.3g off)", err);
    }
    {
        // Soft Clip rounds a hot one off (Output down after the limiter: 0.8 peaks, over the knee)
        auto e = engine ([] (Engine& e) {
            e.setParam (kDryWet, -1.0);
            e.setParam (kLookahead, 0.0);
            e.setParam (kSoftClip, 1.0);
        });
        const auto y = run (*e, sine (1000.0, 0.2, 0.8));
        float peak = 0.0f;
        for (float v : y)
            peak = std::max (peak, std::fabs (v));
        CHECK (std::fabs (peak - 0.755f) < 0.01f, "Soft Clip: 0.8 in, %.3f out", peak);
    }
}

TEST (nan_recovers)
{
    auto e = engine ();
    auto x = bass (0.5);
    x[2000] = std::numeric_limits<float>::quiet_NaN ();
    const auto y = run (*e, x);
    CHECK (std::isfinite (y.back ()), "finite again after a NaN");
}

TEST (cpu_budget)
{
    // 10 s of stereo bass: the defaults, and 4x; the best of three
    auto x = bass (10.0);
    for (int os : {0, 2})
    {
        double secs = 1e9;
        std::vector<float> l;
        for (int i = 0; i < 3; ++i)
        {
            auto e = engine ([os] (Engine& e) { e.setParam (kOversample, os); });
            const pk::testing::CpuClock t0 = pk::testing::cpuClock ();
            l = run (*e, x, nullptr, 512);
            secs = std::min (secs, (double)(pk::testing::cpuClock () - t0) / pk::testing::kCpuClocksPerSec);
        }
        CHECK (finite (l), "finite");
        std::printf ("    CPU: %.2f%% of one core (%s)\n", 100.0 * secs / 10.0, os ? "4x" : "the defaults");
        CHECK (secs / 10.0 < (os ? 0.12 : 0.06), "too slow");
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
