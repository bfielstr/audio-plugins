// Headless tests for the Moistr DSP. Run: ./moistr_tests [filter]
// The split (flat sum, LR4 slopes at the set corners, the seeded Low crossover), the movement (Low locked,
// the other bands rising and falling by Depth, the seeded rise and fall times x Rise / Fall, still at 0,
// Seed, the host's transport), switching Bands, the Glue compressor, Grit, Mix, Passes, silence, the 0.22
// controls (Low Push / Dip, Seed B / Blend, Density, Speed, Drop Out; all at their defaults 0.21's sound bit
// for bit) and the CPU budget.
#include "Dsp.h"
#include "Engine.h"
#include "Movement.h"
#include "Params.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <functional>
#include <limits>
#include <memory>
#include <string>
#include <tuple>
#include <vector>

using namespace moistr;

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
constexpr uint32_t kLevelIds[kMaxBands] = {kLowLevel, kMidLevel, kHighLevel, kAirLevel};
constexpr uint32_t kMoveIds[kMaxBands] = {kLowMove, kMidMove, kHighMove, kAirMove};
const char* const kBandNames[kMaxBands] = {"Low", "Mid", "High", "Air"};

double rms (const std::vector<float>& x, size_t a, size_t b)
{
    b = std::min (b, x.size ());
    double s = 0.0;
    for (size_t i = a; i < b; ++i)
        s += (double)x[i] * x[i];
    return b > a ? std::sqrt (s / (double)(b - a)) : 0.0;
}
double peak (const std::vector<float>& x, size_t a, size_t b)
{
    double p = 0.0;
    for (size_t i = a; i < std::min (b, x.size ()); ++i)
        p = std::max (p, (double)std::fabs (x[i]));
    return p;
}
double db (double v) { return 20.0 * std::log10 (std::max (v, 1e-20)); }
bool finite (const std::vector<float>& x)
{
    for (float v : x)
        if (!std::isfinite (v))
            return false;
    return true;
}
// the amplitude of the component at `hz` in x[a, b) (a single DFT bin, Hann window)
double toneAt (const std::vector<float>& x, double hz, size_t a, size_t b)
{
    double s = 0, c = 0, wsum = 0;
    for (size_t i = a; i < b; ++i)
    {
        const double w = 0.5 - 0.5 * std::cos (2.0 * kPi * (double)(i - a) / (double)(b - a));
        s += w * x[i] * std::sin (2.0 * kPi * hz * (double)i / kSr);
        c += w * x[i] * std::cos (2.0 * kPi * hz * (double)i / kSr);
        wsum += w;
    }
    return 2.0 * std::sqrt (s * s + c * c) / wsum;
}
// the magnitude (dB) of a response h (an impulse response) at hz (a plain DFT bin, no window)
double responseDb (const std::vector<float>& h, size_t from, double hz)
{
    double s = 0, c = 0;
    for (size_t i = from; i < h.size (); ++i)
    {
        s += h[i] * std::sin (2.0 * kPi * hz * (double)(i - from) / kSr);
        c += h[i] * std::cos (2.0 * kPi * hz * (double)(i - from) / kSr);
    }
    return db (std::sqrt (s * s + c * c));
}
std::vector<float> sine (double hz, double amp, double seconds)
{
    std::vector<float> x ((size_t)(seconds * kSr));
    for (size_t i = 0; i < x.size (); ++i)
        x[i] = (float)(amp * std::sin (2.0 * kPi * hz * (double)i / kSr));
    return x;
}
std::vector<float> noise (double amp, double seconds, uint32_t seed = 3)
{
    std::vector<float> x ((size_t)(seconds * kSr));
    for (auto& v : x)
    {
        seed = seed * 1664525u + 1013904223u;
        v = (float)((int32_t)seed / 2147483648.0 * amp);
    }
    return x;
}
// a detuned saw pair (a plain Reese), mono
std::vector<float> reese (double seconds)
{
    std::vector<float> x ((size_t)(seconds * kSr));
    double p1 = 0.0, p2 = 0.3;
    for (auto& v : x)
    {
        p1 += 55.0 / kSr;
        p2 += 55.0 * 1.012 / kSr;
        p1 -= std::floor (p1);
        p2 -= std::floor (p2);
        v = (float)(0.25 * ((2.0 * p1 - 1.0) + (2.0 * p2 - 1.0)));
    }
    return x;
}

std::unique_ptr<Engine> engine (int block = 512)
{
    auto e = std::make_unique<Engine> ();
    e->prepare (kSr, block);
    return e;
}

// the split alone: no Drive, Glue, Grit or movement
void plain (Engine& e)
{
    e.setParam (kDrive, 0.0);
    e.setParam (kGlue, 0.0);
    e.setParam (kGrit, 0.0);
    e.setParam (kMovement, 0.0);
    e.reset ();
}
// only one band sounding
void solo (Engine& e, int band)
{
    for (int b = 0; b < kMaxBands; ++b)
        e.setParam (kLevelIds[b], b == band ? 0.0 : kLevelOffDb);
    e.reset ();
}

// renders x (both channels) in blocks, calling `at (sample)` before each
std::vector<float> run (Engine& e, const std::vector<float>& x, std::vector<float>* right = nullptr, int block = 256,
                        const std::function<void (size_t)>& at = {})
{
    std::vector<float> l (x.size ()), r (x.size ());
    for (size_t a = 0; a < x.size (); a += (size_t)block)
    {
        if (at)
            at (a);
        const int m = (int)std::min ((size_t)block, x.size () - a);
        e.process (x.data () + a, x.data () + a, l.data () + a, r.data () + a, m);
    }
    if (right)
        *right = r;
    return l;
}

// the gain (dB) of a band alone at hz: a sine at -12 dBFS through it
double bandGainDb (int band, double hz, const std::function<void (Engine&)>& setup = {})
{
    auto e = engine ();
    plain (*e);
    if (setup)
        setup (*e);
    solo (*e, band);
    const auto x = sine (hz, 0.25, 0.4);
    const auto y = run (*e, x);
    const size_t a = (size_t)(0.2 * kSr), b = x.size ();
    return db (toneAt (y, hz, a, b) / toneAt (x, hz, a, b));
}

// a Linkwitz-Riley 4 side's magnitude (dB) at f for a corner at fc (the bilinear transform's warped frequency)
double lr4Db (double fc, double f, bool low)
{
    const double w = std::tan (kPi * f / kSr) / std::tan (kPi * fc / kSr), w4 = w * w * w * w;
    return db ((low ? 1.0 : w4) / (1.0 + w4));
}

// the gain envelope (dB) of x in windows of `win` samples
std::vector<double> envelope (const std::vector<float>& y, size_t from, size_t win)
{
    std::vector<double> env;
    for (size_t a = from; a + win <= y.size (); a += win)
        env.push_back (db (rms (y, a, a + win) * std::sqrt (2.0)));
    return env;
}

// a moving band set up to be heard alone in its own region: (band, its test tone, the settings)
struct BandSetup
{
    int band;
    double hz;
    std::function<void (Engine&)> set;
};
std::vector<BandSetup> bandSetups ()
{
    return {
        {kBandMid, 1500.0, [] (Engine& e) { e.setParam (kXoverMid, 6000.0); }},
        {kBandHigh, 6000.0, [] (Engine& e) { e.setParam (kXoverMid, 1000.0); }},
        {kBandAir, 10000.0, [] (Engine& e) {
             e.setParam (kBandCount, kBands4);
             e.setParam (kXoverMid, 400.0);
             e.setParam (kXoverHigh, 1500.0);
         }},
    };
}

} // namespace

TEST (split_sums_flat)
{
    // every band at the same level and still: the bands add up to an all-pass of the input, flat in level
    for (int bands : {3, 4})
        for (int seed : {1, 2, 50, 128})
        {
            auto e = engine ();
            plain (*e);
            e->setParam (kSeed, seed);
            e->setParam (kBandCount, bands == 4 ? kBands4 : kBands3);
            e->setParam (kXoverMid, 1200.0);
            e->setParam (kXoverHigh, 6000.0);
            e->reset ();
            const int lat = e->latency ();
            std::vector<float> x (32768, 0.0f);
            x[0] = 1.0f;
            const auto y = run (*e, x);
            double lo = 1e9, hi = -1e9;
            for (int i = 0; i <= 80; ++i)
            {
                const double f = 20.0 * std::pow (1000.0, i / 80.0); // 20 Hz .. 20 kHz
                const double r = responseDb (y, (size_t)lat, f);
                lo = std::min (lo, r);
                hi = std::max (hi, r);
            }
            // and the sum is the all-pass of the three corners (the same filters, run by hand)
            dsp::SvfCoefs c[kMaxXovers];
            for (int k = 0; k < kMaxXovers; ++k)
                c[k].set (std::tan (dsp::kPi * e->xoverHz (0, k) / kSr), dsp::kSqrt2);
            dsp::Lr4Allpass ap[kMaxXovers];
            double worst = 0.0;
            for (size_t i = 0; i + (size_t)lat < y.size (); ++i)
            {
                double v = x[i];
                for (int k = 0; k < kMaxXovers; ++k)
                    v = ap[k].tick (v, c[k]);
                worst = std::max (worst, std::fabs (v - (double)y[i + (size_t)lat]));
            }
            std::printf ("    %d bands, seed %3d (Low X %.0f Hz): %.3f .. %.3f dB, off the all-pass by %.1e\n", bands, seed,
                         e->lowXover (), lo, hi, worst);
            CHECK (lo > -0.1 && hi < 0.1, "%d bands, seed %d: flat within 0.1 dB (%.3f .. %.3f)", bands, seed, lo, hi);
            CHECK (worst < 1e-5, "%d bands, seed %d: the sum is the all-pass of the corners (%.1e)", bands, seed, worst);
        }
    // every band 6 dB up: flat at +6 dB
    auto e = engine ();
    plain (*e);
    e->setParam (kBandCount, kBands4);
    for (uint32_t id : kLevelIds)
        e->setParam (id, 6.0);
    e->reset ();
    std::vector<float> x (32768, 0.0f);
    x[0] = 1.0f;
    const auto y = run (*e, x);
    double worst = 0.0;
    for (double f : {40.0, 270.0, 900.0, 1500.0, 3000.0, 5000.0, 12000.0})
        worst = std::max (worst, std::fabs (responseDb (y, (size_t)e->latency (), f) - 6.0));
    CHECK (worst < 0.1, "every Level +6 dB: flat at +6 dB (%.3f off)", worst);
}

TEST (crossover_slopes_lr4)
{
    // each corner: the bands either side are -6 dB there and fall as a Linkwitz-Riley 4 (24 dB/oct) away
    struct Corner
    {
        const char* name;
        int below, above;
        std::function<void (Engine&)> set;
        std::function<double (Engine&)> fc;
    };
    const std::vector<Corner> corners = {
        {"Low X (seed 1)", kBandLow, kBandMid, [] (Engine& e) { e.setParam (kXoverMid, 3000.0); },
         [] (Engine& e) { return e.xoverHz (0, 0); }},
        {"Low X (seed 9)", kBandLow, kBandMid,
         [] (Engine& e) {
             e.setParam (kSeed, 9);
             e.setParam (kXoverMid, 3000.0);
         },
         [] (Engine& e) { return e.xoverHz (0, 0); }},
        {"Mid X", kBandMid, kBandHigh, [] (Engine& e) { e.setParam (kXoverMid, 3000.0); },
         [] (Engine& e) { return e.xoverHz (0, 1); }},
        {"High X", kBandHigh, kBandAir,
         [] (Engine& e) {
             e.setParam (kBandCount, kBands4);
             e.setParam (kXoverMid, 1000.0);
             e.setParam (kXoverHigh, 8000.0);
         },
         [] (Engine& e) { return e.xoverHz (0, 2); }},
    };
    for (const auto& c : corners)
    {
        auto e = engine ();
        plain (*e);
        c.set (*e);
        e->reset ();
        const double fc = c.fc (*e);
        const double belowAt = bandGainDb (c.below, fc, c.set), aboveAt = bandGainDb (c.above, fc, c.set);
        const double belowOut = bandGainDb (c.below, 2.0 * fc, c.set), aboveOut = bandGainDb (c.above, 0.5 * fc, c.set);
        const double belowIn = bandGainDb (c.below, 0.5 * fc, c.set), aboveIn = bandGainDb (c.above, std::min (2.0 * fc, 20000.0), c.set);
        std::printf ("    %s at %.0f Hz: %s %.2f / %s %.2f dB there; an octave out %.1f / %.1f dB (LR4 %.1f / %.1f), "
                     "an octave in %.2f / %.2f dB\n",
                     c.name, fc, kBandNames[c.below], belowAt, kBandNames[c.above], aboveAt, belowOut, aboveOut,
                     lr4Db (fc, 2.0 * fc, true), lr4Db (fc, 0.5 * fc, false), belowIn, aboveIn);
        CHECK (std::fabs (belowAt + 6.02) < 0.3 && std::fabs (aboveAt + 6.02) < 0.3, "%s: both sides -6 dB at the corner", c.name);
        CHECK (std::fabs (belowOut - lr4Db (fc, 2.0 * fc, true)) < 0.5, "%s: the lower band falls as LR4 (%.1f)", c.name, belowOut);
        CHECK (std::fabs (aboveOut - lr4Db (fc, 0.5 * fc, false)) < 0.5, "%s: the upper band falls as LR4 (%.1f)", c.name, aboveOut);
        CHECK (belowOut < -20.0 && aboveOut < -20.0, "%s: 24 dB/oct", c.name);
    }
}

TEST (low_x_seeded)
{
    // the Low crossover for every Seed: in 100 .. 500 Hz, spread over the range, the same every time
    int quarter[4] = {};
    double lo = 1e9, hi = 0.0;
    bool same = true, inRange = true;
    for (int s = kMinSeed; s <= kMaxSeed; ++s)
    {
        const double f = lowXoverForSeed (s);
        same = same && f == lowXoverForSeed (s) && f == makePattern (s, 0).lowXover && f == makePattern (s, 1).lowXover;
        inRange = inRange && f >= kLowXoverMin && f <= kLowXoverMax;
        lo = std::min (lo, f);
        hi = std::max (hi, f);
        ++quarter[std::clamp ((int)(4.0 * std::log (f / kLowXoverMin) / std::log (kLowXoverMax / kLowXoverMin)), 0, 3)];
    }
    std::printf ("    seeds 1 .. 128: %.1f .. %.1f Hz; per quarter of the range (log) %d / %d / %d / %d; seed 1: %.1f Hz\n", lo,
                 hi, quarter[0], quarter[1], quarter[2], quarter[3], lowXoverForSeed (1));
    CHECK (inRange, "every Low X within 100 .. 500 Hz");
    CHECK (same, "deterministic, and the same in both passes");
    CHECK (lo < 110.0 && hi > 450.0, "the range used: %.1f .. %.1f Hz", lo, hi);
    CHECK (*std::min_element (quarter, quarter + 4) >= 28, "spread evenly over the range");
    int distinct = 0;
    for (int s = kMinSeed + 1; s <= kMaxSeed; ++s)
        distinct += lowXoverForSeed (s) != lowXoverForSeed (s - 1);
    CHECK (distinct == kMaxSeed - kMinSeed, "each Seed its own");
    // the engine uses it
    for (int s : {1, 33, 100})
    {
        auto e = engine ();
        e->setParam (kSeed, s);
        e->reset ();
        CHECK (std::fabs (e->lowXover () / lowXoverForSeed (s) - 1.0) < 1e-12, "seed %d: the engine's Low X is the Seed's", s);
    }
}

TEST (low_band_locked)
{
    // a 25 Hz sine (the Low band's) at full movement, the deepest Depth, fast, both passes: its level never
    // moves, and neither does Low X
    for (int seed : {1, 5, 77, 128})
        for (int passes : {kPasses1, kPasses2})
        {
            auto e = engine ();
            plain (*e);
            e->setParam (kSeed, seed);
            e->setParam (kBandCount, kBands4);
            e->setParam (kMovement, 1.0);
            e->setParam (kDepth, 48.0);
            e->setParam (kRate, 2.0);
            e->setParam (kRise, 0.25);
            e->setParam (kPasses, passes);
            for (uint32_t id : {kLowMove, kMidMove, kHighMove, kAirMove})
                e->setParam (id, 1.0);
            e->reset ();
            const double lowX = e->lowXover ();
            bool still = true;
            double otherSpread = 0.0, otherMin[kMaxBands], otherMax[kMaxBands];
            std::fill (otherMin, otherMin + kMaxBands, 1e9);
            std::fill (otherMax, otherMax + kMaxBands, -1e9);
            const auto x = sine (25.0, 0.25, 8.0);
            const auto y = run (*e, x, nullptr, 256, [&] (size_t) {
                for (int k = 0; k < kMaxPasses; ++k)
                    still = still && e->bandGainDb (k, kBandLow) == 0.0 && e->xoverHz (k, 0) == lowX;
                for (int b = kBandMid; b < kMaxBands; ++b)
                {
                    otherMin[b] = std::min (otherMin[b], e->bandGainDb (0, b));
                    otherMax[b] = std::max (otherMax[b], e->bandGainDb (0, b));
                }
            });
            for (int b = kBandMid; b < kMaxBands; ++b)
                otherSpread = std::max (otherSpread, otherMax[b] - otherMin[b]);
            const auto env = envelope (y, (size_t)(0.5 * kSr), 3840); // (80 ms: two cycles)
            const double spread = *std::max_element (env.begin (), env.end ()) - *std::min_element (env.begin (), env.end ());
            std::printf ("    seed %3d, %d pass%s: Low's level spread %.3f dB (the other bands moved up to %.1f dB)\n", seed, passes + 1,
                         passes ? "es" : "", spread, otherSpread);
            CHECK (still, "seed %d: Low's gain and Low X never move", seed);
            CHECK (spread < 0.1, "seed %d: a low sine's level stays put (%.3f dB)", seed, spread);
            CHECK (otherSpread > 20.0, "seed %d: (while the others move: %.1f dB)", seed, otherSpread);
        }
}

TEST (bands_rise_and_fall)
{
    // a sine in each moving band (alone in its region) at Movement 100 %, its Move 100 %, Depth 24 dB: its
    // level rises and falls by about Depth, following the band's gain
    for (const auto& bs : bandSetups ())
    {
        int full = 0, tried = 0;
        for (int seed : {1, 2, 3, 4, 5, 6, 7, 8})
        {
            auto e = engine ();
            plain (*e);
            bs.set (*e);
            e->setParam (kSeed, seed);
            e->setParam (kMovement, 1.0);
            e->setParam (kDepth, 24.0);
            e->setParam (kRate, 0.3);
            e->setParam (kMoveIds[bs.band], 1.0);
            e->reset ();
            const size_t win = 480; // 10 ms
            std::vector<double> reported;
            const auto x = sine (bs.hz, 0.25, 30.0);
            const auto y = run (*e, x, nullptr, (int)win, [&] (size_t) { reported.push_back (e->bandGainDb (0, bs.band)); });
            const auto env = envelope (y, 0, win);
            // the tone's envelope against the band's gain (the window's start and end averaged)
            int off = 0, rises = 0, falls = 0;
            double lo = 1e9, hi = -1e9;
            for (size_t i = 50; i + 1 < env.size () && i + 1 < reported.size (); ++i)
            {
                const double g = 0.5 * (reported[i] + reported[i + 1]), level = env[i] - db (0.25);
                off += std::fabs (level - g) > 1.5;
                lo = std::min (lo, level);
                hi = std::max (hi, level);
                rises += reported[i] < -12.0 && reported[i + 1] >= -12.0;
                falls += reported[i] >= -12.0 && reported[i + 1] < -12.0;
            }
            const double range = hi - lo;
            ++tried;
            full += range > 22.0 && rises >= 2 && falls >= 2;
            std::printf ("    %s (%.0f Hz), seed %d: %.1f .. %.1f dB (%.1f dB), %d rises, %d falls; %d of %zu windows off "
                         "the gain\n",
                         kBandNames[bs.band], bs.hz, seed, lo, hi, range, rises, falls, off, env.size ());
            CHECK (range < 25.0 && hi < 0.6 && lo > -25.0, "%s, seed %d: within Level - Depth .. Level", kBandNames[bs.band], seed);
            CHECK (off < (int)env.size () / 50, "%s, seed %d: the tone follows the band's gain", kBandNames[bs.band], seed);
        }
        CHECK (full >= tried * 3 / 4, "%s: most seeds rise and fall the whole Depth, more than once (%d of %d)", kBandNames[bs.band],
               full, tried);
    }
    // Depth sets how far: 12 dB
    auto e = engine ();
    plain (*e);
    e->setParam (kMovement, 1.0);
    e->setParam (kDepth, 12.0);
    e->setParam (kMidMove, 1.0);
    e->reset ();
    double lo = 1e9, hi = -1e9;
    run (*e, std::vector<float> ((size_t)(30.0 * kSr), 0.0f), nullptr, 256, [&] (size_t) {
        lo = std::min (lo, e->bandGainDb (0, kBandMid));
        hi = std::max (hi, e->bandGainDb (0, kBandMid));
    });
    CHECK (std::fabs (lo + 12.0) < 0.2 && std::fabs (hi) < 0.2, "Depth 12 dB: Mid between -12 and 0 dB (%.2f .. %.2f)", lo, hi);
    // and the Move shares scale it: Movement 50 %, Mid Move 50 %: a quarter of Depth
    auto f = engine ();
    plain (*f);
    f->setParam (kMovement, 0.5);
    f->setParam (kMidMove, 0.5);
    f->reset ();
    lo = 1e9;
    run (*f, std::vector<float> ((size_t)(30.0 * kSr), 0.0f), nullptr, 256,
         [&] (size_t) { lo = std::min (lo, f->bandGainDb (0, kBandMid)); });
    CHECK (std::fabs (lo + 6.0) < 0.2, "Movement 50 %%, Mid Move 50 %%, Depth 24: Mid's floor at -6 dB (%.2f)", lo);
}

TEST (rise_fall_times)
{
    // clean rises (from the floor all the way up) and falls (from the top all the way down) of each moving
    // band, timed on its gain: they take the seeded times x Rise / Fall (within 10 % + 1.5 ms: the gain's
    // 1 ms smoothing and the 16-sample ticks)
    struct Stats
    {
        int count = 0;
        double worst = 0.0;
    };
    for (bool fall : {false, true})
        for (double scale : {0.25, 1.0, 4.0})
        {
            Stats st;
            double ratioSum = 0.0;
            for (int seed = 1; seed <= 6; ++seed)
            {
                auto e = engine (16);
                plain (*e);
                e->setParam (kSeed, seed);
                e->setParam (kBandCount, kBands4);
                e->setParam (kMovement, 1.0);
                e->setParam (kRate, 0.05); // (20 s cycles: room for the long ones)
                for (uint32_t id : {kMidMove, kHighMove, kAirMove})
                    e->setParam (id, 1.0);
                e->setParam (fall ? kFall : kRise, scale);
                e->reset ();
                const int ticks = (int)(80.0 * kSr / 16);
                std::vector<double> g[kMaxBands];
                const std::vector<float> z (16, 0.0f);
                std::vector<float> l (16), r (16);
                for (int t = 0; t < ticks; ++t)
                {
                    e->process (z.data (), z.data (), l.data (), r.data (), 16);
                    for (int b = kBandMid; b < kMaxBands; ++b)
                        g[b].push_back (e->bandGainDb (0, b));
                }
                for (int b = kBandMid; b < kMaxBands; ++b)
                {
                    const double expected = fall ? e->fallSeconds (0, b) : e->riseSeconds (0, b);
                    const double bottom = -24.0 + 0.05, top = -0.05;
                    const auto& v = g[b];
                    for (size_t i = 1; i < v.size (); ++i)
                    {
                        // a ramp's start: leaving the floor (rise) or the top (fall)
                        const bool start = fall ? (v[i - 1] >= top && v[i] < top) : (v[i - 1] <= bottom && v[i] > bottom);
                        if (!start)
                            continue;
                        size_t j = i;
                        bool clean = true;
                        while (j + 1 < v.size () && (fall ? v[j] > bottom : v[j] < top))
                        {
                            clean = clean && (fall ? v[j + 1] <= v[j] + 1e-9 : v[j + 1] >= v[j] - 1e-9);
                            ++j;
                        }
                        if (!clean || j + 1 >= v.size ())
                            continue;
                        // timed between 10 % and 90 % of the way (linearly between the ticks): 59 % of a
                        // cosine ramp (acos (0.8) / pi of it is under 10 %, as much over 90 %)
                        auto cross = [&] (double level) {
                            for (size_t k = i; k <= j; ++k)
                                if (fall ? v[k] <= level : v[k] >= level)
                                    return (double)(k - 1) + (level - v[k - 1]) / (v[k] - v[k - 1]);
                            return (double)j;
                        };
                        const double t10 = cross (fall ? -2.4 : -21.6), t90 = cross (fall ? -21.6 : -2.4);
                        const double took = (t90 - t10) * 16.0 / kSr, want = (1.0 - 2.0 * std::acos (0.8) / kPi) * expected;
                        const double slack = 0.1 * want + 0.0015;
                        if (std::fabs (took - want) > slack)
                            std::printf ("      seed %d %s: took %.4f s, want %.4f s\n", seed, kBandNames[b], took, want);
                        st.worst = std::max (st.worst, std::fabs (took - want) - slack);
                        ratioSum += took / want;
                        ++st.count;
                    }
                }
            }
            std::printf ("    %s x%.2f: %d clean %s timed, on average %.3f of the seeded time\n", fall ? "Fall" : "Rise", scale,
                         st.count, fall ? "falls" : "rises", st.count ? ratioSum / st.count : 0.0);
            CHECK (st.count >= 8, "%s x%.2f: enough clean ramps (%d)", fall ? "Fall" : "Rise", scale, st.count);
            CHECK (st.worst <= 0.0, "%s x%.2f: every ramp takes the seeded time x %.2f (within 10 %% + 1.5 ms)", fall ? "Fall" : "Rise",
                   scale, scale);
        }
    // the seeded times themselves: within their ranges, and Rise / Fall scale them
    bool inRange = true;
    for (int s = kMinSeed; s <= kMaxSeed; ++s)
        for (int k = 0; k < kMaxPasses; ++k)
        {
            const Pattern p = makePattern (s, k);
            for (int b = kBandMid; b < kMaxBands; ++b)
                inRange = inRange && p.band[b].rise >= kRiseMin && p.band[b].rise <= kRiseMax && p.band[b].fall >= kFallMin &&
                          p.band[b].fall <= kFallMax && p.band[b].mask != 0;
        }
    CHECK (inRange, "every seeded rise in 10 ms .. 1.5 s, every fall in 30 ms .. 3 s, every band rising somewhere");
    auto e = engine ();
    e->setParam (kRise, 4.0);
    e->setParam (kFall, 0.25);
    CHECK (e->riseSeconds (0, kBandMid) == 4.0 * e->pattern (0).band[kBandMid].rise &&
               e->fallSeconds (0, kBandMid) == 0.25 * e->pattern (0).band[kBandMid].fall,
           "Rise x4, Fall x0.25");
}

TEST (movement_zero_is_static)
{
    // Movement 0: every band at its Level, every corner where it is set, the whole time; the output is the
    // static split exactly, whatever the movement's other settings
    const auto x = reese (3.0);
    auto render = [&] (const std::function<void (Engine&)>& set, bool* still) {
        auto e = engine ();
        plain (*e);
        e->setParam (kSeed, 21);
        e->setParam (kBandCount, kBands4);
        e->setParam (kMidLevel, -3.0);
        e->setParam (kAirLevel, 4.0);
        if (set)
            set (*e);
        e->setParam (kMovement, 0.0);
        e->reset ();
        const double lowX = lowXoverForSeed (21);
        auto y = run (*e, x, nullptr, 256, [&] (size_t) {
            const double lv[kMaxBands] = {0.0, -3.0, 0.0, 4.0};
            for (int b = 0; b < kMaxBands; ++b)
                *still = *still && std::fabs (e->bandGainDb (0, b) - lv[b]) < 1e-9;
            *still = *still && std::fabs (e->xoverHz (0, 0) / lowX - 1.0) < 1e-12 &&
                     std::fabs (e->xoverHz (0, 1) / e->param (kXoverMid) - 1.0) < 1e-12 &&
                     std::fabs (e->xoverHz (0, 2) / e->param (kXoverHigh) - 1.0) < 1e-12;
        });
        return y;
    };
    bool still = true;
    const auto a = render ({}, &still);
    const auto b = render (
        [] (Engine& e) {
            e.setParam (kRise, 0.25);
            e.setParam (kFall, 4.0);
            e.setParam (kDepth, 48.0);
            e.setParam (kRate, 2.0);
            e.setParam (kMidMove, 0.1);
            e.setParam (kAirMove, 0.3);
        },
        &still);
    CHECK (still, "every band at its Level, every corner at its setting, the whole time");
    CHECK (a == b, "the movement's settings change nothing at Movement 0 (bit for bit)");
    // the static split by hand: the same corners, the same gains
    auto e = engine ();
    plain (*e);
    e->setParam (kSeed, 21);
    e->setParam (kBandCount, kBands4);
    e->reset ();
    dsp::SvfCoefs c[kMaxXovers];
    for (int k = 0; k < kMaxXovers; ++k)
        c[k].set (std::tan (dsp::kPi * e->xoverHz (0, k) / kSr), dsp::kSqrt2);
    const double gain[kMaxBands] = {1.0, std::pow (10.0, -3.0 / 20.0), 1.0, std::pow (10.0, 4.0 / 20.0)};
    dsp::Split4 split;
    const int lat = e->latency ();
    double worst = 0.0;
    for (size_t i = 0; i + (size_t)lat < x.size (); ++i)
    {
        double band[kMaxBands];
        split.tick (x[i], c, band);
        const double want = gain[0] * band[0] + gain[1] * band[1] + gain[2] * band[2] + gain[3] * band[3];
        worst = std::max (worst, std::fabs (want - (double)a[i + (size_t)lat]));
    }
    CHECK (worst < 1e-6, "the output is the static split (%.1e off)", worst);
}

TEST (seed_is_repeatable)
{
    const auto x = reese (2.0);
    auto render = [&] (int seed, int passes) {
        auto e = engine ();
        e->setParam (kSeed, seed);
        e->setParam (kMovement, 1.0);
        e->setParam (kPasses, passes);
        e->setParam (kBandCount, kBands4);
        e->reset ();
        std::vector<float> r;
        auto l = run (*e, x, &r);
        l.insert (l.end (), r.begin (), r.end ());
        return l;
    };
    const auto a = render (5, 0), b = render (5, 0), c = render (6, 0);
    CHECK (a == b, "the same Seed renders bit for bit the same");
    CHECK (a != c, "another Seed renders differently");
    double diff = 0.0;
    for (size_t i = 0; i < a.size (); ++i)
        diff = std::max (diff, (double)std::fabs (a[i] - c[i]));
    CHECK (diff > 0.01, "and audibly: %.3f", diff);
    CHECK (render (9, 1) == render (9, 1), "two passes too");
    // the patterns themselves: the same for the same Seed, a different stream for the second pass
    auto same = [] (const Pattern& p, const Pattern& q) {
        bool s = p.lowXover == q.lowXover;
        for (int b = kBandMid; b < kMaxBands; ++b)
            s = s && p.band[b].steps == q.band[b].steps && p.band[b].loop == q.band[b].loop && p.band[b].mask == q.band[b].mask &&
                p.band[b].offset == q.band[b].offset && p.band[b].hold == q.band[b].hold && p.band[b].rise == q.band[b].rise &&
                p.band[b].fall == q.band[b].fall;
        for (int k = 1; k < kMaxXovers; ++k)
            s = s && p.drift[k].rate == q.drift[k].rate && p.drift[k].key == q.drift[k].key;
        return s;
    };
    const Pattern p1 = makePattern (77, 0), p2 = makePattern (77, 0), q = makePattern (77, 1);
    CHECK (same (p1, p2), "makePattern is deterministic");
    CHECK (!same (p1, q) && p1.band[kBandHigh].rise != q.band[kBandHigh].rise && p1.drift[1].key != q.drift[1].key,
           "the second pass moves on its own");
    CHECK (p1.lowXover == q.lowXover, "with the same Low X");
    // different seeds deal out different patterns
    int differ = 0;
    for (int s = kMinSeed + 1; s <= kMaxSeed; ++s)
        differ += !same (makePattern (s, 0), makePattern (s - 1, 0));
    CHECK (differ == kMaxSeed - kMinSeed, "every Seed its own pattern");
}

TEST (transport_follows_song)
{
    // Sync: the phase is the song position over the cycle's beats, whatever came before
    auto e = engine ();
    e->setParam (kSync, 1.0);
    e->setParam (kSyncRate, 2); // 1 bar: 4 beats
    e->reset ();
    std::vector<float> in (256, 0.1f), l (256), r (256);
    e->setTransport (140.0, 0.0, false);
    for (int i = 0; i < 50; ++i)
        e->process (in.data (), in.data (), l.data (), r.data (), 256); // (stopped: running on its own)
    e->setTransport (140.0, 10.0, true);
    e->process (in.data (), in.data (), l.data (), r.data (), 256);
    const double expected = 10.0 / 4.0 + 256.0 * 140.0 / 60.0 / 4.0 / kSr;
    CHECK (std::fabs (e->phase () - expected) < 1e-9, "synced: the phase follows the song (%.6f, expected %.6f)", e->phase (), expected);

    // the bands' gains are a function of the song position: synced, two instances that ran differently
    // before agree from the moment the host plays
    auto synced = [&] (int warm) {
        auto f = engine ();
        f->setParam (kSync, 1.0);
        f->setParam (kSyncRate, 3); // 1/2: 2 beats
        f->setParam (kMovement, 1.0);
        f->reset ();
        std::vector<float> ll (256), rr (256);
        f->setTransport (128.0, 0.0, false);
        for (int i = 0; i < warm; ++i)
            f->process (in.data (), in.data (), ll.data (), rr.data (), 256);
        std::vector<double> gains;
        double ppq = 16.0;
        for (int i = 0; i < 400; ++i)
        {
            f->setTransport (128.0, ppq, true);
            f->process (in.data (), in.data (), ll.data (), rr.data (), 256);
            if (i >= 4) // (past the 1 ms glide from wherever it was)
                for (int b = kBandMid; b <= kBandHigh; ++b)
                    gains.push_back (f->bandGainDb (0, b));
            ppq += 256.0 * 128.0 / 60.0 / kSr;
        }
        return gains;
    };
    const auto sa = synced (0), sb = synced (333);
    double gap = 0.0;
    for (size_t i = 0; i < sa.size (); ++i)
        gap = std::max (gap, std::fabs (sa[i] - sb[i]));
    CHECK (gap < 1e-6, "synced: the bands rise and fall with the song position (%.1e dB apart)", gap);

    // free: two renders from the same song position give the same output, however long the first
    // instance ran before
    auto renderFrom = [&] (int warmBlocks) {
        auto f = engine ();
        f->setParam (kMovement, 1.0);
        f->reset ();
        const auto x = reese (1.0);
        std::vector<float> ll (256), rr (256);
        f->setTransport (128.0, 0.0, false);
        for (int i = 0; i < warmBlocks; ++i)
            f->process (x.data (), x.data (), ll.data (), rr.data (), 256);
        f->reset ();
        f->setParam (kMovement, 1.0);
        double ppq = 32.0;
        std::vector<float> outL;
        for (size_t a = 0; a + 256 <= x.size (); a += 256)
        {
            f->setTransport (128.0, ppq, true);
            f->process (x.data () + a, x.data () + a, ll.data (), rr.data (), 256);
            outL.insert (outL.end (), ll.begin (), ll.end ());
            ppq += 256.0 * 128.0 / 60.0 / kSr;
        }
        return outL;
    };
    const auto a = renderFrom (0), b = renderFrom (77);
    CHECK (a == b, "free: playback from the same song position renders the same");
    // and a jump on the timeline moves the phase there
    auto g = engine ();
    g->setTransport (120.0, 0.0, true);
    g->process (in.data (), in.data (), l.data (), r.data (), 256);
    g->setTransport (120.0, 64.0, true); // (a jump: 64 beats is 32 s at 120 BPM)
    g->process (in.data (), in.data (), l.data (), r.data (), 256);
    const double rate = toPlain (kRate, defaultNormalized (kRate));
    CHECK (std::fabs (g->phase () - (32.0 * rate + 256.0 * rate / kSr)) < 1e-9, "a jump: the phase follows (%.4f)", g->phase ());
    // host stopped: running on its own at Rate
    auto h = engine ();
    h->setTransport (120.0, 50.0, false);
    for (int i = 0; i < 10; ++i)
        h->process (in.data (), in.data (), l.data (), r.data (), 256);
    CHECK (std::fabs (h->phase () - 2560.0 * rate / kSr) < 1e-9, "stopped: free-running from where it was (%.5f)", h->phase ());
}

TEST (band_count_switch)
{
    // a tone in the Air band's region; Air off. Switching to 4 bands fades the tone out over 20 ms (Air at its
    // own Level instead of following High), without a click; and back
    auto e = engine ();
    plain (*e);
    e->setParam (kAirLevel, kLevelOffDb);
    e->setParam (kXoverHigh, 2000.0); // (9 kHz well inside Air)
    e->reset ();
    CHECK (e->bandCount () == 3, "3 bands by default");
    const auto x = sine (9000.0, 0.25, 3.0);
    const size_t at = 256 * 192, back = 256 * 384; // (block starts)
    const auto y = run (*e, x, nullptr, 256, [&] (size_t a) {
        if (a == at)
            e->setParam (kBandCount, kBands4);
        if (a == back)
            e->setParam (kBandCount, kBands3);
    });
    const int lat = e->latency ();
    double steadyStep = 0.0, switchStep = 0.0;
    for (size_t i = (size_t)(0.5 * kSr); i < at; ++i)
        steadyStep = std::max (steadyStep, (double)std::fabs (y[i] - y[i - 1]));
    for (size_t i = at; i < y.size (); ++i)
        switchStep = std::max (switchStep, (double)std::fabs (y[i] - y[i - 1]));
    const size_t s = at + (size_t)lat;
    const double before = rms (y, s - 4800, s), during = rms (y, s + 240, s + 480), after = rms (y, s + 1440, s + 4800);
    const double restored = rms (y, back + lat + 2400, back + lat + 7200);
    std::printf ("    3 -> 4 bands: %.1f dBFS before, %.1f dBFS 5 .. 10 ms in, %.1f dBFS after 30 ms; back: %.1f dBFS; "
                 "largest step %.4f (steady %.4f)\n",
                 db (before), db (during), db (after), db (restored), switchStep, steadyStep);
    CHECK (switchStep <= steadyStep * 1.01, "no click: no step larger than the tone's own (%.4f / %.4f)", switchStep, steadyStep);
    CHECK (during < before * 0.9 && during > before * 0.2, "it fades (%.2f of the level 5 .. 10 ms in)", during / before);
    CHECK (after < before * 1e-2, "Air off with 4 bands: the tone is gone (%.1f dB)", db (after / before));
    CHECK (std::fabs (db (restored / before)) < 0.1, "back to 3 bands: Air follows High again");
    // the meters' band count follows
    Meters m;
    auto f = engine ();
    f->setMeters (&m);
    f->setParam (kBandCount, kBands4);
    std::vector<float> z (256, 0.0f), l (256), r (256);
    f->process (z.data (), z.data (), l.data (), r.data (), 256);
    CHECK (m.bands.load () == 4 && f->bandCount () == 4, "4 bands reported");
}

TEST (passes)
{
    const auto x = reese (4.0);
    auto render = [&] (int passes) {
        auto e = engine ();
        e->setParam (kPasses, passes);
        e->reset ();
        return run (*e, x);
    };
    const auto one = render (kPasses1), two = render (kPasses2);
    const size_t a = (size_t)(0.5 * kSr);
    double diff = 0.0;
    for (size_t i = a; i < x.size (); ++i)
        diff = std::max (diff, (double)std::fabs (one[i] - two[i]));
    const double l1 = db (rms (one, a, x.size ())), l2 = db (rms (two, a, x.size ()));
    std::printf ("    level: 1 pass %.1f dBFS, 2 passes %.1f dBFS (input %.1f)\n", l1, l2, db (rms (x, a, x.size ())));
    CHECK (diff > 0.01, "2 passes differ from 1: %.3f", diff);
    CHECK (finite (two) && peak (two, 0, two.size ()) < 2.0, "2 passes stay finite and bounded");
    CHECK (l2 > -40.0 && l2 < 0.0, "and at a sensible level: %.1f dBFS", l2);
    // the second pass keeps the Low crossover (the low end locked in both) but moves on its own
    for (int seed : {1, 64, 100})
    {
        auto e = engine ();
        e->setParam (kSeed, seed);
        e->setParam (kPasses, kPasses2);
        e->setParam (kMovement, 1.0);
        e->setParam (kBandCount, kBands4);
        e->reset ();
        bool lowSame = true;
        double apart = 0.0;
        run (*e, x, nullptr, 256, [&] (size_t) {
            lowSame = lowSame && e->xoverHz (1, 0) == e->xoverHz (0, 0) && e->bandGainDb (1, kBandLow) == e->bandGainDb (0, kBandLow);
            for (int b = kBandMid; b < kMaxBands; ++b)
                apart = std::max (apart, std::fabs (e->bandGainDb (1, b) - e->bandGainDb (0, b)));
        });
        CHECK (lowSame && e->xoverHz (1, 0) == e->lowXover (), "seed %d: the second pass keeps Low X and Low's level", seed);
        CHECK (apart > 6.0, "seed %d: its other bands move on their own (%.1f dB apart)", seed, apart);
    }
    // switching Passes crossfades: no jump bigger than the signal's own steps
    auto e = engine ();
    e->reset ();
    std::vector<float> l (x.size ()), r (x.size ());
    double maxStep = 0.0;
    for (size_t b = 0; b < x.size (); b += 256)
    {
        if (b == (size_t)(2.0 * kSr))
            e->setParam (kPasses, kPasses2);
        e->process (x.data () + b, x.data () + b, l.data () + b, r.data () + b, 256);
    }
    for (size_t i = (size_t)(1.0 * kSr); i < x.size (); ++i)
        maxStep = std::max (maxStep, (double)std::fabs (l[i] - l[i - 1]));
    CHECK (maxStep < 0.5, "switching Passes does not click: largest step %.3f", maxStep);
}

TEST (glue_reduces_crest)
{
    // a Reese swelling between -20 dB and +1 dB (1.5 times a second): the compressor evens the swells out,
    // so the crest factor falls
    std::vector<float> x = reese (2.0);
    for (size_t i = 0; i < x.size (); ++i)
        x[i] *= (float)(0.1 + 1.0 * (0.5 - 0.5 * std::cos (2.0 * kPi * 1.5 * (double)i / kSr)));
    auto crest = [&] (double glue) {
        auto e = engine ();
        plain (*e);
        e->setParam (kGlue, glue);
        e->reset ();
        const auto y = run (*e, x);
        const size_t a = (size_t)(0.4 * kSr);
        return db (peak (y, a, y.size ()) / rms (y, a, y.size ()));
    };
    const double off = crest (0.0), on = crest (1.0), half = crest (0.5);
    std::printf ("    crest factor: Glue 0 %.2f dB, 50 %% %.2f dB, 100 %% %.2f dB\n", off, half, on);
    CHECK (on < off - 2.0 && half < off, "Glue lowers the crest factor");
    auto e = engine ();
    plain (*e);
    e->setParam (kGlue, 1.0);
    e->reset ();
    run (*e, x);
    CHECK (e->glueReductionDb () > 1.0, "and reports its gain reduction: %.1f dB", e->glueReductionDb ());
}

TEST (grit_adds_harmonics)
{
    // a 50 Hz sine through the Low band alone: its third harmonic with Grit 0 and 100 %
    auto third = [] (double grit) {
        auto e = engine ();
        plain (*e);
        e->setParam (kGrit, grit);
        e->reset ();
        solo (*e, kBandLow);
        const auto x = sine (50.0, 0.5, 0.5);
        const auto y = run (*e, x);
        const size_t a = (size_t)(0.25 * kSr);
        return db (toneAt (y, 150.0, a, y.size ()) / toneAt (y, 50.0, a, y.size ()));
    };
    const double clean = third (0.0), dirty = third (1.0);
    std::printf ("    3rd harmonic: %.1f dB (Grit 0), %.1f dB (Grit 100 %%)\n", clean, dirty);
    CHECK (dirty > clean + 30.0 && dirty > -40.0, "Grit adds harmonics");
    // and the soft clipper's aliasing stays low: a 9 kHz sine driven hard, energy away from its harmonics
    dsp::Saturator s;
    s.maxDb = 24.0;
    s.setAmount (1.0);
    s.reset ();
    std::vector<double> l (48000), r (48000);
    for (size_t i = 0; i < l.size (); ++i)
        l[i] = r[i] = 0.5 * std::sin (2.0 * kPi * 9000.0 * (double)i / kSr);
    for (size_t a = 0; a < l.size (); a += 16)
        s.process (l.data () + a, r.data () + a, 16);
    std::vector<float> y (l.begin (), l.end ());
    // 27 kHz folds to 21 kHz, 45 kHz to 3 kHz: the alias at 3 kHz against the fundamental
    const double alias = db (toneAt (y, 3000.0, 4800, y.size ()) / toneAt (y, 9000.0, 4800, y.size ()));
    std::printf ("    a 9 kHz sine clipped hard: its 3 kHz alias at %.1f dB\n", alias);
    CHECK (alias < -30.0, "the clipper is anti-aliased: %.1f dB", alias);
}

TEST (mix_zero_is_dry)
{
    auto e = engine ();
    e->setParam (kMix, 0.0);
    e->setParam (kMovement, 1.0);
    e->setParam (kPasses, kPasses2);
    e->reset ();
    const int lat = e->latency ();
    const auto x = noise (0.5, 1.0, 11);
    const auto y = run (*e, x);
    bool same = true;
    for (size_t i = (size_t)lat; i < x.size () && same; ++i)
        same = y[i] == x[i - (size_t)lat];
    std::printf ("    latency %d samples (the end saturator's)\n", lat);
    CHECK (same, "Mix 0: the input bit for bit, %d samples late", lat);
    smacheratr::Tail t;
    t.prepare (kSr, 512);
    CHECK (lat == t.latency (), "the latency is the end saturator's alone: %d", lat);
}

TEST (silence_and_extremes)
{
    // silence in, silence out (exactly)
    {
        auto e = engine ();
        e->setParam (kPasses, kPasses2);
        e->setParam (kMovement, 1.0);
        e->setParam (kBandCount, kBands4);
        e->reset ();
        const std::vector<float> x (48000, 0.0f);
        std::vector<float> r;
        const auto l = run (*e, x, &r);
        CHECK (peak (l, 0, l.size ()) == 0.0 && peak (r, 0, r.size ()) == 0.0, "silence in, silence out");
    }
    // the hottest settings, moving fast with the shortest ramps, two passes, four bands: an impulse and a
    // burst of loud noise ring out, never past a bound, and leave no denormals or NaNs
    {
        auto e = engine ();
        e->setParam (kBandCount, kBands4);
        e->setParam (kMovement, 1.0);
        e->setParam (kRate, 2.0);
        e->setParam (kRise, 0.25);
        e->setParam (kFall, 0.25);
        e->setParam (kDepth, 48.0);
        e->setParam (kPasses, kPasses2);
        e->setParam (kDrive, 1.0);
        e->setParam (kGrit, 1.0);
        e->setParam (kGlue, 1.0);
        e->setParam (kXoverMid, 400.0);  // (below Low X for some seeds: kept apart)
        e->setParam (kXoverHigh, 16000.0);
        for (uint32_t id : kLevelIds)
            e->setParam (id, 12.0);
        e->reset ();
        std::vector<float> x ((size_t)(6.0 * kSr), 0.0f);
        x[100] = 1.0f;
        const auto loud = noise (1.0, 1.0, 5);
        std::copy (loud.begin (), loud.end (), x.begin () + 1000);
        std::vector<float> r;
        bool ordered = true;
        const auto l = run (*e, x, &r, 256, [&] (size_t) {
            for (int k = 0; k < kMaxPasses; ++k)
                ordered = ordered && e->xoverHz (k, 0) < e->xoverHz (k, 1) && e->xoverHz (k, 1) < e->xoverHz (k, 2) &&
                          e->xoverHz (k, 2) < 0.5 * kSr;
        });
        const double top = std::max (peak (l, 0, l.size ()), peak (r, 0, r.size ()));
        const double tailLevel = std::max (peak (l, l.size () - 4800, l.size ()), peak (r, r.size () - 4800, r.size ()));
        int denormals = 0;
        for (size_t i = 0; i < l.size (); ++i)
        {
            const float tiny = std::numeric_limits<float>::min ();
            denormals += (l[i] != 0.0f && std::fabs (l[i]) < tiny) || (r[i] != 0.0f && std::fabs (r[i]) < tiny);
        }
        std::printf ("    hottest: peak %.2f, the last 0.1 s at %.1f dBFS\n", top, db (tailLevel));
        CHECK (ordered, "the corners stay in order, below Nyquist");
        CHECK (finite (l) && finite (r), "finite");
        CHECK (top < 4.0, "bounded: %.2f", top);
        CHECK (tailLevel < 1e-5, "rings out: %.2e", tailLevel);
        CHECK (denormals == 0, "no denormals (%d)", denormals);
    }
}

TEST (shift_moves_upper_bands)
{
    // a sine in the Mid band (alone in its region), Shift on: it moves by Shift (one sideband, the other well
    // down), Mix blends it with the unshifted one
    auto shifted = [] (double hz, double shift, double mix, double probe) {
        auto e = engine ();
        plain (*e);
        e->setParam (kXoverMid, 6000.0);
        e->setParam (kShiftOn, 1.0);
        e->setParam (kShift, shift);
        e->setParam (kShiftMix, mix);
        e->reset ();
        const auto x = sine (hz, 0.25, 1.0);
        const auto y = run (*e, x);
        return db (toneAt (y, probe, (size_t)(0.3 * kSr), y.size ()) / 0.25);
    };
    for (double shift : {200.0, -300.0, 450.0})
    {
        const double moved = shifted (1500.0, shift, 1.0, 1500.0 + shift), mirror = shifted (1500.0, shift, 1.0, 1500.0 - shift),
                     left = shifted (1500.0, shift, 1.0, 1500.0);
        std::printf ("    1500 Hz, Shift %+.0f Hz: %.2f dB at %.0f Hz, the other sideband %.1f dB, the original %.1f dB\n", shift,
                     moved, 1500.0 + shift, mirror, left);
        CHECK (std::fabs (moved) < 0.5, "Shift %+.0f: the tone moved, at its level (%.2f dB)", shift, moved);
        CHECK (mirror < -30.0 && left < -30.0, "Shift %+.0f: a single sideband (%.1f / %.1f dB)", shift, mirror, left);
    }
    const double half = shifted (1500.0, 200.0, 0.5, 1700.0), halfDry = shifted (1500.0, 200.0, 0.5, 1500.0);
    CHECK (std::fabs (half + 6.02) < 0.5 && std::fabs (halfDry + 6.02) < 0.5, "Shift Mix 50 %%: half shifted, half not (%.2f / %.2f dB)",
           half, halfDry);
    // Shift 0 (and Shift Mix 0): the bands' levels as without the shifter (only its all-pass phase)
    for (double probe : {1500.0, 3000.0, 7000.0})
    {
        auto level = [&] (bool on, double shift, double mix) {
            auto e = engine ();
            plain (*e);
            e->setParam (kShiftOn, on ? 1.0 : 0.0);
            e->setParam (kShift, shift);
            e->setParam (kShiftMix, mix);
            e->reset ();
            const auto x = sine (probe, 0.25, 0.6);
            const auto y = run (*e, x);
            return db (toneAt (y, probe, (size_t)(0.3 * kSr), y.size ()) / 0.25);
        };
        const double off = level (false, 0.0, 1.0), zero = level (true, 0.0, 1.0), dry = level (true, 250.0, 0.0);
        CHECK (std::fabs (zero - off) < 0.1 && std::fabs (dry - off) < 0.1, "%.0f Hz: Shift 0 / Mix 0 leave its level (%.3f / %.3f dB)",
               probe, zero - off, dry - off);
    }
}

TEST (shift_leaves_the_low_end)
{
    // the Low band never goes through the shifter: a sine below Low X with only Low sounding is bit for bit
    // the same with the shifter on and off; with every band on its level stays
    for (bool soloLow : {true, false})
    {
        auto render = [&] (bool on) {
            auto e = engine ();
            e->setParam (kShiftOn, on ? 1.0 : 0.0);
            e->setParam (kShift, -350.0);
            e->setParam (kMovement, 1.0);
            e->setParam (kPasses, kPasses2);
            if (soloLow)
                solo (*e, kBandLow);
            e->reset ();
            return run (*e, sine (40.0, 0.4, 2.0));
        };
        const auto off = render (false), on = render (true);
        if (soloLow)
            CHECK (on == off, "Low alone: the shifter changes nothing (bit for bit)");
        else
        {
            const size_t a = (size_t)(0.5 * kSr);
            const double d = db (toneAt (on, 40.0, a, on.size ()) / toneAt (off, 40.0, a, off.size ()));
            CHECK (std::fabs (d) < 0.1, "every band on: the 40 Hz sine's level stays (%.3f dB)", d);
        }
    }
    // a shift down never reaches the sub region or DC: 700 Hz shifted down by 500 Hz (to 200 Hz, under Low X)
    int lowSeed = 1;
    for (int s = kMinSeed; s <= kMaxSeed; ++s)
        if (lowXoverForSeed (s) < lowXoverForSeed (lowSeed))
            lowSeed = s;
    for (int seed : {lowSeed, 1})
    {
        auto e = engine ();
        plain (*e);
        e->setParam (kSeed, seed);
        e->setParam (kShiftOn, 1.0);
        e->setParam (kShift, -500.0);
        e->reset ();
        const auto x = sine (700.0, 0.25, 2.0);
        const auto y = run (*e, x);
        double mean = 0.0;
        for (size_t i = (size_t)kSr; i < y.size (); ++i)
            mean += y[i];
        mean /= (double)(y.size () - (size_t)kSr);
        const double at200 = db (toneAt (y, 200.0, (size_t)kSr, y.size ()) / 0.25);
        std::printf ("    seed %d (Low X %.0f Hz): 700 Hz shifted to 200 Hz at %.1f dB, DC %.1e\n", seed, lowXoverForSeed (seed), at200,
                     mean);
        CHECK (finite (y) && std::fabs (mean) < 1e-4, "seed %d: no DC", seed);
        const double expect = lr4Db (lowXoverForSeed (seed), 700.0, false) +
                              db (1.0 / std::sqrt (1.0 + std::pow (std::tan (kPi * lowXoverForSeed (seed) / kSr) / std::tan (kPi * 200.0 / kSr), 4.0)));
        CHECK (std::fabs (at200 - expect) < 1.0, "seed %d: high-passed at Low X (%.1f dB, expected %.1f)", seed, at200, expect);
    }
}

TEST (shift_off_and_smooth)
{
    // off: the engine without it, bit for bit, whatever Shift and Shift Mix say
    const auto x = reese (2.0);
    auto render = [&] (const std::function<void (Engine&)>& set) {
        auto e = engine ();
        e->setParam (kMovement, 1.0);
        e->setParam (kPasses, kPasses2);
        e->setParam (kBandCount, kBands4);
        if (set)
            set (*e);
        e->reset ();
        return run (*e, x);
    };
    const auto plainRender = render ({});
    CHECK (plainRender == render ([] (Engine& e) {
               e.setParam (kShift, 321.0);
               e.setParam (kShiftMix, 0.3);
           }),
           "Shift off: bit for bit the engine without it");
    // on, then off again: back to the same output once faded out (the shifter not run; the split alone,
    // so no compressor remembers the shifted signal)
    {
        auto e = engine ();
        plain (*e);
        e->setParam (kShiftOn, 1.0);
        e->setParam (kShift, 100.0);
        e->reset ();
        auto f = engine ();
        plain (*f);
        const auto s = sine (2000.0, 0.25, 1.0);
        std::vector<float> l (256), r (256), l2 (256), r2 (256);
        bool same = true;
        for (size_t a = 0; a + 256 <= s.size (); a += 256)
        {
            if (a == 256 * 20)
                e->setParam (kShiftOn, 0.0);
            e->process (s.data () + a, s.data () + a, l.data (), r.data (), 256);
            f->process (s.data () + a, s.data () + a, l2.data (), r2.data (), 256);
            if (a > 256 * 40)
                same = same && std::fabs (l[0] - l2[0]) < 1e-6 && e->shiftAmount () == 0.0;
        }
        CHECK (same, "switched off: faded out and off");
    }
    // moving Shift and switching it: no clicks (no step larger than the fastest tone's own)
    auto e = engine ();
    plain (*e);
    e->setParam (kXoverMid, 6000.0);
    e->reset ();
    const auto s = sine (1500.0, 0.25, 3.0);
    const auto y = run (*e, s, nullptr, 128, [&] (size_t a) {
        if (a == (size_t)(0.5 * kSr))
            e->setParam (kShiftOn, 1.0);
        if (a == (size_t)(0.5 * kSr))
            e->setParam (kShift, 300.0);
        if (a == (size_t)(1.5 * kSr))
            e->setParam (kShift, -300.0);
        if (a == (size_t)(2.5 * kSr))
            e->setParam (kShiftOn, 0.0);
    });
    double worst = 0.0;
    for (size_t i = 1000; i < y.size (); ++i)
        worst = std::max (worst, (double)std::fabs (y[i] - y[i - 1]));
    const double bound = 0.25 * 2.0 * kPi * 1800.0 / kSr;
    std::printf ("    switching and moving Shift: largest step %.4f (a 1800 Hz tone's own %.4f)\n", worst, bound);
    CHECK (worst < 1.1 * bound, "no clicks (%.4f)", worst);
    CHECK (finite (y), "finite");
}

// ---- 0.22: Low Push / Low Dip, Seed B / Seed Blend, Density, Speed, Drop Out ---------------------------------

namespace {
// 0.21's BandMotion::lift, verbatim (the reference the new one must match bit for bit at Density x1)
double lift021 (const BandMotion& m, double theta, double secPerCycle, double riseSec, double fallSec)
{
    const double spc = std::max (secPerCycle, 1e-6), r = std::max (riseSec, 1e-6), f = std::max (fallSec, 1e-6);
    const double stepSec = spc / m.steps;
    const double upEnd = std::max (r, m.hold * stepSec);
    const double end = upEnd + f;
    const double u = theta * m.steps - m.offset;
    int64_t j = (int64_t)std::floor (u);
    double best = 0.0;
    for (int guard = 0; guard < 8192; ++guard, --j)
    {
        const double dt = (u - (double)j) * stepSec;
        if (dt > end)
            break;
        if (!m.risesOn (j))
            continue;
        const double v = dt < r ? dt / r : (dt < upEnd ? 1.0 : 1.0 - (dt - upEnd) / f);
        if (v > best)
        {
            best = v;
            if (best >= 1.0)
                break;
        }
    }
    best = std::clamp (best, 0.0, 1.0);
    return 0.5 - 0.5 * std::cos (kPi * best);
}

// the new parameters at values other than their defaults
void extreme (Engine& e)
{
    e.setParam (kSeedB, 40);
    e.setParam (kSeedBlend, 0.5);
    e.setParam (kDensity, 8.0);
    e.setParam (kSpeed, 16.0);
    e.setParam (kDropOut, 1.0);
    e.setParam (kLowPush, 12.0);
    e.setParam (kLowDip, 6.0);
}

// renders silence tick by tick (16 samples), calling `at (tick)` before each; returns the largest change of
// a band's gain (dB) from one tick to the next (and every tick's gain in `gains`)
double steepestTick (Engine& e, int pass, int band, int ticks, const std::function<void (int)>& at = {},
                     std::vector<double>* gains = nullptr)
{
    const std::vector<float> z (16, 0.0f);
    std::vector<float> l (16), r (16);
    double prev = e.bandGainDb (pass, band), worst = 0.0;
    for (int t = 0; t < ticks; ++t)
    {
        if (at)
            at (t);
        e.process (z.data (), z.data (), l.data (), r.data (), 16);
        const double g = e.bandGainDb (pass, band);
        worst = std::max (worst, std::fabs (g - prev));
        prev = g;
        if (gains)
            gains->push_back (g);
    }
    return worst;
}

// the moving bands at full movement with slow seeded ramps (Rise and Fall x4), for the crossfade tests
void slowFull (Engine& e, uint32_t which, int value, double blend)
{
    plain (e);
    e.setParam (kBandCount, kBands4);
    e.setParam (kMovement, 1.0);
    e.setParam (kDepth, 48.0);
    e.setParam (kRise, 4.0);
    e.setParam (kFall, 4.0);
    for (uint32_t id : {kMidMove, kHighMove, kAirMove})
        e.setParam (id, 1.0);
    e.setParam (kSeedBlend, blend);
    e.setParam (kSeed, 3);
    e.setParam (kSeedB, 4);
    e.setParam (which, value);
    e.reset ();
}

double maxStep (const std::vector<float>& y)
{
    double step = 0.0;
    for (size_t i = 1000; i < y.size (); ++i)
        step = std::max (step, (double)std::fabs (y[i] - y[i - 1]));
    return step;
}
} // namespace

TEST (new_params_default_is_021)
{
    // the lift at Density x1 is 0.21's, bit for bit, for every seed, pass and moving band, over a spread of
    // phases, cycle lengths and Rise / Fall scales
    int same = 0, tried = 0;
    for (int s = kMinSeed; s <= kMaxSeed; ++s)
        for (int k = 0; k < kMaxPasses; ++k)
        {
            const Pattern p = makePattern (s, k);
            for (int b = kBandMid; b < kMaxBands; ++b)
                for (double spc : {0.5, 3.3, 20.0})
                    for (double sc : {0.25, 1.0, 4.0})
                        for (int i = 0; i < 40; ++i)
                        {
                            const double th = 0.013 + i * 0.377 + s * 0.01;
                            const BandMotion& m = p.band[b];
                            ++tried;
                            same += m.lift (th, spc, m.rise * sc, m.fall * sc) == lift021 (m, th, spc, m.rise * sc, m.fall * sc);
                        }
        }
    CHECK (same == tried, "the lift is 0.21's bit for bit (%d of %d)", same, tried);
    // Seed B's pattern: its moving bands are Seed B's own, with Seed's Low crossover
    const Pattern b = makePattern (9, 0, 5), b0 = makePattern (9, 0);
    CHECK (b.lowXover == lowXoverForSeed (5) && b.band[kBandHigh].mask == b0.band[kBandHigh].mask &&
               b.band[kBandHigh].rise == b0.band[kBandHigh].rise,
           "Seed B's pattern keeps Seed's Low crossover");
    // the engine: the new parameters set to their defaults (or set elsewhere and back) render exactly as never
    // set, with 3 and 4 bands, 2 passes and the shifter on
    const auto x = reese (3.0);
    for (int bands : {kBands3, kBands4})
    {
        auto render = [&] (int how) {
            auto e = engine ();
            e->setParam (kBandCount, bands);
            e->setParam (kPasses, kPasses2);
            e->setParam (kShiftOn, 1.0);
            e->setParam (kShift, 80.0);
            e->setParam (kMovement, 0.9);
            e->setParam (kSeed, 12);
            if (how >= 1)
            {
                if (how == 2)
                    extreme (*e);
                for (uint32_t id : {kSeedB, kSeedBlend, kDensity, kLowPush, kLowDip, kDropOut, kSpeed})
                    e->setParam (id, toPlain (id, defaultNormalized (id)));
            }
            e->reset ();
            std::vector<float> r;
            auto l = run (*e, x, &r);
            l.insert (l.end (), r.begin (), r.end ());
            return l;
        };
        const auto never = render (0);
        CHECK (never == render (1), "%d bands: the new parameters at their defaults change nothing (bit for bit)", bands + 3);
        CHECK (never == render (2), "%d bands: set and back to the defaults: nothing left over", bands + 3);
    }
    // at Blend 0 Seed B is not heard, even changed while playing
    auto a = engine (), c = engine ();
    for (Engine* e : {a.get (), c.get ()})
    {
        e->setParam (kMovement, 1.0);
        e->reset ();
    }
    const auto s1 = run (*a, x);
    const auto s2 = run (*c, x, nullptr, 256, [&] (size_t at) {
        if (at == 256 * 40)
            c->setParam (kSeedB, 77);
    });
    CHECK (s1 == s2, "Seed B changed at Blend 0: nothing changes");
}

TEST (low_push_and_dip)
{
    // a 25 Hz sine (the Low band's): with Low Push it rises up to Push dB above its Level, with Low Dip it
    // dips at most Dip dB below (both x Movement); Low X never moves
    struct Case
    {
        double push, dip, movement;
    };
    for (const Case cs : {Case {12.0, 0.0, 1.0}, Case {6.0, 6.0, 1.0}, Case {0.0, 6.0, 1.0}, Case {12.0, 6.0, 0.5}})
    {
        int reachedTop = 0, reachedBottom = 0, seeds = 0;
        for (int seed : {1, 5, 77, 128})
        {
            auto e = engine ();
            plain (*e);
            e->setParam (kSeed, seed);
            e->setParam (kMovement, cs.movement);
            e->setParam (kRate, 0.5);
            e->setParam (kLowPush, cs.push);
            e->setParam (kLowDip, cs.dip);
            e->setParam (kPasses, kPasses2);
            e->reset ();
            const double lowX = e->lowXover ();
            bool xStill = true, within = true;
            double lo = 1e9, hi = -1e9;
            const double up = cs.push * cs.movement, down = cs.dip * cs.movement;
            const auto y = run (*e, sine (25.0, 0.25, 12.0), nullptr, 256, [&] (size_t) {
                for (int k = 0; k < kMaxPasses; ++k)
                {
                    const double g = e->bandGainDb (k, kBandLow);
                    xStill = xStill && e->xoverHz (k, 0) == lowX;
                    within = within && g <= up + 1e-9 && g >= -down - 1e-9;
                }
                lo = std::min (lo, e->bandGainDb (0, kBandLow));
                hi = std::max (hi, e->bandGainDb (0, kBandLow));
            });
            const auto env = envelope (y, (size_t)(0.5 * kSr), 1920); // (40 ms: a cycle of 25 Hz)
            const double eHi = *std::max_element (env.begin (), env.end ()) - db (0.25);
            const double eLo = *std::min_element (env.begin (), env.end ()) - db (0.25);
            std::printf ("    Push %.0f, Dip %.0f, Movement %.0f %%, seed %3d: Low's gain %.2f .. %.2f dB, the tone %.2f .. %.2f "
                         "dB (2 passes)\n",
                         cs.push, cs.dip, 100.0 * cs.movement, seed, lo, hi, eLo, eHi);
            CHECK (xStill, "seed %d: Low X never moves", seed);
            CHECK (within, "seed %d: Low's gain within Level - Dip .. Level + Push (x Movement)", seed);
            // (two passes: the tone goes through both, so up to twice each)
            CHECK (eHi < 2.0 * up + 0.5 && eLo > -2.0 * down - 0.5, "seed %d: the tone within (%.2f .. %.2f)", seed, eLo, eHi);
            ++seeds;
            reachedTop += up <= 0.0 || hi > up - 0.3;
            reachedBottom += down <= 0.0 || lo < -down + 0.3;
        }
        CHECK (reachedTop == seeds && reachedBottom >= seeds - 1, "Push %.0f / Dip %.0f: reached (%d / %d of %d)", cs.push, cs.dip,
               reachedTop, reachedBottom, seeds);
    }
}

TEST (seed_blend)
{
    // Blend 1: the moving bands move as Seed B's pattern (Low X stays Seed's); Blend 0.5: both patterns overlap,
    // so the bands are up more of the time (higher on average than either alone)
    auto lifts = [] (int seed, int seedB, double blend, std::vector<double>* gains = nullptr, double* lowX = nullptr) {
        auto e = engine ();
        plain (*e);
        e->setParam (kBandCount, kBands4);
        e->setParam (kPasses, kPasses2);
        e->setParam (kMovement, 1.0);
        for (uint32_t id : {kMidMove, kHighMove, kAirMove})
            e->setParam (id, 1.0);
        e->setParam (kSeed, seed);
        e->setParam (kSeedB, seedB);
        e->setParam (kSeedBlend, blend);
        e->reset ();
        std::vector<double> v;
        run (*e, std::vector<float> ((size_t)(40.0 * kSr), 0.0f), nullptr, 256, [&] (size_t) {
            for (int k = 0; k < kMaxPasses; ++k)
                for (int b = kBandMid; b < kMaxBands; ++b)
                {
                    v.push_back (e->bandLift (k, b));
                    if (gains)
                        gains->push_back (e->bandGainDb (k, b));
                }
        });
        if (lowX)
            *lowX = e->lowXover ();
        return v;
    };
    double xA = 0, xB = 0;
    std::vector<double> gOne, gPlainB;
    lifts (5, 9, 1.0, &gOne, &xA);
    lifts (9, 1, 0.0, &gPlainB, &xB);
    CHECK (gOne == gPlainB, "Blend 1: the moving bands exactly as Seed B's pattern");
    CHECK (std::fabs (xA / lowXoverForSeed (5) - 1.0) < 1e-9 && std::fabs (xB / lowXoverForSeed (9) - 1.0) < 1e-9, "with Seed's Low X (%.1f Hz; Seed B's own would be %.1f Hz)", xA, xB);
    int more = 0, pairs = 0;
    for (auto [s, sb] : {std::pair {5, 9}, std::pair {1, 2}, std::pair {23, 64}, std::pair {100, 7}})
    {
        const auto la = lifts (s, sb, 0.0), lb = lifts (s, sb, 1.0), lm = lifts (s, sb, 0.5);
        double ma = 0, mb = 0, mm = 0;
        bool atLeast = true;
        for (size_t i = 0; i < la.size (); ++i)
        {
            ma += la[i];
            mb += lb[i];
            mm += lm[i];
            atLeast = atLeast && lm[i] >= std::max (la[i], lb[i]) - 1e-9;
        }
        ma /= (double)la.size ();
        mb /= (double)lb.size ();
        mm /= (double)lm.size ();
        std::printf ("    seeds %d / %d: average lift %.3f (Seed), %.3f (Seed B), %.3f (Blend 0.5)\n", s, sb, ma, mb, mm);
        CHECK (atLeast, "seeds %d / %d: at 0.5 a band is up whenever either pattern has it up", s, sb);
        ++pairs;
        more += mm > std::max (ma, mb) + 0.02;
    }
    CHECK (more == pairs, "Blend 0.5 is up more than either pattern alone (%d of %d)", more, pairs);
    // and louder: noise (the split alone) at Blend 0.5 against each pattern alone
    auto level = [] (double blend) {
        auto e = engine ();
        plain (*e);
        e->setParam (kMovement, 1.0);
        e->setParam (kMidMove, 1.0);
        e->setParam (kSeed, 23);
        e->setParam (kSeedB, 64);
        e->setParam (kSeedBlend, blend);
        e->reset ();
        const auto y = run (*e, noise (0.25, 20.0));
        return db (rms (y, 0, y.size ()));
    };
    const double l0 = level (0.0), l1 = level (1.0), lh = level (0.5);
    std::printf ("    noise: %.2f dB (Seed), %.2f dB (Seed B), %.2f dB (Blend 0.5)\n", l0, l1, lh);
    CHECK (lh > std::max (l0, l1) + 1.0, "Blend 0.5 louder than either alone");
}

TEST (seed_change_crossfades)
{
    // changing Seed or Seed B while playing crossfades over 100 ms: no band's gain jumps. Slow seeded ramps
    // (Rise and Fall x4) so the movement's own steps are small; without the crossfade a band would jump by up
    // to Depth (48 dB) within a tick or two.
    int jumps = 0, tried = 0;
    double worstAll = 0.0;
    for (auto [which, from, to, blend] : {std::tuple {kSeed, 1, 2, 0.0}, std::tuple {kSeed, 5, 90, 0.0},
                                          std::tuple {kSeedB, 7, 33, 0.5}, std::tuple {kSeedB, 12, 3, 1.0}})
        for (int when : {3000, 9000, 20000})
            for (int b = kBandMid; b < kMaxBands; ++b)
            {
                // how far the band has to go: the gains either side of the change, from engines that never change
                auto f = engine (16), g = engine (16), e = engine (16);
                slowFull (*f, which, from, blend);
                slowFull (*g, which, to, blend);
                std::vector<double> gf, gg;
                steepestTick (*f, 0, b, when + 1, {}, &gf);
                steepestTick (*g, 0, b, when + 1, {}, &gg);
                jumps += std::fabs (gf.back () - gg.back ()) > 12.0;
                slowFull (*e, which, from, blend);
                const double worst = steepestTick (*e, 0, b, when + 6000, [&] (int t) {
                    if (t == when)
                        e->setParam (which, to);
                });
                ++tried;
                worstAll = std::max (worstAll, worst);
            }
    std::printf ("    %d changes (%d of them moving a band by over 12 dB): the steepest tick %.2f dB\n", tried, jumps, worstAll);
    CHECK (jumps >= 3, "(changes that move bands a long way: %d)", jumps);
    CHECK (worstAll < 2.0, "no jumps: at most %.2f dB a tick", worstAll);
    // and no click in the sound: a 1.5 kHz tone in the Mid band, Seed changed every 250 ms
    auto e = engine ();
    plain (*e);
    e->setParam (kXoverMid, 6000.0);
    e->setParam (kMovement, 1.0);
    e->setParam (kMidMove, 1.0);
    e->setParam (kDepth, 48.0);
    e->reset ();
    const auto y = run (*e, sine (1500.0, 0.25, 4.0), nullptr, 128, [&] (size_t a) {
        if (a % 12032 == 0 && a > 0)
            e->setParam (kSeed, 1 + (int)(a / 128) % 100);
    });
    const double step = maxStep (y), bound = 0.25 * 2.0 * kPi * 1500.0 / kSr;
    std::printf ("    a Seed change every 250 ms: largest step %.4f (the tone's own %.4f)\n", step, bound);
    CHECK (step < 1.1 * bound, "no clicks (%.4f)", step);
}

TEST (drop_out)
{
    // Drop Out at a full Depth (Movement and High Move 100 %): a fallen band goes silent (under -90 dB); off,
    // it falls 48 dB; with a shallower fall (under 30 dB) Drop Out changes nothing
    auto render = [] (bool dropOut, double depth, double* lowest, double* highest) {
        auto e = engine ();
        plain (*e);
        e->setParam (kXoverMid, 1000.0);
        e->setParam (kMovement, 1.0);
        e->setParam (kHighMove, 1.0);
        e->setParam (kDepth, depth);
        e->setParam (kDropOut, dropOut ? 1.0 : 0.0);
        e->setParam (kRate, 0.3);
        solo (*e, kBandHigh);
        const auto x = sine (6000.0, 0.25, 12.0);
        double gLo = 1e9;
        const auto y = run (*e, x, nullptr, 256, [&] (size_t) { gLo = std::min (gLo, e->bandGainDb (0, kBandHigh)); });
        const auto env = envelope (y, (size_t)(0.5 * kSr), 480);
        *lowest = *std::min_element (env.begin (), env.end ()) - db (0.25);
        *highest = *std::max_element (env.begin (), env.end ()) - db (0.25);
        std::printf ("    Drop Out %s, Depth %.0f dB: the tone %.1f .. %.1f dB (the band's gain down to %.1f dB)\n", dropOut ? "on" : "off",
                     depth, *lowest, *highest, gLo);
        return y;
    };
    double lo, hi;
    render (true, 48.0, &lo, &hi);
    CHECK (lo < -90.0 && hi > -1.0, "on, Depth 48: silent when fallen (%.1f dB), back up to its Level (%.1f dB)", lo, hi);
    render (false, 48.0, &lo, &hi);
    CHECK (lo > -50.0 && lo < -46.0, "off, Depth 48: down 48 dB (%.1f dB)", lo);
    double lo2, hi2;
    const auto a = render (true, 24.0, &lo, &hi), b = render (false, 24.0, &lo2, &hi2);
    CHECK (a == b, "Depth 24: Drop Out changes nothing (bit for bit)");
    // fast drop outs (Speed x16, Density x4), Drop Out switched off and on again: no clicks (no step larger
    // than the tone's own)
    auto e = engine ();
    plain (*e);
    e->setParam (kXoverMid, 6000.0);
    e->setParam (kMovement, 1.0);
    e->setParam (kMidMove, 1.0);
    e->setParam (kDepth, 48.0);
    e->setParam (kDropOut, 1.0);
    e->setParam (kSpeed, 16.0);
    e->setParam (kRise, 0.25);
    e->setParam (kFall, 0.25);
    e->setParam (kDensity, 4.0);
    solo (*e, kBandMid);
    const auto y = run (*e, sine (1500.0, 0.25, 6.0), nullptr, 256, [&] (size_t at) {
        if (at == 256 * 560)
            e->setParam (kDropOut, 0.0);
        if (at == 256 * 840)
            e->setParam (kDropOut, 1.0);
    });
    const double step = maxStep (y), bound = 0.25 * 2.0 * kPi * 1500.0 / kSr;
    std::printf ("    fast drop outs, switched off and on: largest step %.4f (the tone's own %.4f)\n", step, bound);
    CHECK (step < 1.25 * bound, "no clicks (%.4f)", step);
}

TEST (speed_fast_ramps)
{
    // Speed x16 with Rise and Fall x0.25: every rise and fall a few ms (at least 1 ms), timed on the gain;
    // the gain still moves in smooth ramps (a tone's samples step no further than its own slope allows)
    auto e = engine (16);
    plain (*e);
    e->setParam (kBandCount, kBands4);
    e->setParam (kMovement, 1.0);
    e->setParam (kSpeed, 16.0);
    e->setParam (kRise, 0.25);
    e->setParam (kFall, 0.25);
    for (uint32_t id : {kMidMove, kHighMove, kAirMove})
        e->setParam (id, 1.0);
    e->reset ();
    double longest = 0.0, shortest = 1e9;
    for (int k = 0; k < kMaxPasses; ++k)
        for (int b = kBandLow; b < kMaxBands; ++b)
            for (double t : {e->riseSeconds (k, b), e->fallSeconds (k, b)})
            {
                longest = std::max (longest, t);
                shortest = std::min (shortest, t);
            }
    std::printf ("    Speed x16, Rise / Fall x0.25: ramps of %.2f .. %.2f ms\n", 1000.0 * shortest, 1000.0 * longest);
    CHECK (shortest >= kMinRampSec && longest <= kFallMax * 0.25 / 16.0 + 1e-9, "every ramp 1 .. 47 ms");
    // clean falls from the top to the floor take Fall's time, timed between 10 % and 90 % of the way (59 % of
    // a cosine ramp; within 10 % + 2 ms: the ticks and the gain's 1 ms smoothing)
    int clean = 0;
    double worst = 0.0;
    for (int b = kBandMid; b < kMaxBands; ++b)
    {
        auto f = engine (16);
        plain (*f);
        for (uint32_t id : {kBandCount, kMovement, kSpeed, kRise, kFall, kMidMove, kHighMove, kAirMove})
            f->setParam (id, e->param (id));
        f->reset ();
        std::vector<double> g;
        steepestTick (*f, 0, b, (int)(30.0 * kSr / 16), {}, &g);
        const double want = (1.0 - 2.0 * std::acos (0.8) / kPi) * f->fallSeconds (0, b);
        int n = 0;
        for (size_t i = 1; i < g.size (); ++i)
            if (g[i - 1] >= -0.05 && g[i] < -0.05)
            {
                size_t j = i;
                while (j + 1 < g.size () && g[j] > -23.95 && g[j + 1] <= g[j] + 1e-9)
                    ++j;
                if (g[j] > -23.95)
                    continue;
                auto cross = [&] (double level) {
                    for (size_t k = i; k <= j; ++k)
                        if (g[k] <= level)
                            return (double)(k - 1) + (level - g[k - 1]) / (g[k] - g[k - 1]);
                    return (double)j;
                };
                const double took = (cross (-21.6) - cross (-2.4)) * 16.0 / kSr;
                worst = std::max (worst, std::fabs (took - want) - (0.1 * want + 0.002));
                ++n;
            }
        clean += n;
        std::printf ("    %s: %d clean falls of %.2f ms\n", kBandNames[b], n, 1000.0 * f->fallSeconds (0, b));
    }
    CHECK (clean >= 4 && worst <= 0.0, "clean falls take Fall's time (%d; %.4f s past the slack)", clean, worst);
    // the sound: a 1.5 kHz tone in the Mid band, every ramp 1 .. 10 ms, the densest events
    auto f = engine ();
    plain (*f);
    f->setParam (kXoverMid, 6000.0);
    f->setParam (kMovement, 1.0);
    f->setParam (kMidMove, 1.0);
    f->setParam (kDepth, 48.0);
    f->setParam (kSpeed, 16.0);
    f->setParam (kRise, 0.25);
    f->setParam (kFall, 0.25);
    f->setParam (kDensity, 8.0);
    solo (*f, kBandMid);
    const auto y = run (*f, sine (1500.0, 0.25, 6.0));
    const auto env = envelope (y, 4800, 240);
    const double step = maxStep (y), bound = 0.25 * 2.0 * kPi * 1500.0 / kSr;
    const double range = *std::max_element (env.begin (), env.end ()) - *std::min_element (env.begin (), env.end ());
    std::printf ("    1.5 kHz tone: moves %.1f dB in 5 ms windows, largest step %.4f (the tone's own %.4f)\n", range, step, bound);
    CHECK (range > 40.0, "it still moves the whole Depth (%.1f dB)", range);
    CHECK (step < 1.25 * bound, "no clicks (%.4f)", step);
    CHECK (finite (y), "finite");
}

TEST (density_events)
{
    // Density x8: about 8 times the rises per cycle (counted on each band's lift, with short ramps so the rises
    // stay apart), x0.25 fewer; the same events every time for the same Seed
    auto rises = [] (double density, int seed, int band) {
        auto e = engine ();
        e->setParam (kMovement, 1.0);
        e->setParam (kRate, 0.05);
        e->setParam (kSpeed, 16.0);
        e->setParam (kRise, 0.25);
        e->setParam (kFall, 0.25);
        e->setParam (kSeed, seed);
        e->setParam (kDensity, density);
        e->reset ();
        int n = 0;
        double prev = 0.0;
        const int perCycle = 40000; // (0.5 ms steps of a 20 s cycle)
        for (int i = 0; i < 4 * perCycle; ++i)
        {
            const double v = e->liftAt (0, band, (double)i / perCycle);
            n += prev < 0.5 && v >= 0.5;
            prev = v;
        }
        return n;
    };
    int ok = 0, tried = 0;
    for (int seed : {1, 2, 3, 4, 5, 6})
        for (int band : {kBandLow, kBandMid, kBandHigh})
        {
            const int r1 = rises (1.0, seed, band), r8 = rises (8.0, seed, band), rq = rises (0.25, seed, band);
            const double ratio = (double)r8 / std::max (r1, 1);
            std::printf ("    seed %d, %s: %d rises in 4 cycles at x1, %d at x8 (x%.2f), %d at x0.25\n", seed, kBandNames[band], r1, r8,
                         ratio, rq);
            ++tried;
            ok += ratio > 6.5 && ratio < 8.5 && rq <= r1;
            CHECK (r8 == rises (8.0, seed, band), "deterministic");
        }
    CHECK (ok >= tried - 1, "Density x8: about 8 times the rises (%d of %d)", ok, tried);
}

TEST (extremes_deterministic_and_clean)
{
    // every new control at its most extreme, 4 bands, 2 passes, the shifter on: the same render twice, another
    // Seed B renders differently, and the hottest input leaves no NaNs or denormals
    const auto x = reese (3.0);
    auto render = [&] (int seedB) {
        auto e = engine ();
        e->setParam (kBandCount, kBands4);
        e->setParam (kPasses, kPasses2);
        e->setParam (kShiftOn, 1.0);
        e->setParam (kShift, -60.0);
        e->setParam (kMovement, 1.0);
        e->setParam (kDepth, 48.0);
        extreme (*e);
        e->setParam (kSeedB, seedB);
        e->reset ();
        std::vector<float> r;
        auto l = run (*e, x, &r);
        l.insert (l.end (), r.begin (), r.end ());
        return l;
    };
    const auto a = render (40);
    CHECK (a == render (40), "the same settings render bit for bit the same");
    CHECK (a != render (41), "another Seed B renders differently");
    CHECK (finite (a), "finite");
    auto e = engine ();
    e->setParam (kBandCount, kBands4);
    e->setParam (kPasses, kPasses2);
    e->setParam (kMovement, 1.0);
    e->setParam (kDepth, 48.0);
    e->setParam (kRate, 2.0);
    e->setParam (kDrive, 1.0);
    e->setParam (kGrit, 1.0);
    e->setParam (kGlue, 1.0);
    for (uint32_t id : kLevelIds)
        e->setParam (id, 12.0);
    extreme (*e);
    e->reset ();
    std::vector<float> in ((size_t)(4.0 * kSr), 0.0f);
    in[100] = 1.0f;
    const auto loud = noise (1.0, 1.0, 9);
    std::copy (loud.begin (), loud.end (), in.begin () + 1000);
    std::vector<float> r;
    const auto l = run (*e, in, &r, 256, [&] (size_t at) {
        if (at == 256 * 100)
            e->setParam (kSeed, 99); // (a crossfade on the way)
    });
    int denormals = 0;
    for (size_t i = 0; i < l.size (); ++i)
    {
        const float tiny = std::numeric_limits<float>::min ();
        denormals += (l[i] != 0.0f && std::fabs (l[i]) < tiny) || (r[i] != 0.0f && std::fabs (r[i]) < tiny);
    }
    const double top = std::max (peak (l, 0, l.size ()), peak (r, 0, r.size ()));
    CHECK (finite (l) && finite (r), "finite");
    CHECK (denormals == 0, "no denormals (%d)", denormals);
    CHECK (top < 4.0, "bounded: %.2f", top);
}

TEST (cpu_budget)
{
    // 10 s of a stereo Reese, the defaults and the heaviest settings (4 bands, 2 passes, full movement at the
    // fastest Rate with the longest ramps, the end saturator on): CPU time, the best of three renders
    const auto x = reese (10.0);
    for (bool heavy : {false, true})
    {
        double secs = 1e9;
        std::vector<float> l;
        for (int i = 0; i < 3; ++i)
        {
            auto e = engine ();
            if (heavy)
            {
                e->setParam (kBandCount, kBands4);
                e->setParam (kPasses, kPasses2);
                e->setParam (kMovement, 1.0);
                e->setParam (kRate, 2.0);
                e->setParam (kRise, 4.0);
                e->setParam (kFall, 4.0);
                e->setParam (kAirMove, 1.0);
                e->setParam (kMidMove, 1.0);
                e->setParam (kDrive, 0.5);
                e->setParam (kGlue, 1.0);
                e->setParam (kGrit, 1.0);
                e->setParam (kShiftOn, 1.0);
                e->setParam (kShift, 120.0);
                e->setParam (kTailBase + pk::kTailOn, 1.0);
                // and the 0.22 controls: both patterns overlapping, the densest, fastest movement, the Low band moving
                e->setParam (kSeedBlend, 0.5);
                e->setParam (kDensity, 8.0);
                e->setParam (kSpeed, 16.0);
                e->setParam (kDropOut, 1.0);
                e->setParam (kLowPush, 12.0);
                e->setParam (kLowDip, 6.0);
            }
            e->reset ();
            const std::clock_t t0 = std::clock ();
            l = run (*e, x, nullptr, 512);
            secs = std::min (secs, (double)(std::clock () - t0) / CLOCKS_PER_SEC);
        }
        CHECK (finite (l), "finite");
        std::printf ("    CPU: %.2f%% of one core (%s)\n", 100.0 * secs / 10.0,
                     heavy ? "4 bands, 2 passes, full movement, Seed Blend, Density x8, Speed x16, Low Push / Dip, Shift on, the end saturator on" : "the defaults");
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
