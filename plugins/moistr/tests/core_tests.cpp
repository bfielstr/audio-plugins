// Headless tests for the Moistr DSP. Run: ./moistr_tests [filter]
// The split (flat sum, LR4 slopes at the set corners, the seeded Low crossover), the movement (Low locked,
// the other bands rising and falling by Depth, the seeded rise and fall times x Rise / Fall, still at 0,
// Seed, the host's transport), switching Bands, the Glue compressor, Grit, Mix, Passes, silence, the 0.22
// controls (Low Push / Dip, Seed B / Blend, Density, Speed, Drop Out; all at their defaults 0.21's sound bit
// for bit), Link and Liquid (0.23: off, 0.22's sound bit for bit; the Liquid preset's statistics on a detuned
// bass, pinned), the gestures (0.27: the curves, Loop and Walk, the transport, every target and none of them
// touching the Low band, Wobble's phase, the files; off: 0.26's sound bit for bit; they still play in a 0.27
// project), the one gesture (0.30: the factory gestures, every lane at the same place on one clock, Loop and Walk
// keeping every lane continuous, Amount 0 or None 0.29's sound bit for bit, the lanes' ranges, the sub and the
// stereo image kept, the files of lanes), the LAB (0.30: empty, 0.29's sound bit for bit; its paths lined up; mono kept,
// the sub untouched; Mute, Solo, Level and Mono) and the CPU budget.
#include "pluginkit/testing/CpuClock.h"
#include "Dsp.h"
#include "Engine.h"
#include "Gesture.h"
#include "GestureFile.h"
#include "Movement.h"
#include "Params.h"
#include "Sweep.h"

#include "pluginkit/PresetStore.h"

#include <algorithm>
#include <complex>
#include <fstream>
#include <sstream>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
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

// moistr as it was before 0.24: the SWEEP stage off, Drive, Movement, Glue and Grit at their old defaults (the
// tests written before 0.24 start from here; a state saved before 0.24 reads these too)
void legacy (Engine& e)
{
    for (uint32_t id : {kDrive, kMovement, kGlue, kGrit, kSweep})
        e.setParam (id, toPlain (id, legacyDefaultNormalized (id)));
}
std::unique_ptr<Engine> engine (int block = 512)
{
    auto e = std::make_unique<Engine> ();
    legacy (*e);
    e->setParam (kTailBase + pk::kTailOn, 0.0); // (the end saturator, on by default since 0.25: the tests are about moistr's own processing)
    e->prepare (kSr, block);
    return e;
}
// a new instance: every parameter at its default (0.24: the SWEEP stage on, the rest neutral)
std::unique_ptr<Engine> fresh (double sr = kSr, int block = 512)
{
    auto e = std::make_unique<Engine> ();
    e->prepare (sr, block);
    return e;
}
// the end saturator out of the way (its Saturator and Gentlr off: it passes the engine's output through)
void tailNeutral (Engine& e)
{
    e.setParam (kTailBase + pk::kTailOn, 0.0);
    e.setParam (kTailExtBase + pk::kTailExtClarity, 0.0);
    e.reset ();
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

// ---- Link and Liquid (0.23) ------------------------------------------------------------------------------
namespace {
// the Liquid preset's test input: two detuned saws (F1, a little flat, and 0.4 Hz above it at half the level), mono
std::vector<float> detunedBass (double seconds)
{
    std::vector<float> x ((size_t)(seconds * kSr));
    double p1 = 0.0, p2 = 0.3;
    for (auto& v : x)
    {
        p1 += 43.2 / kSr;
        p2 += 43.6 / kSr;
        p1 -= std::floor (p1);
        p2 -= std::floor (p2);
        v = (float)(0.25 * ((2.0 * p1 - 1.0) + 0.5 * (2.0 * p2 - 1.0)));
    }
    return x;
}

// x through a band-pass: a 4th-order Butterworth high-pass at lo and low-pass at hi (TPT sections), forwards
// and then backwards (zero phase, as scipy's sosfiltfilt)
std::vector<double> bandPass (const std::vector<float>& x, double lo, double hi)
{
    constexpr double kQ[2] = {0.54119610014619698, 1.3065629648763766};
    dsp::SvfCoefs ch[2], cl[2];
    dsp::Svf sh[2], sl[2];
    for (int i = 0; i < 2; ++i)
    {
        ch[i].set (std::tan (kPi * lo / kSr), 1.0 / kQ[i]);
        cl[i].set (std::tan (kPi * hi / kSr), 1.0 / kQ[i]);
    }
    std::vector<double> y (x.begin (), x.end ());
    for (int dir = 0; dir < 2; ++dir)
    {
        for (int i = 0; i < 2; ++i)
        {
            sh[i].reset ();
            sl[i].reset ();
        }
        for (double& v : y)
        {
            for (int i = 0; i < 2; ++i)
                v = sh[i].tick (v, ch[i]).hp;
            for (int i = 0; i < 2; ++i)
                v = sl[i].tick (v, cl[i]).lp;
        }
        std::reverse (y.begin (), y.end ());
    }
    return y;
}

// the 50 ms RMS envelope (dB) of a band of x, from `from`
std::vector<double> bandEnvelope (const std::vector<float>& x, double lo, double hi, size_t from)
{
    const auto y = bandPass (x, lo, hi);
    const size_t win = (size_t)(0.05 * kSr);
    std::vector<double> e;
    for (size_t a = from; a + win <= y.size (); a += win)
    {
        double s = 0.0;
        for (size_t i = a; i < a + win; ++i)
            s += y[i] * y[i];
        e.push_back (db (std::sqrt (s / (double)win)));
    }
    return e;
}

double percentile (std::vector<double> v, double q)
{
    std::sort (v.begin (), v.end ());
    const double at = q / 100.0 * (double)(v.size () - 1);
    const size_t i = (size_t)at;
    return i + 1 < v.size () ? v[i] + (v[i + 1] - v[i]) * (at - (double)i) : v.back ();
}
double stdDev (const std::vector<double>& v)
{
    double m = 0.0, s = 0.0;
    for (double a : v)
        m += a;
    m /= (double)v.size ();
    for (double a : v)
        s += (a - m) * (a - m);
    return std::sqrt (s / (double)v.size ());
}
double correlation (const std::vector<double>& a, const std::vector<double>& b)
{
    const size_t n = std::min (a.size (), b.size ());
    double ma = 0, mb = 0;
    for (size_t i = 0; i < n; ++i)
    {
        ma += a[i];
        mb += b[i];
    }
    ma /= (double)n;
    mb /= (double)n;
    double sab = 0, saa = 0, sbb = 0;
    for (size_t i = 0; i < n; ++i)
    {
        sab += (a[i] - ma) * (b[i] - mb);
        saa += (a[i] - ma) * (a[i] - ma);
        sbb += (b[i] - mb) * (b[i] - mb);
    }
    return sab / std::sqrt (std::max (saa * sbb, 1e-30));
}

// an in-place radix-2 FFT (the size a power of two)
void fft (std::vector<std::complex<double>>& a)
{
    const size_t n = a.size ();
    for (size_t i = 1, j = 0; i < n; ++i)
    {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j)
            std::swap (a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1)
    {
        const std::complex<double> w (std::cos (-2.0 * kPi / (double)len), std::sin (-2.0 * kPi / (double)len));
        for (size_t i = 0; i < n; i += len)
        {
            std::complex<double> wn (1.0, 0.0);
            for (size_t k = 0; k < len / 2; ++k, wn *= w)
            {
                const auto u = a[i + k], v = a[i + k + len / 2] * wn;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
            }
        }
    }
}

// the resonance: every 0.25 s, where a 0.1 s frame's spectrum (Hann, smoothed over a third of an octave in dB)
// is highest between 250 Hz and 4 kHz
std::vector<double> resonancePeaks (const std::vector<float>& x, size_t from)
{
    const size_t frame = (size_t)(0.1 * kSr), nfft = 16384, hop = (size_t)(0.25 * kSr);
    const double binHz = kSr / (double)nfft;
    std::vector<double> peaks;
    for (size_t a = from; a + frame <= x.size (); a += hop)
    {
        std::vector<std::complex<double>> buf (nfft);
        for (size_t i = 0; i < frame; ++i)
            buf[i] = x[a + i] * (0.5 - 0.5 * std::cos (2.0 * kPi * (double)i / (double)(frame - 1)));
        fft (buf);
        std::vector<double> cum (nfft / 2 + 1, 0.0);
        for (size_t k = 0; k < nfft / 2; ++k)
            cum[k + 1] = cum[k] + 20.0 * std::log10 (std::abs (buf[k]) + 1e-12);
        double best = -1e30, bestHz = 0.0;
        for (size_t k = (size_t)std::ceil (250.0 / binHz); (double)k * binHz <= 4000.0; ++k)
        {
            const double f = (double)k * binHz;
            const size_t lo = (size_t)std::ceil (f * std::exp2 (-1.0 / 6.0) / binHz);
            const size_t hi = (size_t)std::floor (f * std::exp2 (1.0 / 6.0) / binHz);
            const double v = (cum[hi + 1] - cum[lo]) / (double)(hi + 1 - lo);
            if (v > best)
            {
                best = v;
                bestHz = f;
            }
        }
        peaks.push_back (bestHz);
    }
    return peaks;
}

// a factory preset's values, set on the engine
bool loadPreset (Engine& e, const char* rel)
{
    std::ifstream in (std::string (MOISTR_PRESETS_DIR) + "/" + rel);
    std::stringstream ss;
    ss << in.rdbuf ();
    pk::presets::FactoryPreset fp;
    std::string err;
    if (!pk::presets::parseFactoryPreset (ss.str (), rel, paramTable (), fp, err))
    {
        std::printf ("    %s: %s\n", rel, err.c_str ());
        return false;
    }
    for (auto& [id, n] : fp.values)
        e.setParam (id, toPlain (id, n));
    return true;
}

// renders silence tick by tick (16 samples), calling `each` after every tick
void ticks (Engine& e, int n, const std::function<void (int)>& each)
{
    const std::vector<float> z (16, 0.0f);
    std::vector<float> l (16), r (16);
    for (int t = 0; t < n; ++t)
    {
        e.process (z.data (), z.data (), l.data (), r.data (), 16);
        each (t);
    }
}
} // namespace

TEST (link_liquid_default_is_022)
{
    // Link and Liquid at their defaults (never set, set, or set elsewhere and back) render bit for bit the same,
    // with 3 and 4 bands, 2 passes, the shifter on and Seed Blend. (That path is 0.22's: renders like these
    // were compared bit for bit with 0.22.0's when Link and Liquid were added.)
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
            e->setParam (kSeedBlend, 0.5);
            e->setParam (kSeed, 21);
            if (how == 2)
            {
                e->setParam (kLink, 0.8);
                e->setParam (kLiquid, 1.0);
                e->setParam (kLiquidRes, 0.9);
                e->setParam (kLiquidLow, 600.0);
                e->setParam (kLiquidHigh, 3000.0);
            }
            if (how >= 1)
                for (uint32_t id : {kLink, kLiquid, kLiquidRes, kLiquidLow, kLiquidHigh})
                    e->setParam (id, toPlain (id, defaultNormalized (id)));
            e->reset ();
            std::vector<float> r;
            auto l = run (*e, x, &r);
            l.insert (l.end (), r.begin (), r.end ());
            return l;
        };
        const auto never = render (0);
        CHECK (never == render (1), "%d bands: Link and Liquid at their defaults change nothing (bit for bit)", bands + 3);
        CHECK (never == render (2), "%d bands: set and back to the defaults: nothing left over", bands + 3);
    }
    const Pattern a = makePattern (17, 1);
    CHECK (a.liquid.steps >= 2 && a.liquid.steps <= 6, "Liquid's path is dealt (%d steps)", a.liquid.steps);
}

TEST (link_moves_bands_together)
{
    // 4 bands at full movement, 2 passes: at Link 100 % Mid, High and Air rise and fall together (the same lift
    // every tick, each at its own Level and Move); at 0 each has its own; at 50 % each is half way to the Mid
    // band's. The Low band is never linked.
    for (int seed : {3, 40, 101})
    {
        auto setup = [&] (Engine& e, double link) {
            plain (e);
            e.setParam (kBandCount, kBands4);
            e.setParam (kMovement, 1.0);
            e.setParam (kDepth, 48.0);
            e.setParam (kRate, 1.0);
            for (uint32_t id : {kMidMove, kHighMove, kAirMove})
                e.setParam (id, 1.0);
            e.setParam (kPasses, kPasses2);
            e.setParam (kSeed, seed);
            e.setParam (kLink, link);
            e.reset ();
        };
        auto f0 = engine (), f1 = engine (), fh = engine ();
        setup (*f0, 0.0);
        setup (*f1, 1.0);
        setup (*fh, 0.5);
        bool together = true, half = true;
        std::vector<double> mid0, high0, mid1, high1;
        const std::vector<float> z (16, 0.0f);
        std::vector<float> l (16), r (16);
        for (int t = 0; t < 9000; ++t) // (3 s)
        {
            for (Engine* e : {f0.get (), f1.get (), fh.get ()})
                e->process (z.data (), z.data (), l.data (), r.data (), 16);
            for (int k = 0; k < kMaxPasses; ++k)
            {
                const double m1 = f1->bandLift (k, kBandMid);
                together = together && std::fabs (f1->bandLift (k, kBandHigh) - m1) < 1e-12 && std::fabs (f1->bandLift (k, kBandAir) - m1) < 1e-12;
                for (int b : {kBandHigh, kBandAir})
                {
                    const double want = 0.5 * (f0->bandLift (k, b) + f0->bandLift (k, kBandMid));
                    half = half && std::fabs (fh->bandLift (k, b) - want) < 1e-9;
                }
            }
            mid0.push_back (f0->bandGainDb (0, kBandMid));
            high0.push_back (f0->bandGainDb (0, kBandHigh));
            mid1.push_back (f1->bandGainDb (0, kBandMid));
            high1.push_back (f1->bandGainDb (0, kBandHigh));
        }
        const double c0 = correlation (mid0, high0), c1 = correlation (mid1, high1);
        std::printf ("    seed %3d: Mid / High gain correlation %.2f at Link 0, %.2f at 100 %%\n", seed, c0, c1);
        CHECK (together, "seed %d: Link 100 %%: the moving bands' lifts are the same", seed);
        CHECK (half, "seed %d: Link 50 %%: half way to the Mid band's", seed);
        CHECK (c1 > 0.999, "seed %d: Link 100 %%: Mid and High move together (%.3f)", seed, c1);
        CHECK (c0 < 0.9, "seed %d: Link 0: they move on their own (%.3f)", seed, c0);
        CHECK (f1->bandGainDb (0, kBandLow) == 0.0, "seed %d: the Low band is not linked (locked)", seed);
    }
}

TEST (liquid_never_touches_low)
{
    // the Low band alone (the others off): Liquid on or off renders bit for bit the same; with every band on a
    // sine well under Low X keeps its level, and the output stays mono
    const auto x = sine (40.0, 0.4, 2.0);
    auto render = [&] (double liquid, bool soloLow, std::vector<float>* right) {
        auto e = engine ();
        plain (*e);
        e->setParam (kSeed, 70); // (Low X 153 Hz: near Liquid's lowest)
        e->setParam (kBandCount, kBands4);
        e->setParam (kPasses, kPasses2);
        e->setParam (kMovement, 1.0);
        e->setParam (kLink, 1.0);
        e->setParam (kLiquid, liquid);
        e->setParam (kLiquidRes, 1.0);
        e->setParam (kLiquidLow, kLiquidLowMin);
        if (soloLow)
            solo (*e, kBandLow);
        e->reset ();
        return run (*e, x, right);
    };
    CHECK (render (0.0, true, nullptr) == render (1.0, true, nullptr), "the Low band alone: Liquid changes nothing (bit for bit)");
    std::vector<float> r;
    const auto off = render (0.0, false, nullptr), on = render (1.0, false, &r);
    const size_t a = (size_t)(0.5 * kSr), b = x.size ();
    const double change = db (toneAt (on, 40.0, a, b) / toneAt (off, 40.0, a, b));
    std::printf ("    a 40 Hz sine with every band on: %.4f dB with Liquid 100 %%\n", change);
    CHECK (std::fabs (change) < 0.05, "a sine under Low X keeps its level (%.4f dB)", change);
    CHECK (on == r, "mono in, mono out");
}

TEST (liquid_stays_in_range)
{
    // the resonance's F1 stays within Liquid Low .. Liquid High (either way round), with Link and Seed Blend; it
    // covers most of a wide range, glides (under a third of an octave from one tick to the next at Rate 1 Hz)
    // and F2 sits above it
    struct Range
    {
        double lo, hi;
        bool wide;
    };
    for (const Range& rg : {Range {250.0, 1600.0, true}, Range {150.0, 4000.0, true}, Range {800.0, 600.0, false}, Range {400.0, 700.0, false}})
        for (double link : {0.0, 1.0})
            for (double blend : {0.0, 0.5})
            {
                auto e = engine ();
                e->setParam (kMovement, 1.0);
                e->setParam (kRate, 1.0);
                e->setParam (kLiquid, 1.0);
                e->setParam (kLiquidLow, rg.lo);
                e->setParam (kLiquidHigh, rg.hi);
                e->setParam (kLink, link);
                e->setParam (kSeedBlend, blend);
                e->setParam (kSeed, 9);
                e->reset ();
                const double lo = std::min (rg.lo, rg.hi), hi = std::max (rg.lo, rg.hi);
                double fmin = 1e9, fmax = 0.0, step = 0.0, prev = std::log2 (e->liquidHz ());
                bool inside = true, above = true;
                ticks (*e, 3000 * 20, [&] (int) { // (20 s)
                    const double f = e->liquidHz ();
                    inside = inside && f >= lo * (1.0 - 1e-9) && f <= hi * (1.0 + 1e-9);
                    above = above && e->liquidF2Hz () > f;
                    fmin = std::min (fmin, f);
                    fmax = std::max (fmax, f);
                    step = std::max (step, std::fabs (std::log2 (f) - prev));
                    prev = std::log2 (f);
                });
                const double covered = std::log2 (fmax / fmin) / std::log2 (hi / lo);
                CHECK (inside, "%.0f .. %.0f Hz (Link %.0f %%, Blend %.0f %%): F1 within the range (%.0f .. %.0f)", rg.lo, rg.hi,
                       link * 100, blend * 100, fmin, fmax);
                CHECK (above, "F2 above F1");
                CHECK (step < 1.0 / 3.0, "glides: at most %.3f octaves a tick", step);
                if (rg.wide)
                    CHECK (covered > 0.6, "%.0f .. %.0f Hz: covers most of the range (%.0f %%)", lo, hi, covered * 100);
            }
    // the same Seed: the same path (Density x8)
    auto a = engine (), b = engine ();
    for (Engine* e : {a.get (), b.get ()})
    {
        e->setParam (kLiquid, 1.0);
        e->setParam (kDensity, 8.0);
        e->reset ();
    }
    bool same = true;
    const std::vector<float> z (512, 0.0f);
    std::vector<float> l (512), r (512);
    for (int i = 0; i < 200; ++i)
    {
        a->process (z.data (), z.data (), l.data (), r.data (), 512);
        b->process (z.data (), z.data (), l.data (), r.data (), 512);
        same = same && a->liquidHz () == b->liquidHz ();
    }
    CHECK (same, "the same Seed: the same path");
}

TEST (liquid_clean)
{
    // Liquid at its most (the highest resonance, the widest range) with every other extreme and the hottest
    // input: no NaNs or denormals, bounded. Switching it on and off while a tone plays does not click.
    auto e = engine ();
    e->setParam (kBandCount, kBands4);
    e->setParam (kPasses, kPasses2);
    e->setParam (kShiftOn, 1.0);
    e->setParam (kShift, 40.0);
    e->setParam (kMovement, 1.0);
    e->setParam (kDepth, 48.0);
    e->setParam (kRate, 2.0);
    e->setParam (kDrive, 1.0);
    e->setParam (kGrit, 1.0);
    e->setParam (kGlue, 1.0);
    for (uint32_t id : kLevelIds)
        e->setParam (id, 12.0);
    extreme (*e);
    e->setParam (kLink, 1.0);
    e->setParam (kLiquid, 1.0);
    e->setParam (kLiquidRes, 1.0);
    e->setParam (kLiquidLow, kLiquidLowMin);
    e->setParam (kLiquidHigh, kLiquidHighMax);
    e->reset ();
    std::vector<float> in ((size_t)(4.0 * kSr), 0.0f);
    in[100] = 1.0f;
    const auto loud = noise (1.0, 1.0, 5);
    std::copy (loud.begin (), loud.end (), in.begin () + 1000);
    std::vector<float> r;
    const auto l = run (*e, in, &r);
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
    // on and off every 0.3 s on a steady tone (no movement): no step larger than the tone's own
    const auto x = sine (700.0, 0.25, 3.0);
    auto toggled = [&] (bool toggle, double liquid) {
        auto f = engine ();
        plain (*f);
        f->setParam (kLiquid, liquid);
        f->reset ();
        return run (*f, x, nullptr, 256, [&] (size_t at) {
            if (toggle)
                f->setParam (kLiquid, (at / (size_t)(0.3 * kSr)) % 2 ? 0.0 : 1.0);
        });
    };
    const double still = std::max (maxStep (toggled (false, 0.0)), maxStep (toggled (false, 1.0)));
    const double moving = maxStep (toggled (true, 0.0));
    std::printf ("    the largest step: %.4f switching Liquid, %.4f still\n", moving, still);
    CHECK (moving < 1.3 * still, "switching Liquid on and off does not click (%.4f against %.4f)", moving, still);
}

TEST (liquid_preset_statistics)
{
    // Moist/Liquid on a detuned bass (two saws at 43.2 and 43.6 Hz, mono), measured as the reference it was
    // tuned to was (a low detuned bass whose sub stays steady while its mids and highs open and close together
    // by 40 dB or more under a resonance wandering from 250 Hz to over 1.2 kHz): the sub steady, the upper bands
    // swinging far and together, the resonance wandering, the low end mono. Without Liquid the peak stays low.
    const auto x = detunedBass (20.0);
    const size_t from = (size_t)(0.5 * kSr);
    struct Stats
    {
        double subStd, upperRange, corr, peakMin, peakMax, peakP90, sideDb;
    };
    auto measure = [&] (double liquidOverride) {
        auto e = engine ();
        CHECK (loadPreset (*e, "Moist/Liquid.txt"), "the preset loads");
        if (liquidOverride >= 0.0)
            e->setParam (kLiquid, liquidOverride);
        e->reset ();
        std::vector<float> r;
        const auto l = run (*e, x, &r, 512);
        std::vector<float> mono (l.size ()), side (l.size ());
        for (size_t i = 0; i < l.size (); ++i)
        {
            mono[i] = 0.5f * (l[i] + r[i]);
            side[i] = 0.5f * (l[i] - r[i]);
        }
        Stats s {};
        s.subStd = stdDev (bandEnvelope (mono, 30.0, 70.0, from));
        const auto upper = bandEnvelope (mono, 400.0, 6000.0, from);
        s.upperRange = percentile (upper, 95) - percentile (upper, 5);
        s.corr = correlation (bandEnvelope (mono, 400.0, 1500.0, from), bandEnvelope (mono, 1500.0, 6000.0, from));
        const auto peaks = resonancePeaks (mono, from);
        s.peakMin = *std::min_element (peaks.begin (), peaks.end ());
        s.peakMax = *std::max_element (peaks.begin (), peaks.end ());
        s.peakP90 = percentile (peaks, 90);
        s.sideDb = db (rms (side, from, side.size ())) - db (rms (mono, from, mono.size ()));
        std::printf ("    %s: sub (30 .. 70 Hz) std %.2f dB, 400 .. 6000 Hz range (p5 .. p95) %.1f dB, 400 .. 1500 / 1500 .. 6000 "
                     "correlation %.2f, resonance %.0f .. %.0f Hz (p90 %.0f), side %.0f dB\n",
                     liquidOverride >= 0.0 ? "Liquid 0" : "Moist/Liquid", s.subStd, s.upperRange, s.corr, s.peakMin, s.peakMax,
                     s.peakP90, s.sideDb);
        return s;
    };
    const Stats s = measure (-1.0);
    CHECK (s.subStd <= 4.0, "the sub stays steady (std %.2f dB)", s.subStd);
    CHECK (s.upperRange >= 30.0, "the upper bands swing far (%.1f dB)", s.upperRange);
    CHECK (s.corr >= 0.7, "the upper bands move together (%.2f)", s.corr);
    CHECK (s.peakMin <= 320.0 && s.peakMax >= 1000.0, "the resonance wanders (%.0f .. %.0f Hz)", s.peakMin, s.peakMax);
    CHECK (s.sideDb < -60.0, "mono in, mono out (%.0f dB)", s.sideDb);
    const Stats dry = measure (0.0);
    CHECK (dry.peakMax < 700.0, "without Liquid the peak stays low (%.0f Hz)", dry.peakMax);
}

// ---- the SWEEP stage (0.24) -------------------------------------------------------------------------------
namespace {
// the SWEEP stage alone (no split, no end saturator): the defaults, then `set` (plain values)
struct SweepRig
{
    ParamArray p = defaultParams ();
    Sweep s;
    double sr;
    explicit SweepRig (double rate, const std::function<void (ParamArray&)>& set = {}) : sr (rate)
    {
        if (set)
            set (p);
        s.prepare (sr);
        s.reset (p.data ());
    }
    // n samples of x (both channels the same; x shorter: silence after it), in ticks; `each` after every tick
    std::vector<double> run (const std::vector<double>& x, size_t n, const std::function<void (size_t)>& each = {})
    {
        std::vector<double> y (n);
        double l[Sweep::kTick], r[Sweep::kTick];
        for (size_t a = 0; a < n; a += Sweep::kTick)
        {
            const int m = (int)std::min ((size_t)Sweep::kTick, n - a);
            for (int i = 0; i < m; ++i)
                l[i] = r[i] = a + i < x.size () ? x[a + i] : 0.0;
            s.tick (p.data (), l, r, m);
            for (int i = 0; i < m; ++i)
                y[a + i] = l[i];
            if (each)
                each (a + m);
        }
        return y;
    }
};

// the magnitude of an impulse response h at hz (sample rate sr), by a rotating phasor
double irMagnitude (const std::vector<double>& h, double hz, double sr)
{
    const std::complex<double> step = std::polar (1.0, -2.0 * kPi * hz / sr);
    std::complex<double> w = 1.0, sum = 0.0;
    for (double v : h)
    {
        sum += v * w;
        w *= step;
    }
    return std::abs (sum);
}
// RBJ's cookbook: the peaking EQ and the high shelf (with Q) at f0, their magnitude at hz
double rbjMagnitude (bool shelf, double f0, double gainDb, double q, double hz, double sr)
{
    const double a = std::pow (10.0, gainDb / 40.0), w0 = 2.0 * kPi * f0 / sr, cs = std::cos (w0), al = std::sin (w0) / (2.0 * q);
    double b[3], d[3];
    if (!shelf)
    {
        b[0] = 1.0 + al * a, b[1] = -2.0 * cs, b[2] = 1.0 - al * a;
        d[0] = 1.0 + al / a, d[1] = -2.0 * cs, d[2] = 1.0 - al / a;
    }
    else
    {
        const double sa = 2.0 * std::sqrt (a) * al;
        b[0] = a * ((a + 1.0) + (a - 1.0) * cs + sa), b[1] = -2.0 * a * ((a - 1.0) + (a + 1.0) * cs), b[2] = a * ((a + 1.0) + (a - 1.0) * cs - sa);
        d[0] = (a + 1.0) - (a - 1.0) * cs + sa, d[1] = 2.0 * ((a - 1.0) - (a + 1.0) * cs), d[2] = (a + 1.0) - (a - 1.0) * cs - sa;
    }
    const std::complex<double> z = std::polar (1.0, -2.0 * kPi * hz / sr);
    return std::abs ((b[0] + b[1] * z + b[2] * z * z) / (d[0] + d[1] * z + d[2] * z * z));
}
// the prototype's centre (Hz) for a bell at t seconds: Low (High / Low)^(0.5 - 0.5 cos (2 pi Rate t + phase))
double protoCentre (double rate, double lo, double hi, double t, double phase = 0.0)
{
    return lo * std::pow (hi / lo, 0.5 - 0.5 * std::cos (2.0 * kPi * rate * t + phase));
}
} // namespace

TEST (sweep_default_is_the_recipe)
{
    // a new instance: the SWEEP stage on with the Ocean recipe (eight bells, Curve Soft at 14 dB, Tone 7 kHz, no
    // High Shelf, Sub Boost at 70 Hz and 70 %), the rest neutral
    const auto d = defaultParams ();
    CHECK (d[kSweep] == 1.0 && d[kShelf] == 0.0 && d[kSweepDrive] == 14.0 && std::lround (d[kSweepCurve]) == kCurveSoft,
           "Sweep on, High Shelf off, Drive 14 dB on the Soft curve");
    CHECK (d[kToneOn] == 1.0 && d[kTone] == 7000.0, "Tone on at 7 kHz");
    CHECK (d[kCleanSub] == 0.0 && d[kSubBoost] == 1.0 && d[kSubFreq] == 70.0 && d[kSubLevel] == 0.7,
           "Clean Sub off; Sub Boost on at 70 Hz, 70 %%");
    // the eight bells, exactly the recipe (rate, low, high, gain, Q, phase in radians as degrees)
    const double want[kNumBells][6] = {{0.70, 20, 120, 23.4, 0.5, 0.0}, {0.77, 30, 300, -23.4, 0.5, 1.1}, {0.53, 80, 600, 11.7, 0.6, 2.3},
                                       {0.91, 150, 1200, -11.7, 0.6, 0.7}, {0.41, 250, 2000, 7.8, 0.7, 3.9}, {1.13, 400, 3000, -10.4, 0.7, 5.1},
                                       {0.63, 60, 450, -7.8, 0.5, 4.4}, {0.84, 200, 1600, 9.1, 0.6, 2.9}};
    for (int b = 0; b < kNumBells; ++b)
    {
        const double* w = want[b];
        CHECK (d[bellOnId (b)] == 1.0 && d[bellId (b, kBellRate)] == w[0] && d[bellId (b, kBellLow)] == w[1] && d[bellId (b, kBellHigh)] == w[2] &&
                   d[bellId (b, kBellGain)] == w[3] && d[bellId (b, kBellWidth)] == w[4] && d[bellId (b, kBellSync)] == 0.0 &&
                   std::fabs (d[bellId (b, kBellPhase)] * kPi / 180.0 - w[5]) < 1e-12,
               "bell %c: on, %.2f Hz, %.0f .. %.0f Hz, %+.1f dB, Q %.1f, phase %.1f rad", 'A' + b, w[0], w[1], w[2], w[3], w[4], w[5]);
    }
    CHECK (std::fabs (d[kBPhase] - 63.0254) < 1e-4, "B Phase: 1.1 rad is %.4f degrees", d[kBPhase]);
    CHECK (d[kDrive] == 0.0 && d[kMovement] == 0.0 && d[kGlue] == 0.0 && d[kGrit] == 0.0 && d[kLink] == 0.0 && d[kLiquid] == 0.0 &&
               d[kShiftOn] == 0.0 && d[kMix] == 1.0 && d[kOutput] == 0.0,
           "the rest neutral: Drive, Movement, Glue, Grit, Link, Liquid 0, the shifter off, Mix 100 %%");
    CHECK (d[kTailBase + pk::kTailOn] == 1.0, "the end saturator on (as in every plug-in since 0.25)");
    // the bells' centres follow the prototype's formula exactly, tick by tick, over 20 s (the engine, from reset)
    for (double sr : {44100.0, 48000.0, 96000.0})
    {
        auto e = fresh (sr);
        double worst = 0.0;
        size_t done = 0;
        const std::vector<float> z (Sweep::kTick, 0.0f);
        std::vector<float> l (Sweep::kTick), r (Sweep::kTick);
        for (int t = 0; t < (int)(20.0 * sr) / Sweep::kTick; ++t)
        {
            e->process (z.data (), z.data (), l.data (), r.data (), Sweep::kTick);
            done += Sweep::kTick;
            const double ts = (double)done / sr;
            for (int b = 0; b < kNumBells; ++b)
                worst = std::max (worst, std::fabs (e->sweepStage ().bellHz (b) / protoCentre (want[b][0], want[b][1], want[b][2], ts, want[b][5]) - 1.0));
        }
        CHECK (worst < 1e-9, "%.0f Hz: the eight bells' centres are the prototype's (worst %.1e relative)", sr, worst);
    }
}

TEST (sweep_old_states_keep_their_sound)
{
    // a 0.24 / 0.25 state reads the 0.26 parameters at values that leave its sound (defaultNormalizedForVersion):
    // bells C .. H, Tone, Clean Sub and Sub Boost off, Curve Hard. Those paths are then not run at all: whatever
    // their settings, the render is the same, bit for bit. (Renders like these were compared bit for bit with
    // 0.25.0's, for every factory preset and the 0.25 default state, when they were added.)
    const auto x = reese (2.0);
    for (int version : {3, 4})
    {
        CHECK (defaultNormalizedForVersion (kSweepCurve, version) == toNormalized (kSweepCurve, kCurveHard) &&
                   defaultNormalizedForVersion (kToneOn, version) == 0.0 && defaultNormalizedForVersion (kCleanSub, version) == 0.0 &&
                   defaultNormalizedForVersion (kSubBoost, version) == 0.0 && defaultNormalizedForVersion (kShelf, version) == 1.0 &&
                   toPlain (kSweepDrive, defaultNormalizedForVersion (kSweepDrive, version)) == 18.0,
               "version %d: Curve Hard, Tone, Clean Sub and Sub Boost off; High Shelf on, Sweep Drive 18 dB", version);
        int bellsOff = 0;
        for (int b = 2; b < kNumBells; ++b)
            bellsOff += defaultNormalizedForVersion (bellOnId (b), version) == 0.0;
        CHECK (bellsOff == 6 && defaultNormalizedForVersion (kAOn, version) == 1.0 && defaultNormalizedForVersion (kBOn, version) == 1.0,
               "version %d: bells A and B on, C .. H off", version);
    }
    auto render = [&] (bool wild) {
        auto e = fresh ();
        for (uint32_t id = 0; id < kNumParams; ++id)
            e->setParam (id, toPlain (id, defaultNormalizedForVersion (id, 4)));
        if (wild)
            for (int b = 2; b < kNumBells; ++b)
            {
                e->setParam (bellId (b, kBellGain), 24.0);
                e->setParam (bellId (b, kBellRate), 7.0);
                e->setParam (bellId (b, kBellWidth), 9.0);
            }
        if (wild)
        {
            e->setParam (kTone, 1000.0);
            e->setParam (kSplitFreq, 250.0);
            e->setParam (kSplitLevel, 12.0);
            e->setParam (kSplitDrive, 18.0);
            e->setParam (kSubFreq, 200.0);
            e->setParam (kSubLevel, 1.0);
        }
        e->reset ();
        std::vector<float> r;
        auto l = run (*e, x, &r);
        l.insert (l.end (), r.begin (), r.end ());
        return l;
    };
    const auto calm = render (false);
    CHECK (calm == render (true), "0.25's settings: the new paths' settings change nothing (bit for bit)");
    // and the old Sweep presets (explicit since 0.26) over the new defaults render as over 0.25's
    namespace fs = std::filesystem;
    int checked = 0;
    for (const char* name : {"Sweep/Gentle.txt", "Sweep/Heavy.txt", "Sweep/High Shelf 5k.txt", "Sweep/Classic Sweep.txt",
                             "Sweep/Sweep + Bands.txt", "Sweep/Wide Bump.txt"})
    {
        std::ifstream in (fs::path (MOISTR_PRESETS_DIR) / name);
        std::stringstream ss;
        ss << in.rdbuf ();
        pk::presets::FactoryPreset fp;
        std::string err;
        CHECK (pk::presets::parseFactoryPreset (ss.str (), name, paramTable (), fp, err), "%s parses: %s", name, err.c_str ());
        auto renderOver = [&] (bool old) {
            auto e = fresh ();
            for (uint32_t id = 0; id < kNumParams; ++id)
                e->setParam (id, toPlain (id, old ? defaultNormalized025 (id) : defaultNormalized (id)));
            for (auto& [id, n] : fp.values)
                e->setParam (id, toPlain (id, n));
            e->reset ();
            std::vector<float> r;
            auto l = run (*e, x, &r);
            l.insert (l.end (), r.begin (), r.end ());
            return l;
        };
        CHECK (renderOver (false) == renderOver (true), "%s: as in 0.25, bit for bit", name);
        ++checked;
    }
    CHECK (checked == 6, "%d old sweep presets", checked);
}

TEST (sweep_ocean_preset_is_init)
{
    // Sweep/Ocean is Init's sound (a new instance's up to 0.30): over Init it sets nothing that differs
    std::ifstream in (std::filesystem::path (MOISTR_PRESETS_DIR) / "Sweep/Ocean.txt");
    std::stringstream ss;
    ss << in.rdbuf ();
    pk::presets::FactoryPreset fp;
    std::string err;
    CHECK (pk::presets::parseFactoryPreset (ss.str (), "Sweep/Ocean.txt", paramTable (), fp, err), "parses: %s", err.c_str ());
    int differ = 0;
    for (auto& [id, n] : fp.values)
        differ += std::fabs (toPlain (id, n) - defaultParams ()[id]) > 1e-9 * std::max (1.0, std::fabs (defaultParams ()[id]));
    CHECK (differ == 0 && fp.values.size () >= 10, "%d of %zu values differ from Init", differ, fp.values.size ());
}

TEST (sweep_keeps_the_stereo_image)
{
    // the stage (and the engine at its defaults) treats both channels alike: an L-only input leaves R silent, and
    // swapping L and R in swaps them out, bit for bit; a mono input stays mono
    const auto a = reese (3.0);
    const auto b = noise (0.2, 3.0, 11);
    for (int tailOn = 0; tailOn < 2; ++tailOn)
    {
        auto render = [&] (const std::vector<float>& l, const std::vector<float>& r, std::vector<float>& outR) {
            auto e = fresh ();
            if (!tailOn)
                tailNeutral (*e);
            std::vector<float> outL (l.size ());
            outR.assign (l.size (), 0.0f);
            for (size_t i = 0; i < l.size (); i += 256)
            {
                const int m = (int)std::min ((size_t)256, l.size () - i);
                e->process (l.data () + i, r.data () + i, outL.data () + i, outR.data () + i, m);
            }
            return outL;
        };
        const std::vector<float> silent (a.size (), 0.0f);
        std::vector<float> r1, r2, r3, r4;
        const auto l1 = render (a, silent, r1);
        CHECK (peak (r1, 0, r1.size ()) == 0.0 && rms (l1, 0, l1.size ()) > 0.01, "%s: L only in, R out silent (peak %.3g)",
               tailOn ? "the end saturator on" : "the engine", peak (r1, 0, r1.size ()));
        const auto l2 = render (a, b, r2);
        const auto l3 = render (b, a, r3);
        CHECK (l2 == r3 && r2 == l3, "%s: swapping L and R in swaps them out, bit for bit", tailOn ? "the end saturator on" : "the engine");
        const auto l4 = render (a, a, r4);
        CHECK (l4 == r4, "%s: mono in, mono out", tailOn ? "the end saturator on" : "the engine");
    }
}

TEST (clean_sub_split_is_flat)
{
    // Clean Sub with the saturator linear (a tiny impulse, Drive 0 dB) and Split Level 0 dB: the split's two sides
    // add up to an all-pass, so the response is the one without it (the anti-aliasing's average aside, as there)
    for (double sr : {44100.0, 48000.0, 96000.0})
        for (double fc : {40.0, 100.0, 250.0})
        {
            auto response = [&] (bool split) {
                SweepRig rig (sr, [&] (ParamArray& p) {
                    p[kSweepDrive] = 0.0;
                    p[kSweepCurve] = kCurveHard;
                    p[kToneOn] = 0.0;
                    p[kSubBoost] = 0.0;
                    p[kCleanSub] = split ? 1.0 : 0.0;
                    p[kSplitFreq] = fc;
                    p[kSplitLevel] = 0.0;
                });
                std::vector<double> x (1, 1e-6);
                return rig.run (x, (size_t)(2.0 * sr));
            };
            const auto with = response (true), without = response (false);
            double worst = 0.0;
            for (double hz : {20.0, 30.0, 50.0, 70.0, 100.0, 150.0, 250.0, 400.0, 1000.0, 5000.0})
                worst = std::max (worst, std::fabs (db (irMagnitude (with, hz, sr) / irMagnitude (without, hz, sr))));
            CHECK (worst < 0.1 && finite (std::vector<float> (with.begin (), with.end ())), "%.0f Hz, split at %.0f Hz: flat within 0.1 dB (%.3f)",
                   sr, fc, worst);
        }
}

TEST (sub_boost_adds_the_lows)
{
    // Sub Boost at Sub Level 0 adds nothing (bit for bit as off); up, it lifts the lows (30 .. 70 Hz) against the
    // highs and only them; at 100 %% its lows come in at about the saturated signal's RMS
    const auto x = detunedBass (6.0);
    auto render = [&] (bool on, double level, double* unit = nullptr) {
        auto e = fresh ();
        tailNeutral (*e);
        e->setParam (kSubBoost, on ? 1.0 : 0.0);
        e->setParam (kSubLevel, level);
        e->reset ();
        auto y = run (*e, x);
        if (unit)
            *unit = e->sweepStage ().boostUnit ();
        return y;
    };
    const auto off = render (false, 0.7);
    CHECK (render (true, 0.0) == off, "Sub Level 0: as off, bit for bit");
    const auto on = render (true, 0.7), full = render (true, 1.0);
    const size_t from = (size_t)(1.0 * kSr);
    auto balance = [&] (const std::vector<float>& y) {
        double lo = 0, hi = 0;
        for (double hz : {40.0, 55.0, 65.0})
            lo += toneAt (y, hz, from, y.size ());
        for (double hz : {1000.0, 2000.0})
            hi += toneAt (y, hz, from, y.size ());
        return db (lo / hi);
    };
    std::printf ("    lows against highs: off %.1f dB, 70 %% %.1f dB, 100 %% %.1f dB\n", balance (off), balance (on), balance (full));
    CHECK (balance (on) > balance (off) + 1.0 && balance (full) > balance (on), "the lows come up (%.1f, %.1f, %.1f dB)", balance (off),
           balance (on), balance (full));
    double unit = 0.0;
    render (true, 1.0, &unit);
    CHECK (unit > 0.1 && unit < kSubMaxGain, "its RMS match is in range (x%.2f)", unit);
}

TEST (sweep_filters_match_rbj)
{
    // each filter alone, held still (Low = High), against RBJ's cookbook at 44.1 .. 192 kHz: the bells (peaking
    // EQ) and the High Shelf (high shelf with Q, Min = Max). A tiny impulse keeps the saturator in its linear
    // region: its make-up x drive is a plain gain there and its anti-aliasing the mean of two samples
    struct Case
    {
        bool shelf;
        double f0, gain, q;
    };
    const Case cases[] = {{false, 20.0, 18.0, 0.71},  {false, 60.0, 18.0, 0.71},  {false, 120.0, 18.0, 0.71}, {false, 30.0, -18.0, 0.71},
                          {false, 300.0, -18.0, 0.71}, {false, 1000.0, 24.0, 10.0}, {true, 100.0, -18.0, 18.0},  {true, 1000.0, 6.0, 18.0},
                          {true, 300.0, -18.0, 0.71},  {true, 5000.0, -12.0, 24.0}, {true, 50.0, -24.0, 24.0}};
    for (double sr : {44100.0, 48000.0, 96000.0, 192000.0})
    {
        double worst = 0.0;
        for (const Case& c : cases)
        {
            SweepRig rig (sr, [&] (ParamArray& p) {
                p[kSweepDrive] = 0.0;
                p[kSweepCurve] = kCurveHard;
                p[kToneOn] = 0.0;
                p[kSubBoost] = 0.0;
                for (int b = 1; b < kNumBells; ++b)
                    p[bellOnId (b)] = 0.0; // (off: gliding to 0 dB from 0 dB, never run)
                for (int b = 1; b < kNumBells; ++b)
                    p[bellId (b, kBellGain)] = 0.0;
                p[kShelf] = c.shelf ? 1.0 : 0.0;
                if (c.shelf)
                {
                    p[kAGain] = 0.0;
                    p[kShelfLow] = p[kShelfHigh] = c.f0;
                    p[kShelfMin] = p[kShelfMax] = c.gain;
                    p[kShelfQ] = c.q;
                    p[kShelfTilt] = 0.0;
                }
                else
                {
                    p[kALow] = p[kAHigh] = c.f0;
                    p[kAGain] = c.gain;
                    p[kAWidth] = c.q;
                }
            });
            std::vector<double> x (1, 1e-6);
            const auto h = rig.run (x, (size_t)(3.0 * sr));
            const double scale = 1e-6 * rig.s.compensationNow (); // (drive 0 dB: g = 1)
            for (double k : {0.25, 0.5, 0.8, 1.0, 1.25, 2.0, 4.0})
            {
                const double hz = c.f0 * k;
                if (hz < 10.0 || hz > 0.45 * sr)
                    continue;
                const double adaa = std::fabs (std::cos (kPi * hz / sr));
                const double got = db (irMagnitude (h, hz, sr) / scale / adaa), want = db (rbjMagnitude (c.shelf, c.f0, c.gain, c.q, hz, sr));
                worst = std::max (worst, std::fabs (got - want));
            }
            CHECK (finite (std::vector<float> (h.begin (), h.end ())), "%.0f Hz: finite", sr);
        }
        CHECK (worst < 0.05, "%.0f Hz: the bells and the shelf within 0.05 dB of RBJ (worst %.4f dB)", sr, worst);
    }
}

TEST (sweep_off_changes_nothing)
{
    // with Sweep off the stage is not run: its settings change nothing, bit for bit (0.23's engine; renders like
    // these were compared bit for bit with 0.23.0's when the stage was added)
    const auto x = reese (2.0);
    auto render = [&] (bool wild) {
        auto e = engine ();
        e->setParam (kMovement, 0.9);
        e->setParam (kSeed, 17);
        if (wild)
        {
            e->setParam (kSweepDrive, 36.0);
            e->setParam (kAGain, -24.0);
            e->setParam (kBGain, 24.0);
            e->setParam (kShelfQ, 24.0);
            e->setParam (kShelfWander, 1.0);
            e->setParam (kASync, 1.0);
        }
        e->reset ();
        std::vector<float> r;
        auto l = run (*e, x, &r);
        l.insert (l.end (), r.begin (), r.end ());
        return l;
    };
    CHECK (render (false) == render (true), "Sweep off: its settings change nothing (bit for bit)");
    auto e = engine ();
    run (*e, x);
    CHECK (e->sweepStage ().amount () == 0.0, "Sweep off: not faded in");
}

TEST (sweep_old_presets_unchanged)
{
    // every factory preset from before the stage sounds as it did: applied over the new defaults (as the menu
    // does: Init plus its lines) it renders bit for bit as over the defaults it was made with (Sweep off)
    namespace fs = std::filesystem;
    const auto x = reese (1.5);
    int checked = 0, sweeps = 0;
    for (const auto& entry : fs::recursive_directory_iterator (MOISTR_PRESETS_DIR))
    {
        if (entry.path ().extension () != ".txt")
            continue;
        const std::string rel = fs::relative (entry.path (), MOISTR_PRESETS_DIR).generic_string ();
        std::ifstream in (entry.path ());
        std::stringstream ss;
        ss << in.rdbuf ();
        pk::presets::FactoryPreset fp;
        std::string err;
        CHECK (pk::presets::parseFactoryPreset (ss.str (), rel, paramTable (), fp, err), "%s parses: %s", rel.c_str (), err.c_str ());
        if (rel.rfind ("Sweep/", 0) == 0 || rel.rfind ("Gestures/", 0) == 0 || rel.rfind ("Neuro/", 0) == 0)
        {
            // the new ones (0.24's sweep presets, 0.27's gesture presets, 0.30's LAB presets): the stage on
            auto e = fresh ();
            for (auto& [id, n] : fp.values)
                e->setParam (id, toPlain (id, n));
            CHECK (e->param (kSweep) >= 0.5, "%s: Sweep on", rel.c_str ());
            ++sweeps;
            continue;
        }
        auto render = [&] (bool legacyBase) {
            auto e = fresh ();
            for (uint32_t id = 0; id < kNumParams; ++id)
                e->setParam (id, toPlain (id, legacyBase ? legacyDefaultNormalized (id) : defaultNormalized (id)));
            for (auto& [id, n] : fp.values)
                e->setParam (id, toPlain (id, n));
            e->reset ();
            std::vector<float> r;
            auto l = run (*e, x, &r);
            l.insert (l.end (), r.begin (), r.end ());
            return l;
        };
        CHECK (render (false) == render (true), "%s: as before 0.24, bit for bit", rel.c_str ());
        ++checked;
    }
    CHECK (checked >= 14 && sweeps >= 5, "%d presets from before, %d sweep presets", checked, sweeps);
}

TEST (sweep_stable_everywhere)
{
    // the defaults and the extremes at 44.1 .. 192 kHz: finite, bounded, no denormals once it falls silent
    for (double sr : {44100.0, 48000.0, 96000.0, 192000.0})
        for (int extreme = 0; extreme < 2; ++extreme)
        {
            auto e = fresh (sr);
            tailNeutral (*e);
            if (extreme)
            {
                for (uint32_t id : {kARate, kBRate, kShelfRate})
                    e->setParam (id, kSweepRateMax);
                e->setParam (kALow, kBellFreqMin);
                e->setParam (kAHigh, kBellFreqMax);
                e->setParam (kAGain, 24.0);
                e->setParam (kAWidth, kBellQMax);
                e->setParam (kBGain, -24.0);
                e->setParam (kBWidth, kBellQMin);
                e->setParam (kShelfLow, 50.0);
                e->setParam (kShelfHigh, 5000.0);
                e->setParam (kShelfMin, -24.0);
                e->setParam (kShelfMax, 12.0);
                e->setParam (kShelfQ, kShelfQMax);
                e->setParam (kShelfWander, 1.0);
                e->setParam (kShelfTilt, 0.0);
                e->setParam (kSweepDrive, kSweepDriveMax);
                e->setParam (kShelf, 1.0);
                for (int b = 2; b < kNumBells; ++b)
                {
                    e->setParam (bellId (b, kBellRate), kSweepRateMax);
                    e->setParam (bellId (b, kBellLow), kBellFreqMin);
                    e->setParam (bellId (b, kBellHigh), kBellFreqMaxWide);
                    e->setParam (bellId (b, kBellGain), b % 2 ? -24.0 : 24.0);
                    e->setParam (bellId (b, kBellWidth), b % 2 ? kBellQMin : kBellQMax);
                }
                e->setParam (kCleanSub, 1.0);
                e->setParam (kSplitFreq, kSplitFreqMax);
                e->setParam (kSplitLevel, kSplitLevelMax);
                e->setParam (kSplitDrive, kSplitDriveMax);
                e->setParam (kSubLevel, 1.0);
                e->setParam (kSubFreq, kSubFreqMax);
                e->setParam (kTone, kToneMin);
                e->reset ();
            }
            const size_t n = (size_t)(4.0 * sr);
            std::vector<float> in (n, 0.0f), l (n), r (n);
            uint32_t seed = 5;
            double p1 = 0.0;
            for (size_t i = 0; i < (size_t)(3.0 * sr); ++i)
            {
                seed = seed * 1664525u + 1013904223u;
                p1 += 41.0 / sr;
                p1 -= std::floor (p1);
                in[i] = (float)(0.5 * (2.0 * p1 - 1.0) + 0.2 * ((int32_t)seed / 2147483648.0));
            }
            for (size_t a = 0; a < n; a += 512)
            {
                const int m = (int)std::min ((size_t)512, n - a);
                e->process (in.data () + a, in.data () + a, l.data () + a, r.data () + a, m);
            }
            int denormals = 0;
            const float tiny = std::numeric_limits<float>::min ();
            for (size_t i = 0; i < n; ++i)
                denormals += (l[i] != 0.0f && std::fabs (l[i]) < tiny) || (r[i] != 0.0f && std::fabs (r[i]) < tiny);
            const double top = std::max (peak (l, 0, n), peak (r, 0, n));
            CHECK (finite (l) && finite (r), "%.0f Hz%s: finite", sr, extreme ? " (extremes)" : "");
            CHECK (denormals == 0, "%.0f Hz%s: no denormals (%d)", sr, extreme ? " (extremes)" : "", denormals);
            // (the extremes: Clean Sub at +12 dB and Sub Boost at 100 % pass lows boosted by several +24 dB bells
            // around the saturator, so they are bounded only by the bells' gain)
            CHECK (top < (extreme ? 30.0 : 4.0), "%.0f Hz%s: bounded (%.2f)", sr, extreme ? " (extremes)" : "", top);
        }
}

TEST (shelf_orbit_bounded_and_smooth)
{
    // the High Shelf's corner and gain stay in their ranges and move smoothly (bounded change per tick), the
    // same way for the same Seed; Wander 0 is a circle
    for (int seed : {1, 2, 77})
        for (double wander : {0.0, 0.5, 1.0})
            for (double tilt : {0.0, 0.65})
            {
                auto path = [&] (int s) {
                    SweepRig rig (48000.0, [&] (ParamArray& p) {
                        p[kShelf] = 1.0;
                        p[kSeed] = s;
                        p[kShelfWander] = wander;
                        p[kShelfTilt] = tilt;
                        p[kShelfHigh] = 5000.0;
                    });
                    std::vector<double> hz, gain, u, v;
                    rig.run ({}, (size_t)(60.0 * 48000.0), [&] (size_t) {
                        hz.push_back (rig.s.shelfHz ());
                        gain.push_back (rig.s.shelfDb ());
                        u.push_back (rig.s.orbitU ());
                        v.push_back (rig.s.orbitV ());
                    });
                    return std::make_tuple (hz, gain, u, v);
                };
                const auto [hz, gain, u, v] = path (seed);
                double lo = 1e9, hi = 0.0, gLo = 1e9, gHi = -1e9, stepF = 0.0, stepG = 0.0, circle = 0.0;
                for (size_t i = 0; i < hz.size (); ++i)
                {
                    lo = std::min (lo, hz[i]);
                    hi = std::max (hi, hz[i]);
                    gLo = std::min (gLo, gain[i]);
                    gHi = std::max (gHi, gain[i]);
                    if (i > 0)
                    {
                        stepF = std::max (stepF, std::fabs (std::log2 (hz[i] / hz[i - 1])));
                        stepG = std::max (stepG, std::fabs (gain[i] - gain[i - 1]));
                    }
                    circle = std::max (circle, std::fabs ((u[i] - 0.5) * (u[i] - 0.5) + (v[i] - 0.5) * (v[i] - 0.5) - 0.25));
                }
                CHECK (lo >= 100.0 * (1.0 - 1e-9) && hi <= 5000.0 * (1.0 + 1e-9) && gLo >= -18.0 - 1e-9 && gHi <= 6.0 + 1e-9,
                       "seed %d, wander %.1f, tilt %.2f: in range (%.1f .. %.1f Hz, %.2f .. %.2f dB)", seed, wander, tilt, lo, hi, gLo, gHi);
                CHECK (hi / lo > 3.0 && gHi - gLo > 6.0, "seed %d, wander %.1f: it goes round (%.1f .. %.1f Hz, %.2f .. %.2f dB)", seed,
                       wander, lo, hi, gLo, gHi);
                CHECK (stepF < 0.01 && stepG < 0.1, "seed %d, wander %.1f: smooth (at most %.4f octaves, %.4f dB a tick)", seed, wander,
                       stepF, stepG);
                if (wander == 0.0)
                    CHECK (circle < 1e-9, "wander 0: a circle (%.1e)", circle);
                const auto again = path (seed);
                CHECK (std::get<0> (again) == hz && std::get<1> (again) == gain, "seed %d: the same path again", seed);
                if (wander > 0.0)
                    CHECK (std::get<0> (path (seed + 1)) != hz, "seed %d and %d: different paths", seed, seed + 1);
            }
}

TEST (shelf_tilt_tames_high_corners)
{
    // Tilt lowers the gain ceiling as the corner rises above 1 kHz: at its default, 9 dB or more lower at 5 kHz
    // than at 300 Hz, and never above Max
    const double tilt = defaultParams ()[kShelfTilt];
    const double at300 = shelfCeilingDb (300.0, -18.0, 6.0, tilt), at5k = shelfCeilingDb (5000.0, -18.0, 6.0, tilt);
    CHECK (at300 == 6.0 && at5k <= at300 - 9.0, "the ceiling: %.2f dB at 300 Hz, %.2f dB at 5 kHz", at300, at5k);
    CHECK (shelfCeilingDb (5000.0, -18.0, 6.0, 0.0) == 6.0 && shelfCeilingDb (1000.0, -18.0, 6.0, 1.0) == 6.0,
           "Tilt 0: the same range everywhere; up to 1 kHz never lowered");
    // on the orbit (Low 300 Hz, High 5 kHz): the gain under the ceiling, high corners lower
    SweepRig rig (48000.0, [] (ParamArray& p) {
        p[kShelf] = 1.0;
        p[kShelfLow] = 300.0;
        p[kShelfHigh] = 5000.0;
        p[kShelfWander] = 0.0;
    });
    double over = -1e9, topHigh = -1e9, topLow = -1e9;
    rig.run ({}, (size_t)(30.0 * 48000.0), [&] (size_t) {
        const double hz = rig.s.shelfHz (), g = rig.s.shelfDb ();
        over = std::max (over, g - std::min (6.0, shelfCeilingDb (hz, -18.0, 6.0, tilt)));
        if (hz > 4000.0)
            topHigh = std::max (topHigh, g);
        if (hz < 1000.0)
            topLow = std::max (topLow, g);
    });
    CHECK (over <= 1e-9, "never above the ceiling (%.2e)", over);
    CHECK (topHigh <= topLow - 6.0, "above 4 kHz at most %.2f dB, below 1 kHz up to %.2f dB", topHigh, topLow);
}

TEST (sweep_follows_transport)
{
    // while the host plays the sweep follows the song position: two instances that ran differently before agree
    // from the moment it plays; free, the clock is the song's time x Rate; synced, the song position over the beats
    const std::vector<float> in (256, 0.1f);
    std::vector<float> l (256), r (256);
    auto play = [&] (int warm, bool sync) {
        auto e = fresh ();
        if (sync)
        {
            e->setParam (kASync, 1.0);
            e->setParam (kASyncRate, 3); // 1/2: 2 beats
            e->reset ();
        }
        e->setTransport (128.0, 0.0, false);
        for (int i = 0; i < warm; ++i)
            e->process (in.data (), in.data (), l.data (), r.data (), 256);
        std::vector<double> hz;
        double ppq = 16.0;
        for (int i = 0; i < 300; ++i)
        {
            e->setTransport (128.0, ppq, true);
            e->process (in.data (), in.data (), l.data (), r.data (), 256);
            if (i == 0)
            {
                const double t = 16.0 * 60.0 / 128.0 + 256.0 / kSr;
                const double th = sync ? 16.0 / 2.0 + 256.0 * 128.0 / 60.0 / 2.0 / kSr : 0.70 * t;
                const double want = 20.0 * std::pow (6.0, 0.5 - 0.5 * std::cos (2.0 * kPi * th));
                CHECK (std::fabs (e->sweepStage ().bellHz (0) / want - 1.0) < 1e-9, "%s: bell A at the song position (%.3f Hz, want %.3f)",
                       sync ? "synced" : "free", e->sweepStage ().bellHz (0), want);
            }
            for (int b = 0; b < 2; ++b)
                hz.push_back (e->sweepStage ().bellHz (b));
            hz.push_back (e->sweepStage ().shelfHz ());
            ppq += 256.0 * 128.0 / 60.0 / kSr;
        }
        return hz;
    };
    for (bool sync : {false, true})
        CHECK (play (0, sync) == play (123, sync), "%s: the same path from the same song position", sync ? "synced" : "free");
}

TEST (sweep_default_level)
{
    // a new instance on a detuned bass comes out about as loud as it went in (the saturator's make-up)
    for (int which = 0; which < 2; ++which)
    {
        const auto x = which == 0 ? detunedBass (8.0) : reese (8.0);
        auto e = fresh ();
        tailNeutral (*e);
        const auto y = run (*e, x);
        const size_t from = (size_t)(0.5 * kSr);
        const double gap = db (rms (y, from, y.size ())) - db (rms (x, from, x.size ()));
        std::printf ("    %s: out %.2f dB against in\n", which == 0 ? "detuned bass" : "reese", gap);
        CHECK (std::fabs (gap) < 3.0, "%s: within 3 dB of the input (%.2f dB)", which == 0 ? "detuned bass" : "reese", gap);
    }
    // and at every Drive, within 3 dB too
    for (double drive : {0.0, 6.0, 12.0, 24.0, 36.0})
    {
        const auto x = detunedBass (6.0);
        auto e = fresh ();
        tailNeutral (*e);
        e->setParam (kSweepDrive, drive);
        e->reset ();
        const auto y = run (*e, x);
        const double gap = db (rms (y, (size_t)(0.5 * kSr), y.size ())) - db (rms (x, (size_t)(0.5 * kSr), x.size ()));
        CHECK (std::fabs (gap) < 3.0, "Drive %.0f dB: within 3 dB of the input (%.2f dB)", drive, gap);
    }
}

TEST (sweep_switches_smoothly)
{
    // switching Sweep and High Shelf on and off fades (no step larger than the signal's own)
    const auto x = sine (55.0, 0.25, 4.0);
    auto e = fresh ();
    const auto y = run (*e, x, nullptr, 256, [&] (size_t at) {
        if (at == 256 * 100)
            e->setParam (kSweep, 0.0);
        if (at == 256 * 200)
            e->setParam (kSweep, 1.0);
        if (at == 256 * 300)
            e->setParam (kShelf, 0.0);
        if (at == 256 * 400)
            e->setParam (kShelf, 1.0);
        if (at == 256 * 450)
            e->setParam (kCleanSub, 1.0);
        if (at == 256 * 500)
            e->setParam (kToneOn, 0.0);
        if (at == 256 * 520)
            e->setParam (kSubBoost, 0.0);
        if (at == 256 * 540)
            e->setParam (kCOn, 0.0);
        if (at == 256 * 560)
            e->setParam (kSweepCurve, kCurveHard);
    });
    double jump = 0.0;
    for (size_t i = 1000; i < y.size (); ++i)
        jump = std::max (jump, (double)std::fabs (y[i] - y[i - 1]));
    CHECK (finite (y) && jump < 0.1, "no clicks (largest step %.3f)", jump);
}

// ---- the gestures (0.27)

namespace {
// a slot pulling `target` with a factory gesture (Loop, its own length, Depth 100 %)
void gesture (Engine& e, int slot, int target, int which, double depth = 1.0)
{
    e.setParam (gestureId (slot, kGestureTarget), target);
    e.setParam (gestureId (slot, kGestureChoice), which);
    e.setParam (gestureId (slot, kGestureDepth), depth);
}
// the host playing from `fromBeat` at `bpm`, block by block (for run's `at`)
std::function<void (size_t)> playing (Engine& e, double bpm, double fromBeat = 0.0)
{
    return [&e, bpm, fromBeat] (size_t at) { e.setTransport (bpm, fromBeat + (double)at / kSr * bpm / 60.0, true); };
}
// a tone mix with something in every band: 40 Hz (the sub), 700 Hz, 3 kHz and 9 kHz
std::vector<float> bandsMix (double seconds)
{
    std::vector<float> x ((size_t)(seconds * kSr));
    for (size_t i = 0; i < x.size (); ++i)
    {
        const double t = (double)i / kSr;
        x[i] = (float)(0.3 * std::sin (2 * kPi * 40.0 * t) + 0.08 * std::sin (2 * kPi * 700.0 * t) + 0.05 * std::sin (2 * kPi * 3000.0 * t) +
                       0.03 * std::sin (2 * kPi * 9000.0 * t));
    }
    return x;
}
const int kAllTargets[] = {kTargetMidLevel,  kTargetHighLevel, kTargetAirLevel, kTargetWobbleRate, kTargetWobbleAmount,
                           kTargetClose,     kTargetLiquid,    kTargetDirt,     kTargetBells,      kTargetMidX,
                           kTargetHighX,     kTargetSeedBlend, kTargetShift};
// what a target needs to be heard (Liquid up, the shifter on, Wobble at some rate, movement for Seed Blend)
void audible (Engine& e, int target)
{
    e.setParam (kBandCount, kBands4);
    if (target == kTargetLiquid)
        e.setParam (kLiquid, 1.0);
    if (target == kTargetShift)
        e.setParam (kShiftOn, 1.0);
    if (target == kTargetWobbleRate)
        e.setParam (kWobbleAmount, 1.0);
    if (target == kTargetSeedBlend)
    {
        e.setParam (kMovement, 1.0);
        e.setParam (kLowPush, 6.0); // (the Low band's own events keep Seed Blend's value)
    }
}
} // namespace

TEST (gesture_curve_breakpoints)
{
    // linear segments between points, a jump where two points share a beat (the later from that beat on), held
    // before the first point and after the last, clamped to the length
    Gesture g;
    const double b[] = {0.5, 1.0, 2.0, 2.0, 3.0}, v[] = {0.2, 1.0, 0.5, 0.0, 0.8};
    CHECK (g.set (b, v, 5, 4.0), "set");
    struct At
    {
        double beat, want;
    };
    for (const At& a : {At {0.0, 0.2}, At {0.5, 0.2}, At {0.75, 0.6}, At {1.0, 1.0}, At {1.5, 0.75}, At {1.999999, 0.5000005},
                        At {2.0, 0.0}, At {2.5, 0.4}, At {3.0, 0.8}, At {3.9, 0.8}, At {4.0, 0.8}, At {9.0, 0.8}, At {-1.0, 0.2}})
        CHECK (std::fabs (g.at (a.beat) - a.want) < 1e-6, "at %.6f: %.6f (want %.6f)", a.beat, g.at (a.beat), a.want);
    // values clamped; out of order, too many, none or past the length refused (the gesture kept)
    const double vb[] = {-0.5, 1.5};
    const double bb[] = {0.0, 1.0};
    CHECK (g.set (bb, vb, 2, 1.0) && g.at (0.0) == 0.0 && g.at (1.0) == 1.0, "values clamped to 0 .. 1");
    const double bad[] = {1.0, 0.5};
    CHECK (!g.set (bad, v, 2, 4.0) && g.count == 2, "out of order: refused");
    CHECK (!g.set (b, v, 0, 4.0) && !g.set (b, v, kMaxGesturePoints + 1, 4.0), "none or too many: refused");
    CHECK (!g.set (b, v, 5, 2.5), "a point past the length: refused");
    Gesture f;
    f.flat (0.3, 2.0);
    CHECK (f.at (0.0) == 0.3 && f.at (1.7) == 0.3 && f.length == 2.0, "flat");
    Gesture none;
    CHECK (none.at (0.3) == 1.0, "no points: 1 (no pull)");
}

TEST (gesture_factory_library)
{
    // 15 gestures, named, within 0 .. 1, each starting and ending on the beat grid (straight or triplet)
    CHECK (kNumFactoryGestures == 15 && kUserGesture == 15, "15 factory gestures, then User");
    const auto& info = paramTable ().info (gestureId (0, kGestureChoice));
    CHECK ((int)info.choices.size () == kNumFactoryGestures + 1 && std::string (info.choices.back ()) == "User", "the Gesture choice lists them and User");
    for (int i = 0; i < kNumFactoryGestures; ++i)
    {
        const Gesture& g = factoryGesture (i);
        CHECK (std::string (info.choices[(size_t)i]) == factoryGestureName (i), "%s in the choice", factoryGestureName (i));
        bool ok = g.count >= 2 && g.length > 0.0 && g.beat[0] == 0.0;
        double lo = 1.0, hi = 0.0;
        for (int k = 0; k < g.count; ++k)
        {
            lo = std::min (lo, g.value[k]);
            hi = std::max (hi, g.value[k]);
            // every point on a 1/8-beat or a 1/12-beat grid
            const double eighth = g.beat[k] * 8.0, twelfth = g.beat[k] * 12.0;
            ok = ok && (std::fabs (eighth - std::round (eighth)) < 1e-9 || std::fabs (twelfth - std::round (twelfth)) < 1e-9);
        }
        CHECK (ok && lo >= 0.0 && hi <= 1.0 && hi - lo >= 0.5, "%s: %d points over %.2f beats, %.2f .. %.2f", factoryGestureName (i), g.count,
               g.length, lo, hi);
        for (int j = 0; j < i; ++j)
            CHECK (std::string (factoryGestureName (i)) != factoryGestureName (j), "unique names");
    }
    // the stutters' grids: 16ths (on for half of each) and 8th-note triplets
    const Gesture& s16 = factoryGesture (kGestureStutter16);
    CHECK (s16.at (0.0) == 1.0 && s16.at (0.124) == 1.0 && s16.at (0.125) == 0.0 && s16.at (0.25) == 1.0 && s16.at (0.9) == 0.0, "1/16 Stutter");
    const Gesture& s12 = factoryGesture (kGestureStutter12);
    CHECK (s12.at (0.1) == 1.0 && s12.at (0.2) == 0.0 && s12.at (0.34) == 1.0 && s12.at (0.5 + 1e-6) == 0.0, "1/12 Stutter");
}

TEST (gesture_loop_wrap)
{
    // Loop: frac (beats / Length - Position); it wraps to the start at each Length, from the song's beat 0
    for (double length : {0.5, 1.0, 4.0, 8.0, 32.0})
        for (double beats : {0.0, 0.3, 3.99, 4.0, 17.25, 100.5})
        {
            const double pos = gesturePosition (kModeLoop, beats, length, 1.0, 0.0);
            const double want = std::fmod (beats, length) / length;
            CHECK (std::fabs (pos - want) < 1e-12 && pos >= 0.0 && pos < 1.0, "length %.1f, beat %.2f: %.4f (%.4f)", length, beats, pos, want);
        }
    CHECK (std::fabs (gesturePosition (kModeLoop, 1.0, 4.0, 1.0, 0.25) - 0.0) < 1e-12, "Position 25 %%: the loop starts a quarter later");
    CHECK (std::fabs (gesturePosition (kModeLoop, 0.0, 4.0, 1.0, 0.25) - 0.75) < 1e-12, "Position 25 %%: before it, the loop's last quarter");
    CHECK (gesturePosition (kModeLoop, -1.0, 4.0, 1.0, 0.0) == 0.75, "before the song's start");
    // a gesture played across its loop point: the value jumps back exactly at the Length (Cell Fade: 0, then 1)
    const Gesture& g = factoryGesture (kGestureCellFade);
    auto at = [&] (double beats) { return g.at (gesturePosition (kModeLoop, beats, 8.0, 1.0, 0.0) * g.length); };
    CHECK (at (7.999) == 0.0 && at (8.0) == 1.0 && at (16.0) == 1.0 && std::fabs (at (12.5) - 0.5) < 1e-12, "Cell Fade wraps every 8 beats");
    // a Length other than the gesture's own stretches it: Cell Fade over 4 beats is at its middle at beat 2
    CHECK (std::fabs (g.at (gesturePosition (kModeLoop, 2.0, 4.0, 1.0, 0.0) * g.length) - g.at (4.0)) < 1e-12, "stretched to Length");
}

TEST (gesture_walk_reversal)
{
    // Walk: forwards over one Length, backwards over the next; the place is continuous through each turn (and
    // so the value), the direction changes there; Hold stays at Position
    for (double speed : {0.125, 0.5, 1.0, 4.0})
    {
        const double length = 4.0, turn = length / speed;
        double prev = gesturePosition (kModeWalk, 0.0, length, speed, 0.0), worst = 0.0;
        const double step = 0.001;
        for (double b = step; b < 3.0 * turn; b += step)
        {
            const double pos = gesturePosition (kModeWalk, b, length, speed, 0.0);
            worst = std::max (worst, std::fabs (pos - prev));
            prev = pos;
        }
        CHECK (worst <= step * speed / length * 1.0001, "x%.3f: no jumps (the largest step %.6f)", speed, worst);
        const double before = gesturePosition (kModeWalk, turn - 0.01, length, speed, 0.0), at = gesturePosition (kModeWalk, turn, length, speed, 0.0);
        const double after = gesturePosition (kModeWalk, turn + 0.01, length, speed, 0.0);
        CHECK (std::fabs (at - 1.0) < 1e-12 && before < at && after < at && std::fabs (before - after) < 1e-9, "x%.3f: turns at the end after %.1f beats",
               speed, turn);
        CHECK (std::fabs (gesturePosition (kModeWalk, 2.0 * turn, length, speed, 0.0)) < 1e-12, "x%.3f: back at the start", speed);
    }
    for (double pos : {0.0, 0.3, 1.0})
        CHECK (gesturePosition (kModeWalk, 123.4, 4.0, 0.0, pos) == pos, "Hold: at Position %.1f", pos);
    // the engine: a Walk across its turns, Talk on Liquid Pos, the curve's value continuous (no step beyond
    // what the curve's steepest slope allows in a tick)
    auto e = fresh ();
    tailNeutral (*e);
    gesture (*e, 0, kTargetLiquid, kGestureTalk);
    e->setParam (kLiquid, 1.0);
    e->setParam (gestureId (0, kGestureMode), kModeWalk);
    e->setParam (gestureId (0, kGestureSpeed), 6); // x4: a turn every half beat
    e->reset ();
    const std::vector<float> z (Engine::kTick, 0.1f);
    std::vector<float> l (Engine::kTick), r (Engine::kTick);
    double prevV = -1.0, worst = 0.0;
    for (int t = 0; t < 4000; ++t)
    {
        e->setTransport (120.0, (double)(t * Engine::kTick) / kSr * 2.0, true);
        e->process (z.data (), z.data (), l.data (), r.data (), Engine::kTick);
        if (prevV >= 0.0)
            worst = std::max (worst, std::fabs (e->gestureValue (0) - prevV));
        prevV = e->gestureValue (0);
    }
    // (Talk's steepest segment: 0.6 in 1/3 beat; x4 over its 2 beats: 0.6 / (1/3 / 4) per beat; a tick is 16 / 24000 beats)
    const double most = 0.6 / (1.0 / 3.0 / 4.0) * Engine::kTick / 24000.0;
    CHECK (worst <= most * 1.01, "the value through the turns: steps of %.4f at most (%.4f allowed)", worst, most);
}

TEST (gestures_off_are_026)
{
    // every Target Off (the default, and a state saved before 0.27): whatever the other gesture settings, the
    // render is a new instance's, bit for bit; Wobble Amount 0 too
    for (int g = 0; g < kNumGestureSlots; ++g)
    {
        CHECK (defaultParams ()[gestureId (g, kGestureTarget)] == kTargetOff, "slot %d: Target Off by default", g + 1);
        for (int version = 1; version <= 5; ++version)
            CHECK (defaultNormalizedForVersion (gestureId (g, kGestureTarget), version) == 0.0, "slot %d: Off in a version %d state", g + 1, version);
    }
    CHECK (defaultParams ()[kWobbleAmount] == 0.0 && defaultNormalizedForVersion (kWobbleAmount, 5) == 0.0, "Wobble Amount 0");
    const auto x = reese (2.0);
    auto plainNew = fresh ();
    const auto want = run (*plainNew, x, nullptr, 256, playing (*plainNew, 150.0));
    auto e = fresh ();
    for (int g = 0; g < kNumGestureSlots; ++g)
    {
        e->setParam (gestureId (g, kGestureChoice), g + 3);
        e->setParam (gestureId (g, kGestureMode), kModeWalk);
        e->setParam (gestureId (g, kGestureLength), 3);
        e->setParam (gestureId (g, kGestureDepth), -0.7);
        e->setParam (gestureId (g, kGestureSmooth), 0.5);
    }
    e->setParam (kIntensity, 0.4);
    e->setParam (kWobbleRate, 9.0);
    e->reset ();
    const auto got = run (*e, x, nullptr, 256, playing (*e, 150.0));
    CHECK (got == want, "gesture settings with every Target Off: a new instance's sound, bit for bit");
    CHECK (!e->gesturesRunning (), "nothing of it runs");
    // a slot switched on and off again: back to running nothing
    auto f = fresh ();
    gesture (*f, 0, kTargetHighLevel, kGestureStutter16);
    run (*f, reese (0.5), nullptr, 256, playing (*f, 150.0));
    CHECK (f->gesturesRunning (), "on: it runs");
    f->setParam (gestureId (0, kGestureTarget), kTargetOff);
    run (*f, reese (0.2), nullptr, 256, playing (*f, 150.0, 2.0));
    CHECK (!f->gesturesRunning () && f->gesturePull (0) == 0.0, "off again: faded out, not run");
}

TEST (gesture_level_gate)
{
    // 1/16 Stutter on High Level: the High band (a 6 kHz tone, alone) is at its level for the first half of each
    // 16th and silent for the second, in time with the song (120 bpm: a 16th is 125 ms); Depth 50 % halves the
    // dB travel; a negative Depth turns it round
    const auto x = sine (6000.0, 0.25, 2.0);
    auto render = [&] (double depth) {
        auto e = engine ();
        plain (*e);
        e->setParam (kXoverMid, 1000.0);
        solo (*e, kBandHigh);
        gesture (*e, 0, kTargetHighLevel, kGestureStutter16, depth);
        e->reset ();
        return run (*e, x, nullptr, 128, playing (*e, 120.0));
    };
    const size_t sixteenth = (size_t)(0.125 * kSr);
    for (double depth : {1.0, 0.5, -1.0})
    {
        const auto y = render (depth);
        double onDb = 0.0, offDb = 0.0;
        for (int k = 4; k < 14; ++k)
        {
            const size_t a = (size_t)k * sixteenth;
            onDb += db (rms (y, a + sixteenth / 8, a + sixteenth / 2 - 100) / rms (x, a, a + sixteenth / 2)) / 10.0;
            offDb += db (rms (y, a + sixteenth / 2 + sixteenth / 8, a + sixteenth - 100) / rms (x, a, a + sixteenth / 2)) / 10.0;
        }
        std::printf ("    Depth %+.0f %%: on %.2f dB, off %.2f dB\n", depth * 100.0, onDb, offDb);
        if (depth == 1.0)
            CHECK (std::fabs (onDb) < 0.2 && offDb < -60.0, "on at its level, off silent");
        else if (depth == 0.5)
            CHECK (std::fabs (onDb) < 0.2 && std::fabs (offDb + 24.0) < 0.5, "Depth 50 %%: off at -24 dB");
        else
            CHECK (onDb < -60.0 && std::fabs (offDb) < 0.2, "Depth -100 %%: the other way round");
        double jump = 0.0;
        for (size_t i = 1000; i < y.size (); ++i)
            jump = std::max (jump, (double)std::fabs (y[i] - y[i - 1]));
        CHECK (jump < 0.25 * 2.0 * kPi * 6000.0 / kSr * 1.2, "no clicks at the gate's edges (largest step %.3f)", jump);
    }
}

TEST (gesture_tempo_and_transport)
{
    // the gestures follow the song position at any tempo; stopped, they run on at the last tempo; with no tempo
    // at all, at 120
    auto e = fresh ();
    gesture (*e, 0, kTargetHighLevel, kGestureRampUp);
    e->setParam (gestureId (0, kGestureLength), 5); // 8 beats
    e->reset ();
    const std::vector<float> z (512, 0.0f);
    std::vector<float> l (512), r (512);
    for (double bpm : {90.0, 174.0})
    {
        e->setTransport (bpm, 6.0, true);
        e->process (z.data (), z.data (), l.data (), r.data (), 512);
        const double end = 6.0 + 512.0 / kSr * bpm / 60.0;
        CHECK (std::fabs (e->gestureBeats () - end) < 1e-9 && std::fabs (e->gesturePos (0) - end / 8.0) < 1e-9, "%.0f bpm: at beat %.4f (%.4f)",
               bpm, e->gestureBeats (), end);
    }
    // stopped: runs on at 174 bpm from where it was
    const double from = e->gestureBeats ();
    for (int b = 0; b < 10; ++b)
    {
        e->setTransport (0.0, 0.0, false); // (no tempo given: the last one)
        e->process (z.data (), z.data (), l.data (), r.data (), 512);
    }
    const double ran = e->gestureBeats () - from, want = 10.0 * 512.0 / kSr * 174.0 / 60.0;
    CHECK (std::fabs (ran - want) < 1e-9, "stopped: %.4f beats in 10 blocks at the last tempo (%.4f)", ran, want);
    auto f = fresh ();
    gesture (*f, 0, kTargetHighLevel, kGestureRampUp);
    f->reset ();
    f->process (z.data (), z.data (), l.data (), r.data (), 512);
    CHECK (std::fabs (f->gestureBeats () - 512.0 / kSr * 2.0) < 1e-9, "never told a tempo: 120 bpm");
    // a tempo change while playing: the value follows the song position, without a step
    auto g = fresh ();
    tailNeutral (*g);
    gesture (*g, 0, kTargetHighLevel, kGestureRampUp);
    g->setParam (gestureId (0, kGestureLength), 5);
    g->reset ();
    double beat = 0.0, prev = -1.0, worst = 0.0;
    for (int b = 0; b < 400; ++b)
    {
        const double bpm = b < 200 ? 100.0 : 160.0;
        g->setTransport (bpm, beat, true);
        g->process (z.data (), z.data (), l.data (), r.data (), 512);
        beat += 512.0 / kSr * bpm / 60.0;
        if (prev >= 0.0 && g->gesturePos (0) > prev)
            worst = std::max (worst, g->gestureValue (0) - prev);
        prev = g->gestureValue (0);
    }
    CHECK (worst < 512.0 / kSr * 160.0 / 60.0 / 8.0 * 1.05, "across the tempo change: steps of %.5f at most", worst);
}

TEST (gesture_playhead_jump)
{
    // jumping the playhead (backwards, into the middle of a gesture) glides the value over Smooth's 2 ms rather
    // than stepping: no click; the place follows the jump at once
    const auto x = sine (3000.0, 0.25, 1.5);
    auto e = engine ();
    plain (*e);
    e->setParam (kXoverMid, 1000.0);
    solo (*e, kBandHigh);
    gesture (*e, 0, kTargetHighLevel, kGestureCellFade);
    e->reset ();
    // 120 bpm from beat 5.5 (fading: the value about 0.17); at 0.75 s back to beat 1 (held at 1)
    double worstStep = 0.0;
    const auto y = run (*e, x, nullptr, 64, [&] (size_t at) {
        const double t = (double)at / kSr;
        e->setTransport (120.0, t < 0.75 ? 5.5 + 2.0 * t : 1.0 + 2.0 * (t - 0.75), true);
    });
    for (size_t i = 2000; i < y.size (); ++i)
        worstStep = std::max (worstStep, (double)std::fabs (y[i] - y[i - 1]));
    const double sineStep = 0.25 * 2.0 * kPi * 3000.0 / kSr;
    CHECK (worstStep < sineStep * 1.3, "no click at the jump (largest step %.4f, the sine's own %.4f)", worstStep, sineStep);
    // the level after the jump: the band back at its level within 15 ms
    const size_t jumpAt = (size_t)(0.75 * kSr);
    const double before = rms (y, jumpAt - 1200, jumpAt - 200), after = rms (y, jumpAt + 720, jumpAt + 1920);
    CHECK (db (after / rms (x, jumpAt, jumpAt + 1200)) > -0.5 && before < 0.5 * after, "back at its level (%.1f dB before, %.1f dB after)",
           db (before / 0.177), db (after / 0.177));
}

TEST (wobble_phase_continuous)
{
    // Wobble's phase is the integral of its rate: across the loop point of a rate gesture (Rate Rise snaps from
    // 0.9 back to 0.2 of the range) the phase runs on without a jump, its step each tick the rate x the tick's
    // beats, and the gain on the bands above Low moves no more than the fastest rate allows
    auto e = fresh ();
    tailNeutral (*e);
    gesture (*e, 0, kTargetWobbleRate, kGestureRateRise);
    e->setParam (kWobbleAmount, 1.0);
    e->reset ();
    const std::vector<float> z (Engine::kTick, 0.1f);
    std::vector<float> l (Engine::kTick), r (Engine::kTick);
    const double bpm = 150.0, beatsPerTick = Engine::kTick / kSr * bpm / 60.0;
    double prevPhase = e->wobblePhase (), worstPhase = 0.0, worstGain = 0.0, prevGain = e->wobbleGainNow (), loRate = 1e9, hiRate = 0.0;
    double expect = 0.0;
    bool stepsRight = true;
    for (int t = 0; t < (int)(12.0 / beatsPerTick); ++t) // (three loops of 4 beats)
    {
        e->setTransport (bpm, t * beatsPerTick, true);
        e->process (z.data (), z.data (), l.data (), r.data (), Engine::kTick);
        double d = e->wobblePhase () - prevPhase;
        d -= std::floor (d + 0.5); // (wrapped to the nearest)
        if (t > 0)
            stepsRight = stepsRight && std::fabs (d - e->wobbleRate () * beatsPerTick) < 1e-9;
        worstPhase = std::max (worstPhase, std::fabs (d));
        worstGain = std::max (worstGain, std::fabs (e->wobbleGainNow () - prevGain));
        prevPhase = e->wobblePhase ();
        prevGain = e->wobbleGainNow ();
        loRate = std::min (loRate, e->wobbleRate ());
        hiRate = std::max (hiRate, e->wobbleRate ());
        expect += e->wobbleRate () * beatsPerTick;
    }
    std::printf ("    rate %.2f .. %.2f cycles per beat; largest phase step %.4f, gain step %.4f\n", loRate, hiRate, worstPhase, worstGain);
    CHECK (stepsRight, "each tick the phase moves by the rate x the tick's beats");
    CHECK (worstPhase <= kWobbleRateMax * beatsPerTick, "no jump in the phase at the loop points");
    CHECK (worstGain <= 2.0 * kPi * 0.5 * hiRate * beatsPerTick * 1.01, "the gain moves at most as the fastest rate lets it (%.4f)", worstGain);
    CHECK (hiRate > 3.0 * loRate, "the rate gesture moves the rate (%.2f .. %.2f)", loRate, hiRate);
    // at Wobble Rate x2 with no gesture, the tremolo's period is half a beat at 2 cycles per beat
    auto f = fresh ();
    tailNeutral (*f);
    f->setParam (kWobbleAmount, 1.0);
    f->setParam (kWobbleRate, 2.0);
    f->reset ();
    for (int t = 0; t < 3000; ++t)
    {
        f->setTransport (120.0, t * Engine::kTick / kSr * 2.0, true);
        f->process (z.data (), z.data (), l.data (), r.data (), Engine::kTick);
    }
    const double beatsGone = 3000.0 * Engine::kTick / kSr * 2.0, cycles = 2.0 * beatsGone;
    double off = (cycles - std::floor (cycles)) - f->wobblePhase ();
    off -= std::round (off);
    CHECK (std::fabs (off) < 1e-6, "2 cycles per beat: at phase %.4f after %.3f beats (%.4f)", f->wobblePhase (), beatsGone, cycles - std::floor (cycles));
}

TEST (gestures_never_touch_low)
{
    // every target, pulled hard (a 1/16 Stutter at Depth 100 %): the sub (40 Hz, under every Low X) keeps its
    // level within 0.5 dB, and a mono input stays mono (the channels share every gain and coefficient)
    const auto x = bandsMix (2.0);
    const size_t a = (size_t)(0.5 * kSr), b = x.size ();
    for (int seed : {1, 70})
    {
        auto render = [&] (int target, std::vector<float>* right) {
            auto e = fresh ();
            tailNeutral (*e);
            e->setParam (kSeed, seed);
            if (target != kTargetOff)
            {
                audible (*e, target);
                gesture (*e, 0, target, kGestureStutter16);
                gesture (*e, 1, target, kGestureRampUp, -1.0);
            }
            e->reset ();
            return run (*e, x, right, 256, playing (*e, 150.0));
        };
        for (int target : kAllTargets)
        {
            auto ref = [&] {
                auto e = fresh ();
                tailNeutral (*e);
                e->setParam (kSeed, seed);
                audible (*e, target);
                e->reset ();
                return run (*e, x, nullptr, 256, playing (*e, 150.0));
            }();
            std::vector<float> r;
            const auto y = render (target, &r);
            const double change = db (toneAt (y, 40.0, a, b) / toneAt (ref, 40.0, a, b));
            const double moved = db (rms (y, a, b) / rms (ref, a, b));
            const char* name = paramTable ().info (gestureId (0, kGestureTarget)).choices[(size_t)target];
            std::printf ("    seed %3d, %-13s: the sub %+.3f dB (the whole %+.2f dB)\n", seed, name, change, moved);
            CHECK (std::fabs (change) < 0.5, "%s: the sub keeps its level (%.3f dB)", name, change);
            CHECK (y == r, "%s: mono in, mono out", name);
            CHECK (finite (y), "%s: finite", name);
        }
    }
}

TEST (gesture_targets_act)
{
    // each target does what it says: Close takes the highs down, Dirt the saturator's harmonics, Bells the bells,
    // Mid X moves the crossover, Shift the shift, Liquid Pos the resonance; Dirt and Bells stay level matched
    const auto x = reese (2.0);
    const size_t a = (size_t)(1.0 * kSr), b = x.size ();
    auto render = [&] (int target, double depth, Engine** keep = nullptr) {
        static std::unique_ptr<Engine> held;
        held = fresh ();
        tailNeutral (*held);
        audible (*held, target);
        if (target != kTargetOff)
        {
            // a flat line at 0 (Depth 100 %: all the way), via a user gesture
            static Gesture zero;
            zero.flat (0.0);
            held->setUserGesture (0, &zero);
            gesture (*held, 0, target, kUserGesture, depth);
        }
        held->reset ();
        auto y = run (*held, x, nullptr, 256, playing (*held, 150.0));
        if (keep)
            *keep = held.get ();
        return y;
    };
    const auto ref = render (kTargetOff, 1.0);
    // Close: the highs (5 kHz) down a lot, the sub not at all
    Engine* e = nullptr;
    const auto closed = render (kTargetClose, 1.0, &e);
    const double hiDrop = db (toneAt (closed, 55.0 * 61, a, b) / toneAt (ref, 55.0 * 61, a, b));
    std::printf ("    Close: corner %.0f Hz, 3.4 kHz %+.1f dB\n", e->closeHz (), hiDrop);
    CHECK (std::fabs (e->closeHz () - kOceanToneHz * std::exp2 (-kCloseOctaves)) < 1.0 && hiDrop < -30.0, "Close: down to Tone / 2^6, the highs cut");
    // Dirt: clean (fewer harmonics: the 3rd's share of the 1st falls) and about as loud; Bells: a different
    // sound, as loud
    const auto clean = render (kTargetDirt, 1.0, &e);
    CHECK (e->dirtPull () == 1.0, "Dirt: all the way");
    const auto bare = render (kTargetBells, 1.0, &e);
    CHECK (e->bellsPull () == 1.0, "Bells: all the way");
    // (the bands above Low only: the RMS above 600 Hz, a Linkwitz-Riley high-pass)
    auto upper = [&] (const std::vector<float>& y) {
        dsp::Lr4Split hp;
        dsp::SvfCoefs c;
        c.set (std::tan (kPi * 600.0 / kSr), dsp::kSqrt2);
        double s = 0.0;
        for (size_t i = 0; i < b; ++i)
        {
            double lo, hi;
            hp.tick (y[i], c, lo, hi);
            if (i >= a)
                s += hi * hi;
        }
        return std::sqrt (s / (double)(b - a));
    };
    const double cleanDb = db (upper (clean) / upper (ref)), bareDb = db (upper (bare) / upper (ref));
    std::printf ("    Dirt: the upper bands %+.2f dB; Bells: %+.2f dB\n", cleanDb, bareDb);
    // (Dirt is level matched; Bells takes the bells' cuts and boosts away, so the upper bands' balance changes)
    CHECK (std::fabs (cleanDb) < 1.5 && std::fabs (bareDb) < 6.0, "Dirt about as loud, Bells within 6 dB (%.2f, %.2f dB)", cleanDb, bareDb);
    CHECK (clean != ref && bare != ref && clean != bare, "Dirt and Bells change the sound");
    // Mid X at 0 of its range: 400 Hz
    render (kTargetMidX, 1.0, &e);
    const double lowest = std::max (400.0, e->lowXover () * std::exp2 (1.0 / 3.0)); // (a third of an octave above Low X at least)
    CHECK (std::fabs (e->xoverHz (0, 1) - lowest) < 1.0, "Mid X: at %.1f Hz (%.1f)", lowest, e->xoverHz (0, 1));
    render (kTargetShift, 1.0, &e);
    CHECK (std::fabs (e->shiftNow () + 500.0) < 1e-6, "Shift: at -500 Hz (%.1f)", e->shiftNow ());
    render (kTargetLiquid, 1.0, &e);
    CHECK (std::fabs (e->liquidHz () - toPlain (kLiquidLow, defaultNormalized (kLiquidLow))) < 1.0, "Liquid Pos: at Liquid Low (%.1f Hz)", e->liquidHz ());
    render (kTargetHighLevel, -1.0, &e);
    CHECK (e->gestureGain (kBandHigh) == 1.0, "Depth -100 %%: the flat 0 turned to 1, no pull");
    render (kTargetHighLevel, 1.0, &e);
    CHECK (e->gestureGain (kBandHigh) == 0.0 && e->bandGainDb (0, kBandHigh) <= -100.0 && e->gestureGain (kBandMid) == 1.0, "High Level: silent, Mid untouched");
    // Intensity scales every slot: at 50 % High Level is half way down (-24 dB)
    auto h = fresh ();
    static Gesture zero2;
    zero2.flat (0.0);
    h->setUserGesture (1, &zero2);
    gesture (*h, 1, kTargetHighLevel, kUserGesture, 1.0);
    h->setParam (kIntensity, 0.5);
    h->reset ();
    run (*h, x, nullptr, 256);
    CHECK (std::fabs (db (h->gestureGain (kBandHigh)) + 24.0) < 1e-9, "Intensity 50 %%: -24 dB (%.2f)", db (h->gestureGain (kBandHigh)));
}

TEST (gesture_target_change_fades)
{
    // switching a slot's Target (or the gesture on and off) fades: no click
    const auto x = bandsMix (3.0);
    auto e = fresh ();
    tailNeutral (*e);
    static Gesture zero;
    zero.flat (0.0);
    e->setUserGesture (0, &zero);
    gesture (*e, 0, kTargetHighLevel, kUserGesture);
    e->reset ();
    const auto y = run (*e, x, nullptr, 256, [&] (size_t at) {
        if (at == 256 * 100)
            e->setParam (gestureId (0, kGestureTarget), kTargetClose);
        if (at == 256 * 200)
            e->setParam (gestureId (0, kGestureTarget), kTargetMidLevel);
        if (at == 256 * 300)
            e->setParam (gestureId (0, kGestureTarget), kTargetOff);
        if (at == 256 * 400)
            e->setParam (gestureId (0, kGestureTarget), kTargetDirt);
        if (at == 256 * 450)
            e->setParam (kWobbleAmount, 1.0);
    });
    auto f = fresh ();
    tailNeutral (*f);
    const auto ref = run (*f, x, nullptr, 256);
    double jump = 0.0, own = 0.0;
    for (size_t i = 1000; i < y.size (); ++i)
    {
        jump = std::max (jump, (double)std::fabs (y[i] - y[i - 1]));
        own = std::max (own, (double)std::fabs (ref[i] - ref[i - 1]));
    }
    CHECK (finite (y) && jump < 1.5 * own, "no clicks (largest step %.3f, without the gestures %.3f)", jump, own);
}

TEST (gesture_files)
{
    // moistr's own format; the extractor's (raw values with min and max, song beats); errors
    GestureData d;
    std::string err;
    CHECK (parseGestureJson (R"({"name": "Up", "length_beats": 2, "points": [[0, 0], [1, 0.5], [1, 1], [2, 0.25]]})", "file", d, err), "%s", err.c_str ());
    CHECK (d.name == "Up" && d.length == 2.0 && d.points.size () == 4 && d.points[2].second == 1.0, "moistr's format");
    Gesture g;
    CHECK (toGesture (d, g) && g.at (1.0) == 1.0 && std::fabs (g.at (0.5) - 0.25) < 1e-12, "as a curve (the jump kept)");
    const std::string extractor = R"({"name": "Track > Device > Gain", "source": {"kind": "arrangement"}, "time_unit": "beats",
        "points": [[64.0, -12.0], [66.0, 0.0], [66.0, -24.0], [68.0, -24.0]], "min": -24.0, "max": 0.0})";
    CHECK (parseGestureJson (extractor, "file", d, err), "%s", err.c_str ());
    CHECK (d.length == 4.0 && d.points.front ().first == 0.0 && d.points[1].first == 2.0 && d.points[0].second == 0.5 && d.points[1].second == 1.0 &&
               d.points[2].second == 0.0,
           "the extractor's: from its first point, normalised by min and max");
    // what scripts/als_extract.py --moistr writes: already 0 .. 1 (source_min and source_max are the lane's range)
    CHECK (parseGestureJson (R"({"name": "Gain", "length_beats": 2.0, "points": [[0.0, 0.0], [0.5, 1.0], [0.5, 0.25], [2.0, 1.0]], "source_min": -12.0, "source_max": 0.0})",
                             "file", d, err) &&
               d.name == "Gain" && d.length == 2.0 && d.points.size () == 4 && d.points[2].second == 0.25,
           "the extractor's --moistr files");
    CHECK (parseGestureJson (R"({"points": [[0, 3], [1, 3]], "min": 3, "max": 3})", "Flat", d, err) && d.name == "Flat" && d.points[0].second == 1.0,
           "a flat lane: 1; no name: the file's");
    CHECK (parseGestureJson (R"({"points": [[1, 0.2], [0, 0.4], [0.5, 2]]})", "x", d, err) && d.points[0].first == 0.0 && d.points[0].second == 0.4 &&
               d.points[1].second == 1.0,
           "sorted, clamped");
    CHECK (!parseGestureJson ("[1, 2]", "x", d, err), "not an object: %s", err.c_str ());
    CHECK (!parseGestureJson (R"({"points": []})", "x", d, err), "no points: %s", err.c_str ());
    CHECK (!parseGestureJson (R"({"points": [[0, 1]], "time_unit": "seconds"})", "x", d, err), "seconds: %s", err.c_str ());
    CHECK (!parseGestureJson (R"({"points": [[0, "a"]]})", "x", d, err), "a bad point: %s", err.c_str ());
    CHECK (!parseGestureJson (R"({"points": [[0, 1]] )", "x", d, err), "cut short: %s", err.c_str ());
    // round trip through moistr's format
    GestureData back;
    d.name = "Say \"hi\"";
    d.length = 3.0;
    d.points = {{0.0, 0.1}, {1.5, 0.9}, {1.5, 0.3}, {3.0, 0.3}};
    CHECK (parseGestureJson (gestureJson (d), "x", back, err) && back.name == d.name && back.length == 3.0 && back.points == d.points, "round trip");
    // too many points
    std::string many = "{\"points\": [";
    for (int i = 0; i <= kMaxGesturePoints; ++i)
        many += (i ? ", [" : "[") + std::to_string (i) + ", 0.5]";
    CHECK (!parseGestureJson (many + "]}", "x", d, err), "too many: %s", err.c_str ());
}

TEST (gesture_presets)
{
    // the gesture presets (presets/Gestures): each picks one gesture (no 0.27 slot) and renders clean (finite, mono,
    // no clicks beyond the plain sound's), and the sub (the Reese's 55 Hz) stays within 0.5 dB of the same settings
    // without the gesture
    const auto x = reese (4.0);
    const size_t a = (size_t)(1.0 * kSr), b = x.size ();
    int checked = 0;
    for (const char* rel : {"Gestures/Reese Cell.txt", "Gestures/Stutter Wobble.txt", "Gestures/Talking Reese.txt", "Gestures/Crossover Scan.txt"})
    {
        auto e = fresh ();
        tailNeutral (*e);
        CHECK (loadPreset (*e, rel), "%s loads", rel);
        e->reset ();
        auto off = fresh ();
        tailNeutral (*off);
        loadPreset (*off, rel);
        off->setParam (kScene, kSceneNone);
        off->setParam (kWobbleAmount, 0.0);
        bool slots = false;
        for (int g = 0; g < kNumGestureSlots; ++g)
            slots = slots || std::lround (e->param (gestureId (g, kGestureTarget))) != kTargetOff;
        CHECK (!slots && std::lround (e->param (kScene)) > kSceneNone && std::lround (e->param (kScene)) < kSceneUser, "%s: one factory gesture, no slots", rel);
        off->reset ();
        std::vector<float> r;
        const auto y = run (*e, x, &r, 256, playing (*e, 150.0));
        const auto ref = run (*off, x, nullptr, 256, playing (*off, 150.0));
        double jump = 0.0, own = 0.0;
        for (size_t i = 1000; i < y.size (); ++i)
        {
            jump = std::max (jump, (double)std::fabs (y[i] - y[i - 1]));
            own = std::max (own, (double)std::fabs (ref[i] - ref[i - 1]));
        }
        const double sub = db (toneAt (y, 55.0, a, b) / toneAt (ref, 55.0, a, b));
        std::printf ("    %-28s the sub %+.3f dB, the whole %+.2f dB, largest step %.3f (%.3f without)\n", rel, sub, db (rms (y, a, b) / rms (ref, a, b)), jump, own);
        CHECK (e->gesturesRunning () && e->scenePlaying () != nullptr && finite (y) && y == r, "%s: the gesture on, finite, mono", rel);
        CHECK (std::fabs (sub) < 0.5, "%s: the sub keeps its level (%.3f dB)", rel, sub);
        CHECK (jump < 1.5 * own, "%s: no clicks", rel);
        ++checked;
    }
    CHECK (checked == 4, "%d gesture presets", checked);
}

// ---- the one gesture (0.30): a scene of lanes on one clock

namespace {
// the Gesture choice on a factory scene (Loop, its own length, Amount 100 %)
void sceneOn (Engine& e, int which) { e.setParam (kScene, which + 1); }
// a user scene from JSON (kept alive by the caller)
std::unique_ptr<Scene> sceneFrom (const std::string& json)
{
    SceneData d;
    std::string err;
    auto s = std::make_unique<Scene> ();
    if (!parseSceneJson (json, "test", d, err) || !toScene (d, *s))
        std::printf ("    scene: %s\n", err.c_str ());
    return s;
}
} // namespace

TEST (scene_factory_library)
{
    // 10 factory gestures, each of two or more lanes on real targets (never the Low band), every point on a 1/8- or
    // 1/12-beat grid, every lane's curve within 0 .. 1 and moving; listed in the Gesture choice between None and User
    CHECK (kNumFactoryScenes == 10, "%d factory gestures", kNumFactoryScenes);
    const auto& info = paramTable ().info (kScene);
    CHECK ((int)info.choices.size () == kNumFactoryScenes + 2 && std::string (info.choices.front ()) == "None" &&
               std::string (info.choices.back ()) == "User",
           "the Gesture choice: None, the factory gestures, User");
    for (int i = 0; i < kNumFactoryScenes; ++i)
    {
        const Scene& s = factoryScene (i);
        CHECK (std::string (info.choices[(size_t)i + 1]) == factorySceneName (i), "%s in the choice", factorySceneName (i));
        CHECK (s.count >= 2 && s.count <= kMaxSceneLanes && s.length > 0.0, "%s: %d lanes over %.2f beats", factorySceneName (i), s.count, s.length);
        bool grid = true, ranges = true;
        for (int l = 0; l < s.count; ++l)
        {
            const SceneLane& lane = s.lane[l];
            ranges = ranges && lane.target > kTargetOff && lane.target < kNumTargets && lane.curve.count >= 2 && lane.curve.length == s.length &&
                     (lane.target == kTargetMidX || lane.target == kTargetHighX
                          ? std::min (lane.lo, lane.hi) >= std::log2 (20.0) && std::max (lane.lo, lane.hi) <= std::log2 (20000.0) // (log2 Hz)
                          : lane.lo >= 0.0 && lane.lo <= 1.0 && lane.hi >= 0.0 && lane.hi <= 1.0);
            double lo = 1.0, hi = 0.0;
            for (int k = 0; k < lane.curve.count; ++k)
            {
                const double eighth = lane.curve.beat[k] * 8.0, twelfth = lane.curve.beat[k] * 12.0;
                grid = grid && (std::fabs (eighth - std::round (eighth)) < 1e-9 || std::fabs (twelfth - std::round (twelfth)) < 1e-9);
                lo = std::min (lo, lane.curve.value[k]);
                hi = std::max (hi, lane.curve.value[k]);
            }
            ranges = ranges && (hi - lo > 0.2 || s.lane[l].lo == s.lane[l].hi);
            for (int m = 0; m < l; ++m)
                ranges = ranges && s.lane[m].target != lane.target;
        }
        CHECK (grid, "%s: every point on a straight or triplet grid", factorySceneName (i));
        CHECK (ranges, "%s: real targets (one lane each), curves within 0 .. 1", factorySceneName (i));
    }
    // Reese Cell as described: the mids fade out over 2.5 beats while the highs swell, Close shuts on beat 6 and
    // opens on the downbeat, Wobble Rate 3 .. 9 then 38, Dirt the other way to the mids
    const Scene& r = factoryScene (kSceneReeseCell);
    auto laneOf = [&r] (int target) -> const SceneLane* {
        for (int l = 0; l < r.count; ++l)
            if (r.lane[l].target == target)
                return &r.lane[l];
        return nullptr;
    };
    const SceneLane *mid = laneOf (kTargetMidLevel), *high = laneOf (kTargetHighLevel), *close = laneOf (kTargetClose), *rate = laneOf (kTargetWobbleRate),
                    *dirt = laneOf (kTargetDirt);
    CHECK (mid && high && close && rate && dirt, "Reese Cell's lanes");
    if (mid && high && close && rate && dirt)
    {
        CHECK (mid->curve.at (3.0) == 1.0 && mid->curve.at (5.5) == 0.0 && high->curve.at (2.0) == 0.0 && high->curve.at (4.0) == 1.0, "mids out, highs in");
        CHECK (close->closeHz && close->hzLo == 400.0 && close->curve.at (5.9) == 1.0 && close->curve.at (7.0) == 0.0, "Close: open, then shut at 400 Hz");
        const double r3 = std::exp2 (std::log2 (3.0) + (std::log2 (38.0) - std::log2 (3.0)) * rate->curve.at (0.0));
        const double r9 = std::exp2 (std::log2 (3.0) + (std::log2 (38.0) - std::log2 (3.0)) * rate->curve.at (4.0 + 1.0 / 3.0));
        CHECK (std::fabs (r3 - 3.0) < 1e-9 && std::fabs (r9 - 9.0) < 1e-9 && rate->curve.at (7.5) == 1.0, "Wobble Rate 3, 9, then 38 (%.3f, %.3f)", r3, r9);
        double off = 0.0;
        for (double b = 0.0; b < 8.0; b += 1.0 / 24.0)
            off = std::max (off, std::fabs (dirt->curve.at (b) - (1.0 - mid->curve.at (b))));
        CHECK (off < 1e-9, "Dirt crossfades the other way to the mids");
    }
}

TEST (scene_lanes_in_tandem)
{
    // every lane at the same place: with Walk at Hold the clock stays at Position, and each lane's value is its
    // curve there; playing, every lane follows the one clock (Loop: frac (beats / Length))
    for (int which : {kSceneReeseCell, kSceneTalkingCell, kSceneSlowPhrase})
        for (double at : {0.1, 0.45, 0.8})
        {
            auto e = fresh ();
            tailNeutral (*e);
            sceneOn (*e, which);
            e->setParam (kSceneMode, kModeWalk);
            e->setParam (kSceneSpeed, 0); // Hold
            e->setParam (kScenePosition, at);
            e->reset ();
            run (*e, reese (0.1), nullptr, 256, playing (*e, 140.0));
            const Scene& s = factoryScene (which);
            double worst = 0.0;
            for (int l = 0; l < s.count; ++l)
                worst = std::max (worst, std::fabs (e->laneValue (l) - s.lane[l].curve.at (at * s.length)));
            CHECK (e->scenePlaying () == &s && std::fabs (e->scenePos () - at) < 1e-12 && worst < 1e-6, "%s at %.2f: every lane there (off by %.2g)",
                   factorySceneName (which), at, worst);
        }
    auto e = fresh ();
    tailNeutral (*e);
    sceneOn (*e, kSceneStutterCell);
    e->reset ();
    const std::vector<float> z (Engine::kTick, 0.0f);
    std::vector<float> l (Engine::kTick), r (Engine::kTick);
    bool same = true;
    for (int t = 0; t < 2000; ++t)
    {
        const double beat = t * Engine::kTick / kSr * 140.0 / 60.0;
        e->setTransport (140.0, beat, true);
        e->process (z.data (), z.data (), l.data (), r.data (), Engine::kTick);
        const double end = beat + Engine::kTick / kSr * 140.0 / 60.0;
        same = same && std::fabs (e->scenePos () - gesturePosition (kModeLoop, end, 4.0, 1.0, 0.0)) < 1e-9;
    }
    CHECK (same, "playing: the one clock follows the song (Loop over its 4 beats)");
}

TEST (scene_loop_and_walk_continuous)
{
    // a gesture whose lanes end where they start: across Loop's wrap and Walk's turns every lane moves no more
    // per tick than its steepest slope allows (no jumps), all lanes at once
    auto s = sceneFrom (R"({"name": "Smooth", "length_beats": 2, "lanes": [
        {"target": "Mid Level", "min": -24, "max": 0, "points": [[0, 1], [1, 0], [2, 1]]},
        {"target": "Close", "min": 500, "max": 16000, "points": [[0, 0.2], [0.5, 1], [1.5, 0], [2, 0.2]]},
        {"target": "Wobble Rate", "min": 2, "max": 12, "points": [[0, 0.5], [1, 1], [2, 0.5]]},
        {"target": "Wobble Amount", "points": [[0, 0.6], [2, 0.6]]}]})");
    CHECK (s->count == 4, "4 lanes (%d)", s->count);
    const double steepest = 1.0 / 0.5 * 0.8; // (Close: 0.8 in half a beat)
    for (int mode : {kModeLoop, kModeWalk})
    {
        auto e = fresh ();
        tailNeutral (*e);
        e->setUserScene (s.get ());
        e->setParam (kScene, kSceneUser);
        e->setParam (kSceneMode, mode);
        e->setParam (kSceneSpeed, 6); // (Walk x4: a turn every half beat)
        e->reset ();
        const std::vector<float> z (Engine::kTick, 0.1f);
        std::vector<float> l (Engine::kTick), r (Engine::kTick);
        double prev[4] {-1, -1, -1, -1}, worst = 0.0, lastPos = 0.0;
        int turns = 0, wraps = 0;
        double dir = 0.0;
        const double beatsPerTick = Engine::kTick / kSr * 2.0;
        for (int t = 0; t < 20000; ++t)
        {
            e->setTransport (120.0, t * beatsPerTick, true);
            e->process (z.data (), z.data (), l.data (), r.data (), Engine::kTick);
            const double pos = e->scenePos ();
            if (t > 0)
            {
                const double d = pos - lastPos;
                if (mode == kModeLoop && d < -0.5)
                    ++wraps;
                if (mode == kModeWalk && dir * d < 0.0)
                    ++turns;
                if (std::fabs (d) > 1e-12)
                    dir = d;
            }
            lastPos = pos;
            for (int i = 0; i < 4; ++i)
            {
                if (prev[i] >= 0.0)
                    worst = std::max (worst, std::fabs (e->laneValue (i) - prev[i]));
                prev[i] = e->laneValue (i);
            }
        }
        const double speed = mode == kModeWalk ? 4.0 : 1.0, most = steepest * speed * beatsPerTick;
        std::printf ("    %s: %d wraps, %d turns, largest step of a lane %.5f (%.5f allowed)\n", mode == kModeLoop ? "Loop" : "Walk", wraps, turns, worst, most);
        CHECK ((mode == kModeLoop ? wraps : turns) >= 5, "it wrapped or turned");
        CHECK (worst <= most * 1.01, "every lane continuous");
    }
}

TEST (scene_amount_zero_is_none)
{
    // Gesture None (the default, and an older state) or Amount 0: a new instance's sound, bit for bit, nothing run;
    // a gesture faded in and out again: back to nothing run
    CHECK (std::lround (defaultParams ()[kScene]) == kSceneNone && defaultParams ()[kSceneAmount] == 1.0, "None by default, Amount 100 %%");
    const auto x = reese (2.0);
    auto plainNew = fresh ();
    const auto want = run (*plainNew, x, nullptr, 256, playing (*plainNew, 150.0));
    for (int which : {kSceneReeseCell, kSceneBuzzTail, kSceneCrossoverWalk})
    {
        auto e = fresh ();
        sceneOn (*e, which);
        e->setParam (kSceneAmount, 0.0);
        e->setParam (kSceneMode, kModeWalk);
        e->setParam (kSceneSmooth, 0.4);
        e->reset ();
        const auto got = run (*e, x, nullptr, 256, playing (*e, 150.0));
        CHECK (got == want && !e->gesturesRunning (), "%s at Amount 0: a new instance's sound, bit for bit", factorySceneName (which));
    }
    // a 0.27 project's slots: they still play, the same with Gesture None as beside a gesture at Amount 0
    auto slots = [&] (int which, double amount) {
        auto e = fresh ();
        gesture (*e, 0, kTargetMidLevel, kGestureCellFade);
        gesture (*e, 1, kTargetHighLevel, kGestureSwell, 0.8);
        gesture (*e, 2, kTargetClose, kGestureResonantClose, 0.7);
        e->setParam (kScene, which);
        e->setParam (kSceneAmount, amount);
        e->reset ();
        auto y = run (*e, x, nullptr, 256, playing (*e, 150.0));
        CHECK (e->gesturesRunning () && e->gesturePull (0) == 1.0, "the slots play");
        return y;
    };
    const auto slotsAlone = slots (kSceneNone, 1.0);
    CHECK (slotsAlone != want && slotsAlone == slots (kSceneReeseCell + 1, 0.0), "0.27 slots: the same beside a gesture at Amount 0");
    auto f = fresh ();
    sceneOn (*f, kSceneGateSwap);
    f->reset ();
    run (*f, reese (0.5), nullptr, 256, playing (*f, 150.0));
    CHECK (f->gesturesRunning () && f->scenePull () == 1.0, "on: it runs");
    f->setParam (kSceneAmount, 0.0);
    run (*f, reese (1.0), nullptr, 256, playing (*f, 150.0, 2.0));
    CHECK (!f->gesturesRunning () && f->scenePull () == 0.0, "Amount 0 again: faded out, not run");
    f->setParam (kSceneAmount, 1.0);
    f->setParam (kScene, kSceneNone);
    run (*f, reese (0.3), nullptr, 256, playing (*f, 150.0, 4.0));
    CHECK (!f->gesturesRunning () && f->scenePlaying () == nullptr, "None: not run");
}

TEST (scene_lane_ranges)
{
    // each lane's range in its target's units: a flat lane at its top puts the target there; Amount scales them
    auto s = sceneFrom (R"({"length_beats": 1, "lanes": [
        {"target": "High Level", "min": -48, "max": -24, "points": [[0, 1]]},
        {"target": "Close", "min": 400, "max": 900, "points": [[0, 0]]},
        {"target": "Wobble Rate", "min": 1, "max": 9, "points": [[0, 1]]},
        {"target": "Wobble Amount", "points": [[0, 0.5]]},
        {"target": "Mid X", "min": 1000, "max": 2000, "points": [[0, 0]]},
        {"target": "Shift", "min": 120, "max": 0, "points": [[0, 0]]},
        {"target": "Off", "points": [[0, 0]]}]})");
    CHECK (s->count == 6, "the Off lane left out (%d lanes)", s->count);
    auto render = [&] (double amount) {
        auto e = fresh ();
        tailNeutral (*e);
        e->setParam (kShiftOn, 1.0);
        e->setUserScene (s.get ());
        e->setParam (kScene, kSceneUser);
        e->setParam (kSceneAmount, amount);
        e->reset ();
        run (*e, reese (0.3), nullptr, 256, playing (*e, 120.0));
        return e;
    };
    auto e = render (1.0);
    CHECK (std::fabs (db (e->gestureGain (kBandHigh)) + 24.0) < 1e-6, "High Level at -24 dB (%.3f)", db (e->gestureGain (kBandHigh)));
    CHECK (std::fabs (e->closeHz () - 400.0) < 1e-6, "Close at 400 Hz (%.3f; Tone on at 7 kHz)", e->closeHz ());
    CHECK (std::fabs (e->wobbleRate () - 9.0) < 1e-9 && std::fabs (e->wobbleAmount () - 0.5) < 1e-9, "Wobble at 9 cycles per beat, 50 %%");
    CHECK (std::fabs (e->xoverHz (0, 1) - 1000.0) < 1.0, "Mid X at 1 kHz (%.1f)", e->xoverHz (0, 1));
    // a lane takes Mid X below the control's range (400 Hz), down to a third of an octave above the locked Low X
    {
        auto low = sceneFrom (R"({"length_beats": 1, "lanes": [{"target": "Mid X", "min": 100, "max": 100, "points": [[0, 1]]}]})");
        for (int seed : {1, 40, 90})
        {
            auto f = fresh ();
            f->setParam (kSeed, seed);
            f->setUserScene (low.get ());
            f->setParam (kScene, kSceneUser);
            f->reset ();
            run (*f, reese (0.3), nullptr, 256, playing (*f, 120.0));
            const double floor = f->lowXover () * std::exp2 (1.0 / 3.0), want = std::max (100.0, floor);
            CHECK (std::fabs (f->xoverHz (0, 1) - want) < 0.5 && f->xoverHz (0, 0) == f->lowXover (),
                   "seed %d: Mid X at %.1f Hz (Low X %.1f stays locked)", seed, f->xoverHz (0, 1), f->lowXover ());
        }
    }
    CHECK (std::fabs (e->shiftNow () - 120.0) < 1e-6, "Shift: min above max turns the lane round (%.2f Hz)", e->shiftNow ());
    // Amount 50 %: half way from the controls (High Level at its Level: half way down the dB scale to -24 is -12)
    auto h = render (0.5);
    CHECK (std::fabs (db (h->gestureGain (kBandHigh)) + 12.0) < 1e-6, "Amount 50 %%: High Level at -12 dB (%.3f)", db (h->gestureGain (kBandHigh)));
    CHECK (std::fabs (h->wobbleAmount () - 0.25) < 1e-9, "Amount 50 %%: Wobble Amount half way from 0 (%.3f)", h->wobbleAmount ());
}

TEST (scenes_never_touch_low)
{
    // every factory gesture on a new instance (the Ocean sound: 3 bands at one level, no Movement, no Liquid):
    // clearly heard (the difference from the same render without it at least -20 dB of the whole), while the sub
    // (the Reese's 55 Hz) keeps its level within 0.5 dB, a mono input stays mono (the channels share every gain and
    // coefficient), finite
    const auto x = reese (14.0); // (32 beats at 140 bpm: every gesture played through)
    const size_t a = (size_t)(0.5 * kSr), b = x.size ();
    auto ref = [&] {
        auto e = fresh ();
        e->reset ();
        return run (*e, x, nullptr, 256, playing (*e, 140.0));
    }();
    for (int which = 0; which < kNumFactoryScenes; ++which)
    {
        auto e = fresh ();
        sceneOn (*e, which);
        e->reset ();
        std::vector<float> r;
        const auto y = run (*e, x, &r, 256, playing (*e, 140.0));
        double diff = 0.0, all = 0.0;
        for (size_t i = a; i < b; ++i)
        {
            diff += ((double)y[i] - ref[i]) * ((double)y[i] - ref[i]);
            all += (double)ref[i] * ref[i];
        }
        const double heard = 10.0 * std::log10 (std::max (diff, 1e-30) / all);
        const double sub = db (toneAt (y, 55.0, a, b) / toneAt (ref, 55.0, a, b));
        std::printf ("    %-15s the sub %+.3f dB, the difference %+.1f dB of the whole\n", factorySceneName (which), sub, heard);
        CHECK (std::fabs (sub) < 0.5 && y == r && finite (y), "%s: the sub kept (%.3f dB), mono, finite", factorySceneName (which), sub);
        CHECK (heard > -20.0, "%s: heard on a new instance (the difference %.1f dB)", factorySceneName (which), heard);
    }
}

TEST (scene_change_fades)
{
    // switching the gesture (one to another, to None, to User) fades: no click
    const auto x = bandsMix (3.0);
    auto s = sceneFrom (R"({"length_beats": 1, "lanes": [{"target": "High Level", "points": [[0, 0]]}, {"target": "Mid Level", "points": [[0, 0]]}]})");
    auto e = fresh ();
    tailNeutral (*e);
    e->setUserScene (s.get ());
    sceneOn (*e, kSceneSlowPhrase);
    e->reset ();
    const auto y = run (*e, x, nullptr, 256, [&] (size_t at) {
        e->setTransport (120.0, (double)at / kSr * 2.0, true);
        if (at == 256 * 100)
            e->setParam (kScene, kSceneUser);
        if (at == 256 * 200)
            sceneOn (*e, kSceneTalkingCell);
        if (at == 256 * 300)
            e->setParam (kScene, kSceneNone);
        if (at == 256 * 400)
            e->setParam (kScene, kSceneUser);
    });
    auto f = fresh ();
    tailNeutral (*f);
    const auto ref = run (*f, x, nullptr, 256);
    double jump = 0.0, own = 0.0;
    for (size_t i = 1000; i < y.size (); ++i)
    {
        jump = std::max (jump, (double)std::fabs (y[i] - y[i - 1]));
        own = std::max (own, (double)std::fabs (ref[i] - ref[i - 1]));
    }
    CHECK (finite (y) && jump < 1.5 * own, "no clicks (largest step %.3f, without the gesture %.3f)", jump, own);
}

TEST (scene_files)
{
    // the format of lanes: targets by name, ranges, the length, errors; there and back
    SceneData d;
    std::string err;
    const std::string text = R"({"name": "Two", "length_beats": 4, "lanes": [
        {"target": "high level", "source": "A > B > Chain Volume", "min": -30, "max": 0, "points": [[0, 1], [2, 1], [2, 0], [4, 0.5]]},
        {"target": "Close", "min": 400, "points": [[1, 0.5], [0, 1.5]]},
        {"target": "Off", "source": "unused", "points": [[0, 0]]}]})";
    CHECK (parseSceneJson (text, "file", d, err), "%s", err.c_str ());
    CHECK (d.name == "Two" && d.length == 4.0 && d.lanes.size () == 3 && d.lanes[0].target == "High Level" && d.lanes[0].source == "A > B > Chain Volume",
           "name, length, targets by name, sources");
    CHECK (d.lanes[1].hasMin && !d.lanes[1].hasMax && d.lanes[1].points[0].first == 0.0 && d.lanes[1].points[0].second == 1.0, "sorted, clamped, min only");
    Scene s;
    CHECK (toScene (d, s) && s.count == 2 && s.lane[1].closeHz && s.lane[1].hzLo == 400.0 && s.lane[1].hzHi == kCloseOpenHz, "two lanes play (Off left out)");
    CHECK (s.lane[0].curve.at (2.0) == 0.0 && std::fabs (s.lane[0].lo - (1.0 - 30.0 / 48.0)) < 1e-12 && s.lane[0].hi == 1.0, "a jump, the range");
    SceneData back;
    CHECK (parseSceneJson (sceneJson (d), "x", back, err) && back.name == d.name && back.length == d.length && back.lanes.size () == 3 &&
               back.lanes[0].points == d.lanes[0].points && back.lanes[0].min == -30.0 && back.lanes[1].hasMax == false && back.lanes[2].source == "unused",
           "round trip");
    CHECK (parseSceneJson (R"({"lanes": [{"target": "Dirt", "points": [[0, 0], [3, 1]]}]})", "Named", d, err) && d.name == "Named" && d.length == 3.0,
           "no name, no length: the file's name, the last point");
    CHECK (parseSceneJson (R"({"target": "Bells", "points": [[0, 0], [1, 1]]})", "x", d, err) && d.lanes.size () == 1, "one lane at the top level");
    CHECK (!parseSceneJson (R"({"points": [[0, 0], [1, 1]]})", "x", d, err), "a 0.27 single curve: %s", err.c_str ());
    CHECK (!parseSceneJson (R"({"lanes": [{"target": "Low Level", "points": [[0, 0]]}]})", "x", d, err), "the Low band is no target: %s", err.c_str ());
    CHECK (!parseSceneJson (R"({"lanes": [{"target": "Dirt", "points": []}]})", "x", d, err), "no points: %s", err.c_str ());
    CHECK (!parseSceneJson (R"({"lanes": []})", "x", d, err), "no lanes: %s", err.c_str ());
    std::string many = R"({"lanes": [)";
    for (int i = 0; i <= kMaxSceneLanes; ++i)
        many += std::string (i ? "," : "") + R"({"target": "Dirt", "points": [[0, 0]]})";
    CHECK (!parseSceneJson (many + "]}", "x", d, err), "too many lanes: %s", err.c_str ());
}

// ---- the LAB (0.30)

namespace {
// a LAB slot holding `kind` (On or off), with some of the kind's own parameters (its IDs, plain values) set
void labSlot (Engine& e, int slot, int kind, bool on = true, std::initializer_list<std::pair<uint32_t, double>> values = {})
{
    e.setParam (labSlotParam (slot, kLabType), kind);
    e.setParam (labSlotParam (slot, kLabOn), on ? 1.0 : 0.0);
    const auto& t = smemplr::fxBlockTable (kind);
    for (const auto& [id, v] : values)
    {
        const int64_t j = smemplr::fxBlockOf (kind, id);
        if (j >= 0)
            e.setParam (labBlockParam (slot, (uint32_t)j), t.toNormalized ((uint32_t)j, v));
    }
}
// every chain on the bands with a driven smacheratr and an OTT, and an OTT in POST (a dirty, dense LAB)
void dirtyLab (Engine& e, double driveDb = 18.0)
{
    for (int c = 0; c < kNumBandChains; ++c)
    {
        labSlot (e, chainSlot (c, 0), smemplr::kFxSmacheratr, true, {{smacheratr::kDrive, driveDb}, {smacheratr::kPostClip, 1.0}});
        labSlot (e, chainSlot (c, 1), smemplr::kFxMultidyn, true, {{multidyn::kAmount, 0.7}});
    }
    labSlot (e, postSlot (0), smemplr::kFxMultidyn, true, {{multidyn::kAmount, 0.4}});
}
// the engine linear: no SWEEP stage, Drive, Glue, Grit or movement, the end saturator out of the way
std::unique_ptr<Engine> linearEngine (int bands = 4)
{
    auto e = engine ();
    plain (*e);
    e->setParam (kSweep, 0.0);
    e->setParam (kBandCount, bands == 4 ? kBands4 : kBands3);
    e->setParam (kXoverMid, 1200.0);
    e->setParam (kXoverHigh, 6000.0);
    tailNeutral (*e);
    return e;
}
} // namespace

TEST (lab_empty_is_029)
{
    // every slot Empty and every chain at 0 dB (Init, and a state saved before 0.30, which reads the LAB's parameters at
    // these: defaultNormalizedForVersion): the LAB does not run, the latency is the end saturator's alone, and the sound
    // is the engine's without the LAB, bit for bit, also where the LAB had been set up and taken back (the check against
    // 0.29 itself, a build of each rendering every factory preset and gesture, bit for bit, is run by hand)
    const auto x = reese (1.5);
    for (int which = 0; which < 2; ++which)
    {
        auto a = fresh ();
        auto b = fresh ();
        for (auto* e : {a.get (), b.get ()})
        {
            e->setParam (kBandCount, which ? kBands4 : kBands3);
            e->setParam (kMovement, 0.6);
        }
        dirtyLab (*b);
        b->setParam (chainId (1, kChainLevel), -6.0);
        b->setParam (chainId (2, kChainMono), 1.0);
        b->setParam (chainId (0, kChainSolo), 1.0);
        b->reset ();
        CHECK (b->labStage ().active () && b->labLatency () > 0, "set up: running, with a latency");
        // (a 0.29 state: every LAB parameter at what such a state reads)
        for (uint32_t id = kChainBase; id < kNumParams; ++id)
            b->setParam (id, toPlain (id, defaultNormalizedForVersion (id, 7)));
        a->reset ();
        b->reset ();
        CHECK (!b->labStage ().active () && b->labLatency () == 0 && b->latency () == a->latency (), "taken back: not running, no latency of its own");
        std::vector<float> ar, br;
        const auto al = run (*a, x, &ar), bl = run (*b, x, &br);
        CHECK (al == bl && ar == br, "%d bands: the sound without the LAB, bit for bit", which ? 4 : 3);
    }
}

TEST (lab_paths_line_up)
{
    // the chains and POST with their effects switched off (smacheratr fully dry, multidyn bypassed: their latencies
    // only): every path lined up, the bands sum to the same all-pass as without the LAB, just later by its latency
    for (int bands : {3, 4})
    {
        auto ref = linearEngine (bands);
        auto e = linearEngine (bands);
        labSlot (*e, chainSlot (0, 0), smemplr::kFxSmacheratr, false);
        labSlot (*e, chainSlot (0, 1), smemplr::kFxMultidyn, false);
        labSlot (*e, chainSlot (1, 0), smemplr::kFxSmacheratr, false); // (a shorter chain)
        labSlot (*e, postSlot (0), smemplr::kFxMultidyn, false);      // (chain 3 empty)
        e->reset ();
        const Lab& lab = e->labStage ();
        const int labLat = e->labLatency ();
        CHECK (lab.chainLatency (0) > lab.chainLatency (1) && lab.chainLatency (2) == 0 && labLat == Lab::kChunk + lab.chainLatency (0) + lab.postLatency () &&
                   e->latency () == labLat + ref->latency (),
               "the latency: the effects' block, the slowest chain's (%d) and POST's (%d), then the end saturator's (%d)", lab.chainLatency (0),
               lab.postLatency (),
               e->latency () - labLat);
        std::vector<float> x (32768, 0.0f);
        x[0] = 0.5f;
        const auto y0 = run (*ref, x), y = run (*e, x);
        double worst = 0.0, lo = 1e9, hi = -1e9;
        for (size_t i = 0; i + (size_t)labLat < y.size (); ++i)
            worst = std::max (worst, std::fabs ((double)y[i + (size_t)labLat] - y0[i]));
        for (int i = 0; i <= 80; ++i)
        {
            const double f = 20.0 * std::pow (1000.0, i / 80.0);
            const double r = responseDb (y, (size_t)e->latency (), f) - db (0.5);
            lo = std::min (lo, r);
            hi = std::max (hi, r);
        }
        std::printf ("    %d bands: latency %d, %.3f .. %.3f dB, off the LAB-less sum by %.1e\n", bands, labLat, lo, hi, worst);
        CHECK (lo > -0.1 && hi < 0.1, "%d bands: flat within 0.1 dB (%.3f .. %.3f)", bands, lo, hi);
        CHECK (worst < 1e-5, "%d bands: the same sum, %d samples later (%.1e)", bands, labLat, worst);
    }
    // Mix at 0: the dry signal, delayed by the whole latency (so dry and wet line up at every Mix)
    auto e = linearEngine ();
    dirtyLab (*e);
    e->setParam (kMix, 0.0);
    e->reset ();
    std::vector<float> x (8192, 0.0f);
    x[100] = 0.5f;
    const auto y = run (*e, x);
    const size_t at = (size_t)(std::max_element (y.begin (), y.end (), [] (float a, float b) { return std::fabs (a) < std::fabs (b); }) - y.begin ());
    CHECK (at == 100 + (size_t)e->latency () && std::fabs (y[at] - 0.5f) < 1e-6, "Mix 0: the dry impulse at %zu (latency %d)", at, e->latency ());
}

TEST (lab_mono_and_sub)
{
    // a dirty LAB on a mono Reese: the output stays mono, and the sub (the Low band: never in a chain) as it is
    // without the LAB, lined up
    const auto x = reese (3.0);
    auto ref = fresh ();
    auto e = fresh ();
    for (auto* en : {ref.get (), e.get ()})
    {
        en->setParam (kBandCount, kBands4);
        en->setParam (kMovement, 0.5);
        tailNeutral (*en); // (the end saturator after it all would take the sub down as the rest gets louder)
    }
    dirtyLab (*e, 24.0);
    ref->reset ();
    e->reset ();
    std::vector<float> rr, er;
    const auto rl = run (*ref, x, &rr), el = run (*e, x, &er);
    double side = 0.0;
    for (size_t i = 0; i < el.size (); ++i)
        side = std::max (side, (double)std::fabs (el[i] - er[i]));
    CHECK (finite (el) && side == 0.0, "mono in, mono out (L - R up to %.1e)", side);
    const size_t lat = (size_t)e->labLatency (), a = (size_t)(1.0 * kSr), b = x.size () - lat;
    std::vector<float> shifted (el.begin () + (long)lat, el.end ());
    const double subRef = toneAt (rl, 55.0, a, b), subLab = toneAt (shifted, 55.0, a, b);
    std::printf ("    the 55 Hz fundamental: %.2f dB without the LAB, %.2f dB with it\n", db (subRef), db (subLab));
    CHECK (std::fabs (db (subLab) - db (subRef)) < 1.0, "the sub within 1 dB (%.2f dB)", db (subLab) - db (subRef));
    // and something happened above it
    const double midRef = rms (rl, a, b), midLab = rms (shifted, a, b);
    CHECK (std::fabs (db (midLab) - db (midRef)) < 12.0 && peak (el, 0, el.size ()) < 4.0, "level in reason (%.1f dB against %.1f)", db (midLab),
           db (midRef));
}

TEST (lab_mute_solo_level)
{
    // Mute: the chain fades out and stops running (its settings no longer matter); Solo: only the soloed chain (the Low
    // band silent too); Level: after the chain's effects; Mono: the chain in the middle
    const auto x = reese (1.5);
    auto render = [&] (const std::function<void (Engine&)>& set, std::vector<float>* right = nullptr) {
        auto e = fresh ();
        e->setParam (kBandCount, kBands4);
        dirtyLab (*e);
        set (*e);
        tailNeutral (*e);
        return run (*e, x, right);
    };
    const size_t a = (size_t)(0.6 * kSr), b = x.size ();
    const auto all = render ([] (Engine&) {});
    const auto muted = render ([] (Engine& e) { e.setParam (chainId (0, kChainMute), 1.0); });
    const auto mutedHot = render ([] (Engine& e) {
        e.setParam (chainId (0, kChainMute), 1.0);
        labSlot (e, chainSlot (0, 0), smemplr::kFxSmacheratr, true, {{smacheratr::kDrive, 36.0}});
    });
    CHECK (muted == mutedHot, "a muted chain is not run: its Drive does not matter");
    CHECK (db (rms (muted, a, b)) < db (rms (all, a, b)), "muting the Mid chain takes something away");
    const auto soloed = render ([] (Engine& e) { e.setParam (chainId (2, kChainSolo), 1.0); });
    const auto airOnly = render ([] (Engine& e) {
        e.setParam (chainId (0, kChainMute), 1.0);
        e.setParam (chainId (1, kChainMute), 1.0);
        e.setParam (kLowLevel, kLevelOffDb);
    });
    double worst = 0.0;
    for (size_t i = a; i < b; ++i)
        worst = std::max (worst, (double)std::fabs (soloed[i] - airOnly[i]));
    std::printf ("    Solo Air against Mid and High muted and Low off: %.1e\n", worst);
    CHECK (worst < 1e-3, "Solo: the soloed chain alone (%.1e)", worst);
    // (without POST: its OTT after the chains would not take a level change as it is)
    const auto quieter = render ([] (Engine& e) {
        for (int c = 0; c < kNumBandChains; ++c)
            e.setParam (chainId (c, kChainLevel), -6.0);
        e.setParam (kLowLevel, kLevelOffDb);
        e.setParam (labSlotParam (postSlot (0), kLabType), 0.0);
    });
    const auto lowOff = render ([] (Engine& e) {
        e.setParam (kLowLevel, kLevelOffDb);
        e.setParam (labSlotParam (postSlot (0), kLabType), 0.0);
    });
    CHECK (std::fabs (db (rms (quieter, a, b)) - db (rms (lowOff, a, b)) + 6.0) < 0.2, "Level -6 dB on every chain: 6 dB down after the effects (%.2f)",
           db (rms (quieter, a, b)) - db (rms (lowOff, a, b)));
    std::vector<float> wideR;
    auto stereo = [&] (bool mono) {
        auto e = fresh ();
        e->setParam (kBandCount, kBands4);
        e->setParam (kLowLevel, kLevelOffDb);
        dirtyLab (*e);
        for (int c = 0; c < kNumBandChains; ++c)
            e->setParam (chainId (c, kChainMono), mono ? 1.0 : 0.0);
        e->reset ();
        std::vector<float> l (x.size ()), r (x.size ()), xr (x.size ());
        for (size_t i = 0; i < x.size (); ++i)
            xr[i] = i >= 40 ? x[i - 40] : 0.0f; // (a wide input)
        for (size_t s = 0; s < x.size (); s += 256)
        {
            const int m = (int)std::min<size_t> (256, x.size () - s);
            e->process (x.data () + s, xr.data () + s, l.data () + s, r.data () + s, m);
        }
        double d = 0.0;
        for (size_t i = a; i < b; ++i)
            d = std::max (d, (double)std::fabs (l[i] - r[i]));
        return d;
    };
    const double wide = stereo (false), narrow = stereo (true);
    CHECK (wide > 1e-3 && narrow < 1e-6, "Mono: the chains in the middle (L - R %.1e, wide %.1e)", narrow, wide);
}

namespace {
// a factory preset's values (Init plus its lines), by ID
std::vector<std::pair<uint32_t, double>> presetValues (const char* file)
{
    std::ifstream in (std::filesystem::path (MOISTR_PRESETS_DIR) / file);
    std::stringstream ss;
    ss << in.rdbuf ();
    pk::presets::FactoryPreset fp;
    std::string err;
    const bool ok = pk::presets::parseFactoryPreset (ss.str (), file, paramTable (), fp, err);
    CHECK (ok, "%s parses: %s", file, err.c_str ());
    return {fp.values.begin (), fp.values.end ()};
}
// an engine with the defaults and these values (normalized, by ID)
std::unique_ptr<Engine> withValues (const std::vector<std::pair<uint32_t, double>>& values)
{
    auto e = fresh ();
    for (const auto& [id, n] : values)
        e->setParam (id, toPlain (id, n));
    e->reset ();
    return e;
}
} // namespace

TEST (neuro_recipe)
{
    // a new instance's recipe (newInstanceValues) is the factory preset Neuro/Neuro, value for value
    std::array<double, kNumParams> fromPreset {}, fromRecipe {};
    for (uint32_t id = 0; id < kNumParams; ++id)
        fromPreset[id] = fromRecipe[id] = defaultNormalized (id);
    for (const auto& [id, n] : presetValues ("Neuro/Neuro.txt"))
        fromPreset[id] = n;
    for (const auto& [id, n] : newInstanceValues ())
        fromRecipe[id] = n;
    int differ = 0;
    for (uint32_t id = 0; id < kNumParams; ++id)
        differ += std::fabs (fromPreset[id] - fromRecipe[id]) > 1e-7;
    CHECK (differ == 0, "Neuro/Neuro is the new instance's recipe (%d values differ)", differ);
    // and it has the LAB on: smacheratr and multidyn in the chains, multidyn and smacheratr in POST, nothing in chain 4
    auto e = withValues (newInstanceValues ());
    const Lab& lab = e->labStage ();
    CHECK (lab.active () && lab.kind (chainSlot (0, 0)) == smemplr::kFxSmacheratr && lab.kind (chainSlot (2, 1)) == smemplr::kFxMultidyn &&
               lab.kind (postSlot (0)) == smemplr::kFxMultidyn && lab.kind (postSlot (1)) == smemplr::kFxSmacheratr &&
               lab.kind (chainSlot (3, 0)) == smemplr::kFxEmpty,
           "the LAB: smacheratr and an OTT on each band, an OTT and a clipper in POST");
    std::printf ("    latency: the LAB's %d (chains %d, %d, %d; POST %d) and the end saturator's %d\n", e->labLatency (), lab.chainLatency (0),
                 lab.chainLatency (1), lab.chainLatency (2), lab.postLatency (), e->latency () - e->labLatency ());
}

TEST (neuro_presets)
{
    // every LAB preset on a mono Reese at 140 BPM: finite, mono, and the sub (55 Hz, the Low band's) within 1 dB of the
    // same settings with the LAB empty (the end saturator out of the way: it takes everything down as the rest gets
    // louder), lined up by the LAB's latency
    const auto x = reese (4.0);
    const size_t a = (size_t)(1.0 * kSr);
    for (const char* file : {"Neuro/Neuro.txt", "Neuro/Neuro Heavy.txt", "Neuro/Dirty Mids.txt", "Neuro/Neuro Gesture.txt"})
    {
        const auto values = presetValues (file);
        auto e = withValues (values);
        auto bare = withValues (values);
        tailNeutral (*e);
        tailNeutral (*bare);
        for (int sl = 0; sl < kNumLabSlots; ++sl)
            bare->setParam (labSlotParam (sl, kLabType), 0.0);
        bare->reset ();
        auto at = [&] (Engine& en) { return [&en] (size_t s) { en.setTransport (140.0, (double)s / kSr * 140.0 / 60.0, true); }; };
        std::vector<float> r, br;
        const auto l = run (*e, x, &r, 256, at (*e)), bl = run (*bare, x, &br, 256, at (*bare));
        double side = 0.0;
        for (size_t i = 0; i < l.size (); ++i)
            side = std::max (side, (double)std::fabs (l[i] - r[i]));
        const size_t lat = (size_t)e->labLatency ();
        const std::vector<float> lined (l.begin () + (long)lat, l.end ());
        const double sub = db (toneAt (lined, 55.0, a, lined.size ())), subBare = db (toneAt (bl, 55.0, a, lined.size ()));
        std::printf ("    %-24s 55 Hz %.2f dB (LAB empty %.2f), peak %.2f\n", file, sub, subBare, peak (l, 0, l.size ()));
        CHECK (finite (l) && side == 0.0, "%s: finite, mono in mono out (%.1e)", file, side);
        CHECK (std::fabs (sub - subBare) < 1.0, "%s: the sub within 1 dB (%.2f dB)", file, sub - subBare);
    }
}

TEST (lab_gesture_targets)
{
    // the LAB's targets (0.30, lanes only): Mid / High / Air Grit move the chain's first smacheratr's Drive, the OTT targets
    // its first multidyn's Amount (Post OTT: POST's), from the slot's own setting; let go, the slot's own setting again.
    // A chain without such a slot: nothing. The 0.27 slots' Target choice keeps its 14 entries.
    CHECK ((int)paramTable ().info (gestureId (0, kGestureTarget)).choices.size () == kNumSlotTargets && kNumSlotTargets == 14,
           "the slots' Target: the 0.27 targets only");
    SceneData d;
    std::string err;
    const bool parsed = parseSceneJson (R"({"name": "Grit", "length_beats": 4, "lanes": [
        {"target": "Mid Grit", "min": 0, "max": 36, "points": [[0, 0], [4, 1]]},
        {"target": "High OTT", "points": [[0, 1], [4, 0]]},
        {"target": "Air Grit", "points": [[0, 1], [4, 1]]},
        {"target": "Post OTT", "points": [[0, 0.25], [4, 0.25]]}]})",
                                        "Grit", d, err);
    CHECK (parsed, "a file with the LAB's targets: %s", err.c_str ());
    auto scene = std::make_unique<Scene> ();
    CHECK (toScene (d, *scene) && scene->count == 4 && scene->lane[0].target == kTargetMidGrit && scene->lane[3].target == kTargetPostOtt,
           "four lanes, by name");
    const double lo = targetNorm (kTargetMidGrit, 0.0), hi = targetNorm (kTargetMidGrit, 36.0);
    CHECK (std::fabs (lo - 0.5) < 1e-12 && std::fabs (hi - 1.0) < 1e-12, "Grit in dB of Drive (0 dB: halfway)");
    auto e = withValues (newInstanceValues ());
    e->setParam (labSlotParam (chainSlot (2, 0), kLabType), 0.0); // (no smacheratr on Air: its lane does nothing)
    e->setUserScene (scene.get ());
    e->setParam (kScene, kSceneUser);
    e->reset ();
    const auto x = reese (2.0);
    double atOne = -1.0, atThree = -1.0;
    run (*e, x, nullptr, 256, [&] (size_t a) {
        e->setTransport (120.0, (double)a / kSr * 2.0, true);
        if (a == 256 * 94) // (1 beat at 120 BPM: 24000 samples)
            atOne = e->labStage ().pulledTo (0, false);
        if (a == 256 * 281)
            atThree = e->labStage ().pulledTo (0, false);
    });
    const Lab& lab = e->labStage ();
    std::printf ("    Mid Grit at beat 1: %.3f, at beat 3: %.3f (its own: %.3f); High OTT %.3f; Post OTT %.3f\n", atOne, atThree, lab.ownValue (0, false),
                 lab.pulledTo (1, true), lab.pulledTo (3, true));
    CHECK (atOne > lo && atThree > atOne && atThree < hi, "Mid Grit rises with its lane");
    CHECK (lab.pulledTo (2, false) < 0.0, "Air Grit without a smacheratr on Air: nothing");
    CHECK (std::fabs (lab.pulledTo (3, true) - 0.25) < 0.01 && lab.pulledTo (1, true) >= 0.0, "the OTT targets pull");
    // let go: the slots' own settings again
    e->setParam (kScene, kSceneNone);
    std::vector<float> rest (8192, 0.0f);
    run (*e, rest);
    CHECK (lab.pulledTo (0, false) < 0.0 && lab.pulledTo (3, true) < 0.0, "Gesture None: every pull let go");
}

TEST (cpu_budget)
{
    // 10 s of a stereo Reese, the defaults, the heaviest settings and a new instance (the Neuro recipe, under 18 %) (4 bands, 2 passes, full movement at the
    // fastest Rate with the longest ramps, the end saturator on): CPU time, the best of three renders
    const auto x = reese (10.0);
    for (int which = 0; which < 5; ++which)
    {
        const bool heavy = which == 1, ocean = which == 2 || which == 3, gestures = which == 3, neuro = which == 4;
        double secs = 1e9;
        std::vector<float> l;
        for (int i = 0; i < 3; ++i)
        {
            auto e = ocean ? fresh () : neuro ? withValues (newInstanceValues ()) : engine ();
            if (ocean)
            {
                // a new instance (eight bells, Sub Boost, Tone, the end saturator) with Clean Sub on as well
                e->setParam (kCleanSub, 1.0);
                e->setParam (kSplitDrive, 6.0);
                e->setParam (kShelf, 1.0);
            }
            if (gestures)
            {
                // and every gesture path: a level gate, Close, Wobble on a rate gesture, Dirt (the taps' second split)
                gesture (*e, 0, kTargetHighLevel, kGestureStutter16);
                gesture (*e, 1, kTargetClose, kGestureResonantClose);
                gesture (*e, 2, kTargetWobbleRate, kGestureRateRise);
                gesture (*e, 3, kTargetDirt, kGestureCrossfade);
                e->setParam (kWobbleAmount, 0.8);
                // and the one gesture's six lanes on top
                sceneOn (*e, kSceneReeseCell);
            }
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
                // and 0.23's: the bands linked, Liquid's two resonances in both passes
                e->setParam (kLink, 1.0);
                e->setParam (kLiquid, 1.0);
                e->setParam (kLiquidRes, 0.8);
            }
            e->reset ();
            const pk::testing::CpuClock t0 = pk::testing::cpuClock ();
            l = run (*e, x, nullptr, 512);
            secs = std::min (secs, (double)(pk::testing::cpuClock () - t0) / pk::testing::kCpuClocksPerSec);
        }
        CHECK (finite (l), "finite");
        std::printf ("    CPU: %.2f%% of one core (%s)\n", 100.0 * secs / 10.0,
                     neuro ? "a new instance (the Neuro recipe: three chains of smacheratr and an OTT, an OTT and a clipper in POST)"
                     : heavy ? "4 bands, 2 passes, full movement, Seed Blend, Density x8, Speed x16, Low Push / Dip, Shift on, Link, Liquid, the end saturator on"
                     : gestures ? "the new instance as above, with four slots (a level gate, Close, Wobble on a rate gesture, Dirt) and Reese Cell"
                     : ocean ? "a new instance's eight bells, Sub Boost and Tone, plus Clean Sub, Split Drive and the High Shelf"
                             : "the bands alone (Sweep off)");
        CHECK (secs / 10.0 < (neuro ? 0.18 : heavy ? 0.15 : gestures ? 0.10 : 0.08), "too slow");
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
