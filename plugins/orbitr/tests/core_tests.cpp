// Headless tests for the Orbitr DSP. Run: ./orbitr_tests [filter]
// The Motion tests are Detonatr's (its Motion stage is Orbitr's effect), then the engine around it.
#include "Engine.h"
#include "Motion.h"
#include "Params.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <functional>
#include <memory>
#include <random>
#include <string>
#include <vector>

using namespace orbitr;

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

// Signals and measurements (Detonatr's test signals).
namespace sig {

constexpr double kPi = 3.14159265358979323846;

std::vector<float> sine (double hz, double amp, double seconds, double sr)
{
    std::vector<float> x ((size_t)(seconds * sr));
    for (size_t i = 0; i < x.size (); ++i)
        x[i] = (float)(amp * std::sin (2.0 * kPi * hz * (double)i / sr));
    return x;
}

std::vector<float> noise (double rmsAmp, double seconds, double sr, unsigned seed = 1)
{
    std::mt19937 rng (seed);
    std::normal_distribution<float> d (0.0f, (float)rmsAmp);
    std::vector<float> x ((size_t)(seconds * sr));
    for (auto& v : x)
        v = d (rng);
    return x;
}

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

// runs the effect (process (l, r, n) in place) over a mono signal on both channels, in blocks
std::vector<float> run (Motion& s, const std::vector<float>& x, int block = 256, std::vector<float>* right = nullptr)
{
    std::vector<float> out (x.size ()), l ((size_t)block), r ((size_t)block);
    if (right)
        right->assign (x.size (), 0.0f);
    for (size_t a = 0; a < x.size (); a += (size_t)block)
    {
        const int n = (int)std::min ((size_t)block, x.size () - a);
        std::copy (x.begin () + (ptrdiff_t)a, x.begin () + (ptrdiff_t)a + n, l.begin ());
        std::copy (x.begin () + (ptrdiff_t)a, x.begin () + (ptrdiff_t)a + n, r.begin ());
        s.process (l.data (), r.data (), n);
        std::copy (l.begin (), l.begin () + n, out.begin () + (ptrdiff_t)a);
        if (right)
            std::copy (r.begin (), r.begin () + n, right->begin () + (ptrdiff_t)a);
    }
    return out;
}

// the same through the whole engine (separate input and output buffers)
std::vector<float> run (Engine& e, const std::vector<float>& x, int block = 512, std::vector<float>* right = nullptr)
{
    std::vector<float> out (x.size ()), r (x.size ());
    for (size_t a = 0; a < x.size (); a += (size_t)block)
    {
        const int n = (int)std::min ((size_t)block, x.size () - a);
        e.process (x.data () + a, x.data () + a, out.data () + a, r.data () + a, n);
    }
    if (right)
        *right = r;
    return out;
}

} // namespace sig

namespace {
constexpr double kSr = 48000.0;

Motion make ()
{
    Motion m;
    m.prepare (kSr, 256);
    return m;
}

// one orb on a circle, nothing else: a clean Doppler test
Motion oneOrb (double speed, double distance = 6.0, double radius = 2.0)
{
    Motion m;
    m.setOrbs (1);
    m.setPattern (Motion::kOrbit);
    m.setRandomness (0.0);
    m.setSpread (0.0);
    m.setFloor (false);
    m.setMix (1.0);
    m.setSpeed (speed);
    m.setDistance (distance);
    m.setRadius (radius);
    m.prepare (kSr, 256);
    return m;
}

// the frequency over each period (from one rising zero crossing to the next, interpolated), from `from` on
std::vector<double> periods (const std::vector<float>& y, size_t from)
{
    std::vector<double> f;
    double last = -1.0;
    for (size_t i = from + 1; i < y.size (); ++i)
        if (y[i - 1] < 0.0f && y[i] >= 0.0f)
        {
            const double t = (double)(i - 1) + y[i - 1] / (double)(y[i - 1] - y[i]);
            if (last >= 0.0)
                f.push_back (kSr / (t - last));
            last = t;
        }
    return f;
}

std::unique_ptr<Engine> engine ()
{
    auto e = std::make_unique<Engine> ();
    e->prepare (kSr, 512);
    return e;
}
} // namespace

// --- Motion (Detonatr's tests) ---------------------------------------------------------

TEST (doppler_matches_theory)
{
    // a 1 kHz tone from an orb at 30 m/s on a circle (radius 2 m, its centre 6 m ahead): the listener is
    // outside the circle, so twice a turn the orb moves straight towards or away from the ears at its
    // full speed: f c / (c - v) and f c / (c + v)
    const double v = 30.0, c = Motion::kSoundSpeed;
    Motion m = oneOrb (v);
    const auto x = sig::sine (1000.0, 0.5, 3.0, kSr);
    const auto y = sig::run (m, x);
    const auto f = periods (y, 24000);
    double hi = 0.0, lo = 1e9;
    for (size_t i = 1; i + 1 < f.size (); ++i)
    {
        const double s = (f[i - 1] + f[i] + f[i + 1]) / 3.0; // over three periods
        hi = std::max (hi, s);
        lo = std::min (lo, s);
    }
    const double wantHi = 1000.0 * c / (c - v), wantLo = 1000.0 * c / (c + v);
    std::printf ("    highest %.2f Hz (theory %.2f), lowest %.2f Hz (theory %.2f)\n", hi, wantHi, lo, wantLo);
    CHECK (std::fabs (hi / wantHi - 1.0) < 0.003, "approaching: %.2f vs %.2f Hz", hi, wantHi);
    CHECK (std::fabs (lo / wantLo - 1.0) < 0.003, "receding: %.2f vs %.2f Hz", lo, wantLo);
}

TEST (doppler_follows_the_orbs_radial_speed)
{
    // over time: the pitch at each moment against the orb's radial speed from its positions
    const double v = 20.0, c = Motion::kSoundSpeed;
    Motion m = oneOrb (v, 4.0, 1.5);
    const auto x = sig::sine (1000.0, 0.5, 1.0, kSr);
    std::vector<float> y (x.size ());
    std::vector<double> dist (x.size ());
    std::vector<float> l (16), r (16);
    for (size_t a = 0; a < x.size (); a += 16)
    {
        const auto p = m.orbPosition (0);
        std::copy (x.begin () + (ptrdiff_t)a, x.begin () + (ptrdiff_t)a + 16, l.begin ());
        std::copy (l.begin (), l.end (), r.begin ());
        m.process (l.data (), r.data (), 16);
        for (int i = 0; i < 16; ++i)
        {
            y[a + (size_t)i] = l[(size_t)i];
            dist[a + (size_t)i] = std::sqrt (p.x * p.x + p.y * p.y + p.z * p.z);
        }
    }
    // the source's radial speed at the moment the sound left it (the orb's clock runs with the output's:
    // the sound heard at t left the orb at t - d / c; the fixed 10 ms only delays the audio); compare
    // the measured frequency with c / (c + v_r) there
    double worst = 0.0;
    int n = 0;
    double last = -1.0;
    for (size_t i = 24001; i < y.size (); ++i)
        if (y[i - 1] < 0.0f && y[i] >= 0.0f)
        {
            const double t = (double)(i - 1) + y[i - 1] / (double)(y[i - 1] - y[i]);
            if (last >= 0.0)
            {
                const double fm = kSr / (t - last), mid = 0.5 * (t + last);
                // the emission time of the sample at `mid`
                const double em = mid - dist[(size_t)mid] / c * kSr;
                const size_t e = (size_t)em;
                const double vr = (dist[e + 48] - dist[e - 48]) / (96.0 / kSr);
                const double want = 1000.0 * c / (c + vr);
                worst = std::max (worst, std::fabs (fm / want - 1.0));
                ++n;
            }
            last = t;
        }
    std::printf ("    %d periods: the largest error against c / (c + v_r) %.3f %%\n", n, 100.0 * worst);
    CHECK (n > 400 && worst < 0.002, "the pitch follows the radial speed: %.3f %%", 100.0 * worst);
}

TEST (standing_still_keeps_the_pitch)
{
    Motion m = oneOrb (0.0);
    const auto x = sig::sine (1000.0, 0.5, 1.0, kSr);
    const auto y = sig::run (m, x);
    const auto f = periods (y, 24000);
    double worst = 0.0;
    for (double v : f)
        worst = std::max (worst, std::fabs (v - 1000.0));
    CHECK (worst < 0.5, "1 kHz stays 1 kHz: off by %.3f Hz at most", worst);
}

TEST (latency_and_dry)
{
    Motion m = make ();
    CHECK (m.latency () == 480, "10 ms at 48 kHz: %d", m.latency ());
    m.setMix (0.0);
    const auto x = sig::noise (0.2, 0.5, kSr, 2);
    const auto y = sig::run (m, x, 173);
    double err = 0.0;
    for (size_t i = (size_t)m.latency (); i < x.size (); ++i)
        err = std::max (err, (double)std::fabs (y[i] - x[i - (size_t)m.latency ()]));
    CHECK (err < 1e-7, "Mix 0: the input delayed by the latency (%.2g)", err);
    for (double sr : {44100.0, 96000.0})
    {
        Motion k;
        k.prepare (sr, 512);
        CHECK (k.latency () == (int)std::lround (0.01 * sr), "10 ms at %.0f Hz: %d", sr, k.latency ());
    }
}

TEST (swarm_is_wide_and_level_matched)
{
    Motion m = make (); // Liquid Debris-like: six orbs swarming
    m.setOrbs (kLiquidDebris.orbs);
    m.setPattern (Motion::kSwarm);
    m.setSpeed (kLiquidDebris.speed);
    m.setDistance (kLiquidDebris.distance);
    m.setRadius (kLiquidDebris.radius);
    m.setSpread (kLiquidDebris.spread);
    m.setRandomness (kLiquidDebris.randomness);
    m.setFloor (kLiquidDebris.floor);
    m.setMix (1.0);
    m.reset ();
    const auto x = sig::noise (0.1, 3.0, kSr, 8);
    std::vector<float> r;
    const auto l = sig::run (m, x, 256, &r);
    double lr = 0.0, ll = 0.0, rr = 0.0;
    for (size_t i = 48000; i < x.size (); ++i)
    {
        lr += (double)l[i] * r[i];
        ll += (double)l[i] * l[i];
        rr += (double)r[i] * r[i];
    }
    const double corr = lr / std::sqrt (ll * rr);
    const double d = sig::db (0.5 * (sig::rms (l, 48000, x.size ()) + sig::rms (r, 48000, x.size ())) / sig::rms (x, 48000, x.size ()));
    std::printf ("    left / right correlation %.2f, level %+.1f dB\n", corr, d);
    CHECK (corr < 0.9, "the swarm spreads across the stereo field: %.2f", corr);
    CHECK (std::fabs (d) < 4.0, "about as loud as the input: %+.1f dB", d);
    // the orbs stay in their ball round the centre
    bool inside = true;
    for (int k = 0; k < 6; ++k)
    {
        const auto p = m.orbPosition (k);
        inside = inside && std::sqrt (p.x * p.x + (p.y - 3.0) * (p.y - 3.0) + p.z * p.z) <= 2.0 + 1e-6;
    }
    CHECK (inside, "every orb within the radius");
}

TEST (finite_everywhere)
{
    std::mt19937 rng (6);
    std::uniform_real_distribution<double> u (0.0, 1.0);
    Motion m = make ();
    bool ok = true;
    for (int round = 0; round < 16; ++round)
    {
        m.setOrbs (1 + (int)(u (rng) * 16));
        m.setPattern (u (rng) < 0.5 ? 0 : 1);
        m.setSpeed (u (rng) * 80.0);
        m.setDistance (0.5 + u (rng) * 19.5);
        m.setRadius (0.1 + u (rng) * 2.9);
        m.setSpread (u (rng));
        m.setRandomness (u (rng));
        m.setFloor (u (rng) < 0.5);
        m.setMix (u (rng));
        auto x = sig::noise (u (rng) < 0.3 ? 3.0 : 0.1, 0.25, kSr, (unsigned)round);
        if (round % 4 == 0)
            std::fill (x.begin (), x.end (), 0.0f);
        ok = ok && sig::finite (sig::run (m, x, 1 + (int)(u (rng) * 600)));
    }
    CHECK (ok, "finite with random settings, loud input and silence");
}

// --- the plug-in around it -------------------------------------------------------------

TEST (params)
{
    const auto& t = paramTable ();
    CHECK (t.size () == kNumParams, "size");
    for (uint32_t id = 0; id < kNumParams; ++id)
    {
        double v = 0.0;
        CHECK (t.fromText (id, t.toText (id, t.info (id).def), v), "%s", t.info (id).name);
    }
    // the defaults are Detonatr's Motion stage's (Liquid Debris-like)
    CHECK (t.info (kOrbs).def == 6.0 && t.info (kPattern).def == (double)kPatternSwarm && t.info (kSpeed).def == 18.0 &&
               t.info (kDistance).def == 3.0 && t.info (kRadius).def == 2.0 && t.info (kSpread).def == 0.8 &&
               t.info (kRandom).def == 0.6 && t.info (kFloor).def == 1.0 && t.info (kMix).def == 0.5,
           "Liquid Debris: 6 orbs swarming at 18 m/s, 3 m ahead, radius 2 m, Spread 80 %%, Random 60 %%, Floor, Mix 50 %%");
    CHECK (t.info (kDryWet).def == 1.0 && t.info (kOutput).def == 0.0, "Dry/Wet 100 %%, Output 0 dB");
    CHECK (t.info (kTailBase + pk::kTailOn).def == 0.0, "the end saturator starts off");
    // the engine starts where the table does
    Engine e;
    e.prepare (kSr, 512);
    CHECK (e.motionStage ().orbs () == 6, "six orbs: %d", e.motionStage ().orbs ());
}

TEST (latency_is_motion_plus_the_saturator)
{
    auto e = engine ();
    smacheratr::Tail t;
    t.prepare (kSr, 512);
    CHECK (e->latency () == 480 + t.latency (), "latency %d samples (10 ms + the end saturator's %d)", e->latency (), t.latency ());
}

TEST (dry_wet_lines_up)
{
    // Dry/Wet 0 %: the input delayed by the effect's latency, whatever Mix and the orbs do; Mix 0 %
    // inside gives the same (both dry paths are lined up)
    const auto x = sig::noise (0.1, 1.0, kSr, 3);
    auto delayErr = [&] (uint32_t id) {
        auto e = engine ();
        e->setParam (id, 0.0);
        e->reset ();
        const auto y = sig::run (*e, x);
        const size_t lat = (size_t)e->latency ();
        double err = 0.0;
        for (size_t i = lat; i < x.size (); ++i)
            err = std::max (err, (double)std::fabs (y[i] - x[i - lat]));
        return err;
    };
    const double dw = delayErr (kDryWet), mx = delayErr (kMix);
    std::printf ("    largest difference from the delayed input: Dry/Wet 0 %% %.2g, Mix 0 %% %.2g\n", dw, mx);
    CHECK (dw < 1e-4, "Dry/Wet 0 %%: the delayed input (%.2g)", dw);
    CHECK (mx < 1e-4, "Mix 0 %%: the delayed input (%.2g)", mx);
}

TEST (defaults_move_and_spread)
{
    // the defaults: the swarm moves a 1 kHz tone's pitch about and spreads it across the stereo field,
    // about as loud as it went in; Output scales it
    const auto x = sig::sine (1000.0, 0.25, 3.0, kSr);
    auto e = engine ();
    std::vector<float> r;
    const auto l = sig::run (*e, x, 512, &r);
    double lr = 0.0, ll = 0.0, rr = 0.0;
    for (size_t i = 48000; i < x.size (); ++i)
    {
        lr += (double)l[i] * r[i];
        ll += (double)l[i] * l[i];
        rr += (double)r[i] * r[i];
    }
    const double corr = lr / std::sqrt (ll * rr);
    const double d = sig::db (sig::rms (l, 48000, x.size ()) / sig::rms (x, 48000, x.size ()));
    const auto f = periods (l, 48000);
    double hi = 0.0, lo = 1e9;
    for (double v : f)
    {
        hi = std::max (hi, v);
        lo = std::min (lo, v);
    }
    std::printf ("    correlation %.2f, level %+.1f dB, zero-crossing rate from %.0f to %.0f Hz\n", corr, d, lo, hi);
    CHECK (corr < 0.98, "wider than the mono input: %.2f", corr);
    CHECK (std::fabs (d) < 4.0, "about as loud as the input: %+.1f dB", d);
    CHECK (hi - lo > 5.0, "the pitch moves (%.0f .. %.0f Hz)", lo, hi);
    auto quiet = engine ();
    quiet->setParam (kOutput, -12.0);
    quiet->reset ();
    const auto q = sig::run (*quiet, x);
    const double drop = sig::db (sig::rms (q, 48000, x.size ()) / sig::rms (l, 48000, x.size ()));
    CHECK (std::fabs (drop + 12.0) < 0.05, "Output -12 dB: %.2f dB", drop);
}

TEST (finite_and_cpu)
{
    // the defaults (six orbs) and the most orbs (16, the fastest), the saturator on
    auto make = [] (bool most) {
        auto e = engine ();
        e->setParam (kTailBase + pk::kTailOn, 1.0);
        if (most)
        {
            e->setParam (kOrbs, 16.0);
            e->setParam (kSpeed, 80.0);
        }
        e->reset ();
        return e;
    };
    const size_t n = (size_t)(10.0 * kSr);
    std::vector<float> in (n);
    uint32_t seed = 1;
    for (size_t i = 0; i < n; ++i)
    {
        seed = seed * 1664525u + 1013904223u;
        in[i] = (float)((int32_t)seed / 2147483648.0) * 0.9f;
    }
    for (bool most : {false, true})
    {
        // CPU time (other programs running do not count), the best of three renders
        std::vector<float> l, r;
        double secs = 1e9;
        for (int i = 0; i < 3; ++i)
        {
            auto e = make (most);
            const std::clock_t t0 = std::clock ();
            l = sig::run (*e, in, 333, &r);
            secs = std::min (secs, (double)(std::clock () - t0) / CLOCKS_PER_SEC);
        }
        bool finite = true;
        for (size_t i = 0; i < n; ++i)
            finite = finite && std::isfinite (l[i]) && std::isfinite (r[i]) && std::fabs (l[i]) < 8.0f;
        CHECK (finite, "finite and bounded");
        std::printf ("    CPU: %.2f%% of one core (%s, saturator on)\n", 100.0 * secs / 10.0, most ? "16 orbs at 80 m/s" : "the defaults");
        CHECK (secs / 10.0 < (most ? 0.18 : 0.09), "too slow"); // (about 7 % and 4 % here)
    }
}

// --- Grains ----------------------------------------------------------------------------

namespace {
// oneOrb with Grains on: no Scatter, Density 2, no Pitch unless asked
Motion grainOrb (double speed, double size = 80.0, double density = 2.0, double scatter = 0.0, double pitch = 0.0)
{
    Motion m = oneOrb (speed);
    m.setGrains (true);
    m.setGrainSize (size);
    m.setGrainDensity (density);
    m.setGrainScatter (scatter);
    m.setGrainPitch (pitch);
    m.reset ();
    return m;
}

double correlation (const std::vector<float>& a, const std::vector<float>& b, size_t from, size_t to, size_t lag = 0)
{
    double ab = 0.0, aa = 0.0, bb = 0.0;
    for (size_t i = from; i < to; ++i)
    {
        ab += (double)a[i] * b[i - lag];
        aa += (double)a[i] * a[i];
        bb += (double)b[i - lag] * b[i - lag];
    }
    return ab / std::sqrt (std::max (1e-30, aa * bb));
}

// the 5 ms stretches of y from `from` on: the share of them more than 30 dB under the loudest, and
// the mean length of the runs of louder ones (ms)
void gaps (const std::vector<float>& y, size_t from, double& silentShare, double& meanRunMs)
{
    const size_t w = (size_t)(0.005 * kSr);
    std::vector<double> r;
    for (size_t a = from; a + w <= y.size (); a += w)
        r.push_back (sig::rms (y, a, a + w));
    const double top = *std::max_element (r.begin (), r.end ());
    int silent = 0, runs = 0, loud = 0;
    bool in = false;
    for (double v : r)
    {
        const bool q = v < top * 0.0316;
        silent += q ? 1 : 0;
        if (!q)
        {
            ++loud;
            if (!in)
                ++runs;
        }
        in = !q;
    }
    silentShare = (double)silent / (double)r.size ();
    meanRunMs = runs > 0 ? 5.0 * loud / runs : 0.0;
}

double toneDbFrom (const std::vector<float>& y, double hz, size_t from)
{
    double s = 0.0, co = 0.0;
    for (size_t i = from; i < y.size (); ++i)
    {
        s += y[i] * std::sin (2.0 * sig::kPi * hz * (double)i / kSr);
        co += y[i] * std::cos (2.0 * sig::kPi * hz * (double)i / kSr);
    }
    return sig::db (2.0 * std::sqrt (s * s + co * co) / (double)(y.size () - from));
}
} // namespace

TEST (grains_off_is_untouched)
{
    // Grains off: the grain controls change nothing, and after Grains was on and off again (its fade
    // and the orbs' lines emptied of grains), the output is the never-grained one, sample for sample
    const auto x = sig::noise (0.2, 2.5, kSr, 11);
    auto plain = engine ();
    const auto ref = sig::run (*plain, x, 300);
    auto knobs = engine ();
    knobs->setParam (kGrainSize, 20.0);
    knobs->setParam (kGrainDensity, 0.3);
    knobs->setParam (kGrainScatter, 1.0);
    knobs->setParam (kGrainPitch, 7.0);
    knobs->reset ();
    const auto y = sig::run (*knobs, x, 300);
    CHECK (y == ref, "the grain controls do nothing with Grains off");
    auto toggled = engine ();
    std::vector<float> z (x.size ()), zr (x.size ());
    for (size_t a = 0; a < x.size (); a += 300)
    {
        if (a == 300 * 80)
            toggled->setParam (kGrains, 1.0);
        if (a == 300 * 160)
            toggled->setParam (kGrains, 0.0);
        const int n = (int)std::min ((size_t)300, x.size () - a);
        toggled->process (x.data () + a, x.data () + a, z.data () + a, zr.data () + a, n);
    }
    const size_t back = 300 * 160 + (size_t)(0.2 * kSr);
    double during = 0.0, after = 0.0;
    for (size_t i = 300 * 90; i < 300 * 150; ++i)
        during = std::max (during, (double)std::fabs (z[i] - ref[i]));
    for (size_t i = back; i < x.size (); ++i)
        after = std::max (after, (double)std::fabs (z[i] - ref[i]));
    std::printf ("    Grains on: differs by up to %.3f; off again: %.3g\n", during, after);
    CHECK (during > 0.01, "Grains on changes the sound: %.3f", during);
    CHECK (after == 0.0, "Grains off again: the plain output (%.3g)", after);
}

TEST (grains_with_no_scatter_play_the_slice)
{
    // one standing orb, Density 2, no Scatter or Pitch: the Hann windows sum to 1, so the orb plays its
    // slice (3 samples back for the first orb) untouched: the plain output, 3 samples late
    const auto x = sig::noise (0.2, 1.0, kSr, 12);
    Motion a = oneOrb (0.0);
    Motion b = grainOrb (0.0);
    const auto ya = sig::run (a, x), yb = sig::run (b, x);
    double err = 0.0, peak = 0.0;
    for (size_t i = 12000; i < x.size (); ++i)
    {
        err = std::max (err, (double)std::fabs (yb[i] - ya[i - 3]));
        peak = std::max (peak, (double)std::fabs (ya[i]));
    }
    std::printf ("    largest difference %.2g (peak %.2f)\n", err, peak);
    CHECK (err < 1e-3 * peak, "the slice plays untouched: %.2g", err);
}

TEST (grains_size_and_density)
{
    // a steady tone through one standing orb: below Density 1 the grains leave gaps (a grain every
    // Size / Density); the louder runs are about a grain long
    const auto x = sig::sine (500.0, 0.5, 4.0, kSr);
    double silentDense, runDense, silentSparse, runSparse, silentLong, runLong;
    {
        Motion m = grainOrb (0.0, 80.0, 2.0);
        gaps (sig::run (m, x), 48000, silentDense, runDense);
    }
    {
        Motion m = grainOrb (0.0, 40.0, 0.2);
        gaps (sig::run (m, x), 48000, silentSparse, runSparse);
    }
    {
        Motion m = grainOrb (0.0, 160.0, 0.2);
        gaps (sig::run (m, x), 48000, silentLong, runLong);
    }
    std::printf ("    Density 2: %.0f %% silent; Density 0.2: %.0f %% silent, runs of %.0f ms (40 ms grains), %.0f %% and %.0f ms "
                 "(160 ms grains)\n",
                 100.0 * silentDense, 100.0 * silentSparse, runSparse, 100.0 * silentLong, runLong);
    CHECK (silentDense == 0.0, "Density 2: a continuous cloud (%.0f %% silent)", 100.0 * silentDense);
    CHECK (silentSparse > 0.75 && silentLong > 0.75, "Density 0.2: mostly gaps (%.0f %%, %.0f %%)", 100.0 * silentSparse,
           100.0 * silentLong);
    CHECK (runSparse > 15.0 && runSparse < 45.0 && runLong > 70.0 && runLong < 170.0 && runLong > 3.0 * runSparse,
           "the grains as long as Size: %.0f ms and %.0f ms", runSparse, runLong);
}

TEST (grains_scatter_moves_the_slices)
{
    // noise through one standing orb: with no Scatter the orb plays its slice (the input, a few
    // samples late); Scatter takes each grain from elsewhere in the last half second
    const auto x = sig::noise (0.2, 3.0, kSr, 13);
    Motion ref = oneOrb (0.0);
    const auto yr = sig::run (ref, x);
    Motion a = grainOrb (0.0, 80.0, 2.0, 0.0), b = grainOrb (0.0, 80.0, 2.0, 1.0);
    const auto ya = sig::run (a, x), yb = sig::run (b, x);
    const double ca = correlation (ya, yr, 48000, x.size (), 3), cb = correlation (yb, yr, 48000, x.size (), 3);
    const double lb = sig::db (sig::rms (yb, 48000, x.size ()) / sig::rms (yr, 48000, x.size ()));
    std::printf ("    correlation with the slice: Scatter 0 %.3f, Scatter 100 %% %.3f (level %+.1f dB)\n", ca, cb, lb);
    CHECK (ca > 0.999, "no Scatter: the slice (%.3f)", ca);
    CHECK (std::fabs (cb) < 0.2, "Scatter 100 %%: elsewhere (%.3f)", cb);
    CHECK (std::fabs (lb) < 3.0, "about as loud: %+.1f dB", lb);
}

TEST (grains_keep_the_doppler)
{
    // the moving orb's Doppler shifts its grains as it did the input: the tone at f c / (c - v) and
    // f c / (c + v) twice a turn (the grains summing to 1: no Scatter, Density 2)
    const double v = 30.0, c = Motion::kSoundSpeed;
    Motion m = grainOrb (v);
    const auto x = sig::sine (1000.0, 0.5, 3.0, kSr);
    const auto f = periods (sig::run (m, x), 24000);
    double hi = 0.0, lo = 1e9;
    for (size_t i = 1; i + 1 < f.size (); ++i)
    {
        const double s = (f[i - 1] + f[i] + f[i + 1]) / 3.0;
        hi = std::max (hi, s);
        lo = std::min (lo, s);
    }
    const double wantHi = 1000.0 * c / (c - v), wantLo = 1000.0 * c / (c + v);
    std::printf ("    highest %.2f Hz (theory %.2f), lowest %.2f Hz (theory %.2f)\n", hi, wantHi, lo, wantLo);
    CHECK (std::fabs (hi / wantHi - 1.0) < 0.003 && std::fabs (lo / wantLo - 1.0) < 0.003, "the Doppler stays");
    // Grain Pitch +12: an octave up, standing still; with the orb moving, an octave over its Doppler
    Motion up = grainOrb (0.0, 80.0, 2.0, 0.0, 12.0);
    const auto yu = sig::run (up, x);
    const double at2k = toneDbFrom (yu, 2000.0, 48000), at1k = toneDbFrom (yu, 1000.0, 48000);
    std::printf ("    Pitch +12: 2 kHz at %.1f dB, 1 kHz at %.1f dB\n", at2k, at1k);
    CHECK (at2k > at1k + 30.0, "an octave up: %.1f vs %.1f dB", at2k, at1k);
    Motion upMoving = grainOrb (v, 80.0, 2.0, 0.0, 12.0);
    const auto yum = sig::run (upMoving, x);
    const double near2k = toneDbFrom (yum, 2000.0, 48000);
    double hiUp = 0.0;
    {
        // the strongest frequency in 20 ms windows, from 1.8 to 2.3 kHz in 5 Hz steps: up to an octave over the Doppler's highest
        for (size_t a = 48000; a + 960 <= yum.size (); a += 960)
        {
            double best = -1.0, bestHz = 0.0;
            for (double hz = 1800.0; hz <= 2300.0; hz += 5.0)
            {
                double s = 0.0, co = 0.0;
                for (size_t i = a; i < a + 960; ++i)
                {
                    s += yum[i] * std::sin (2.0 * sig::kPi * hz * (double)i / kSr);
                    co += yum[i] * std::cos (2.0 * sig::kPi * hz * (double)i / kSr);
                }
                if (s * s + co * co > best)
                {
                    best = s * s + co * co;
                    bestHz = hz;
                }
            }
            hiUp = std::max (hiUp, bestHz);
        }
    }
    std::printf ("    Pitch +12 and moving: up to %.0f Hz (an octave over the Doppler's highest: %.0f Hz)\n", hiUp, 2.0 * wantHi);
    CHECK (std::fabs (hiUp / (2.0 * wantHi) - 1.0) < 0.02, "an octave over the Doppler: %.0f Hz", hiUp);
    CHECK (near2k > -40.0, "an octave up while moving (%.1f dB at 2 kHz)", near2k);
}

TEST (grains_level_and_switching)
{
    // the defaults with Grains: about as loud as the input; switching Grains on and off crossfades
    const auto x = sig::sine (300.0, 0.25, 3.0, kSr);
    auto e = engine ();
    e->setParam (kGrains, 1.0);
    e->reset ();
    const auto y = sig::run (*e, x);
    const double d = sig::db (sig::rms (y, 48000, x.size ()) / sig::rms (x, 48000, x.size ()));
    std::printf ("    the defaults with Grains: %+.1f dB\n", d);
    CHECK (std::fabs (d) < 4.0, "about as loud as the input: %+.1f dB", d);
    // the largest step from sample to sample while switching, against the steady ones (one standing
    // orb, all wet)
    auto steps = [] (const std::vector<float>& z, size_t a, size_t b) {
        double s = 0.0;
        for (size_t i = a + 1; i < b; ++i)
            s = std::max (s, (double)std::fabs (z[i] - z[i - 1]));
        return s;
    };
    auto setUp = [] (Engine& g) {
        g.setParam (kMix, 1.0);
        g.setParam (kOrbs, 1.0);
        g.setParam (kSpeed, 0.0);
        g.reset ();
    };
    auto sw = engine (), still = engine ();
    setUp (*sw);
    setUp (*still);
    std::vector<float> z (x.size ()), zr (x.size ());
    for (size_t a = 0; a < x.size (); a += 256)
    {
        if (a == 256 * 100)
            sw->setParam (kGrains, 1.0);
        if (a == 256 * 300)
            sw->setParam (kGrains, 0.0);
        const int n = (int)std::min ((size_t)256, x.size () - a);
        sw->process (x.data () + a, x.data () + a, z.data () + a, zr.data () + a, n);
    }
    const auto s0 = sig::run (*still, x, 256);
    const double steady = steps (s0, 24000, x.size ());
    const double on = steps (z, 256 * 100, 256 * 100 + 9600), off = steps (z, 256 * 300, 256 * 300 + 9600);
    std::printf ("    largest step: steady %.4f, switching on %.4f, off %.4f\n", steady, on, off);
    CHECK (on < 1.5 * steady && off < 1.5 * steady, "no clicks");
    CHECK (sig::finite (z), "finite");
}

TEST (grains_finite_and_cpu)
{
    std::mt19937 rng (21);
    std::uniform_real_distribution<double> u (0.0, 1.0);
    Motion m = make ();
    bool ok = true;
    double peak = 0.0;
    for (int round = 0; round < 24; ++round)
    {
        m.setOrbs (1 + (int)(u (rng) * 16));
        m.setPattern (u (rng) < 0.5 ? 0 : 1);
        m.setSpeed (u (rng) * 80.0);
        m.setDistance (0.5 + u (rng) * 19.5);
        m.setRadius (0.1 + u (rng) * 2.9);
        m.setSpread (u (rng));
        m.setRandomness (u (rng));
        m.setFloor (u (rng) < 0.5);
        m.setMix (u (rng));
        m.setGrains (u (rng) < 0.8);
        m.setGrainSize (10.0 * std::pow (50.0, u (rng)));
        m.setGrainDensity (0.1 * std::pow (80.0, u (rng)));
        m.setGrainScatter (u (rng));
        m.setGrainPitch (-12.0 + 24.0 * u (rng));
        auto x = sig::noise (u (rng) < 0.3 ? 1.0 : 0.1, 0.4, kSr, (unsigned)round + 100);
        if (round % 5 == 0)
            std::fill (x.begin (), x.end (), 0.0f);
        const auto y = sig::run (m, x, 1 + (int)(u (rng) * 600));
        ok = ok && sig::finite (y);
        for (float v : y)
            peak = std::max (peak, (double)std::fabs (v));
    }
    std::printf ("    peak %.2f over random settings\n", peak);
    CHECK (ok && peak < 16.0, "finite and bounded with random settings (peak %.2f)", peak);

    // CPU: 16 orbs at 80 m/s with Grains (at their defaults, then at their heaviest: 500 ms grains at
    // Density 8), the saturator on; the best of three renders
    const size_t n = (size_t)(10.0 * kSr);
    std::vector<float> in (n);
    uint32_t seed = 1;
    for (size_t i = 0; i < n; ++i)
    {
        seed = seed * 1664525u + 1013904223u;
        in[i] = (float)((int32_t)seed / 2147483648.0) * 0.9f;
    }
    for (bool heaviest : {false, true})
    {
        std::vector<float> l, r;
        double secs = 1e9;
        for (int i = 0; i < 3; ++i)
        {
            auto e = engine ();
            e->setParam (kTailBase + pk::kTailOn, 1.0);
            e->setParam (kOrbs, 16.0);
            e->setParam (kSpeed, 80.0);
            e->setParam (kGrains, 1.0);
            if (heaviest)
            {
                e->setParam (kGrainSize, 500.0);
                e->setParam (kGrainDensity, 8.0);
            }
            e->reset ();
            const std::clock_t t0 = std::clock ();
            l = sig::run (*e, in, 333, &r);
            secs = std::min (secs, (double)(std::clock () - t0) / CLOCKS_PER_SEC);
        }
        bool finite = true;
        for (size_t i = 0; i < n; ++i)
            finite = finite && std::isfinite (l[i]) && std::isfinite (r[i]) && std::fabs (l[i]) < 8.0f;
        CHECK (finite, "finite and bounded");
        std::printf ("    CPU: %.2f%% of one core (16 orbs at 80 m/s, Grains %s, saturator on)\n", 100.0 * secs / 10.0,
                     heaviest ? "500 ms at Density 8" : "at their defaults");
        CHECK (secs / 10.0 < (heaviest ? 0.30 : 0.20), "too slow");
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
