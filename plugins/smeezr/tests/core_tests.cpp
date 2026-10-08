// Headless tests for the Smeezr DSP. Run: ./smeezr_tests [filter]
// Squeeze 0 passes bit for bit, the split sums flat, the pink stage moves white, brown and bright signals
// towards pink and leaves pink noise alone (loudness kept, near-silent bands not boosted), the OTT stage
// lifts quiet parts and pulls peaks down, the knob is continuous through 50 % and fast automation is
// clean, Mix, Output and Speed, and the CPU budget.
#include "Dsp.h"
#include "Engine.h"
#include "Params.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <functional>
#include <limits>
#include <memory>
#include <string>
#include <vector>

using namespace smeezr;

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

struct Rng
{
    uint32_t s;
    explicit Rng (uint32_t seed) : s (seed) {}
    double next () // -1 .. 1
    {
        s = s * 1664525u + 1013904223u;
        return (int32_t)s / 2147483648.0;
    }
};
void normalize (std::vector<float>& x, double rmsTarget)
{
    const double r = rms (x, 0, x.size ());
    for (auto& v : x)
        v = (float)(v * rmsTarget / r);
}
std::vector<float> white (double seconds, double level = 0.1, uint32_t seed = 3)
{
    Rng r (seed);
    std::vector<float> x ((size_t)(seconds * kSr));
    for (auto& v : x)
        v = (float)r.next ();
    normalize (x, level);
    return x;
}
// pink noise: Paul Kellet's filter on white noise (within 0.05 dB of -3 dB/oct above about 10 Hz)
std::vector<float> pinkNoise (double seconds, double level = 0.1, uint32_t seed = 5)
{
    Rng r (seed);
    std::vector<float> x ((size_t)(seconds * kSr));
    double b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0;
    for (auto& v : x)
    {
        const double w = r.next ();
        b0 = 0.99886 * b0 + w * 0.0555179;
        b1 = 0.99332 * b1 + w * 0.0750759;
        b2 = 0.96900 * b2 + w * 0.1538520;
        b3 = 0.86650 * b3 + w * 0.3104856;
        b4 = 0.55000 * b4 + w * 0.5329522;
        b5 = -0.7616 * b5 - w * 0.0168980;
        v = (float)(b0 + b1 + b2 + b3 + b4 + b5 + b6 + w * 0.5362);
        b6 = w * 0.115926;
    }
    normalize (x, level);
    return x;
}
// brown (red) noise: white noise integrated (leaking below 5 Hz), -6 dB/oct
std::vector<float> brown (double seconds, double level = 0.1, uint32_t seed = 7)
{
    Rng r (seed);
    std::vector<float> x ((size_t)(seconds * kSr));
    double y = 0.0;
    const double leak = std::exp (-2.0 * kPi * 5.0 / kSr);
    for (auto& v : x)
    {
        y = leak * y + 0.05 * r.next ();
        v = (float)y;
    }
    normalize (x, level);
    return x;
}
// a bright synth: a chord of narrow pulse waves (10 % duty), its harmonics nearly flat up into the highs
std::vector<float> brightSynth (double seconds, double level = 0.1)
{
    std::vector<float> x ((size_t)(seconds * kSr));
    const double f[4] = {110.0, 138.6, 164.8, 220.6};
    double ph[4] = {0.0, 0.25, 0.5, 0.75};
    for (auto& v : x)
    {
        double s = 0.0;
        for (int k = 0; k < 4; ++k)
        {
            ph[k] += f[k] / kSr;
            ph[k] -= std::floor (ph[k]);
            s += (ph[k] < 0.1 ? 0.9 : -0.1);
        }
        v = (float)s;
    }
    normalize (x, level);
    return x;
}

// ---- spectrum ------------------------------------------------------------------------------------------
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
        const double ang = -2.0 * kPi / (double)len;
        const std::complex<double> wl (std::cos (ang), std::sin (ang));
        for (size_t i = 0; i < n; i += len)
        {
            std::complex<double> w (1.0);
            for (size_t k = 0; k < len / 2; ++k)
            {
                const auto u = a[i + k], v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
                w *= wl;
            }
        }
    }
}
// the power in each octave band (dB) of x[a, b): Welch, Hann, 16384 points. The ten octaves of 20 Hz ..
// 20 kHz: 20 .. 40 Hz, 40 .. 80 Hz and so on up to 10 .. 20 kHz.
constexpr int kOctaves = 10;
double octLo (int o) { return 20.0 * std::pow (1000.0, o / 10.0); }
std::vector<double> octaves (const std::vector<float>& x, size_t a, size_t b)
{
    constexpr size_t N = 16384;
    std::vector<double> power (N / 2, 0.0);
    std::vector<std::complex<double>> buf (N);
    int frames = 0;
    for (size_t s = a; s + N <= b; s += N / 2, ++frames)
    {
        for (size_t i = 0; i < N; ++i)
            buf[i] = x[s + i] * (0.5 - 0.5 * std::cos (2.0 * kPi * (double)i / N));
        fft (buf);
        for (size_t i = 0; i < N / 2; ++i)
            power[i] += std::norm (buf[i]);
    }
    std::vector<double> out (kOctaves, 0.0);
    for (int o = 0; o < kOctaves; ++o)
    {
        double s = 0.0;
        for (size_t i = 1; i < N / 2; ++i)
        {
            const double f = (double)i * kSr / N;
            if (f >= octLo (o) && f < octLo (o + 1))
                s += power[i];
        }
        out[(size_t)o] = 10.0 * std::log10 (s / std::max (1, frames) + 1e-30);
    }
    return out;
}
// the octave spectrum's slope (dB per octave, least squares) over octaves [o0, o1]: 0 for pink noise
double slope (const std::vector<double>& oct, int o0, int o1)
{
    double sx = 0, sy = 0, sxx = 0, sxy = 0;
    const int n = o1 - o0 + 1;
    for (int o = o0; o <= o1; ++o)
    {
        sx += o;
        sy += oct[(size_t)o];
        sxx += (double)o * o;
        sxy += o * oct[(size_t)o];
    }
    return (n * sxy - sx * sy) / (n * sxx - sx * sx);
}
// how far the octaves are from a flat (pink) line through their mean (RMS, dB)
double spread (const std::vector<double>& oct, int o0, int o1)
{
    double m = 0;
    for (int o = o0; o <= o1; ++o)
        m += oct[(size_t)o];
    m /= (o1 - o0 + 1);
    double s = 0;
    for (int o = o0; o <= o1; ++o)
        s += (oct[(size_t)o] - m) * (oct[(size_t)o] - m);
    return std::sqrt (s / (o1 - o0 + 1));
}

// The defaults at `squeeze`, with the end saturator (on by default) off: the tests are about Smeezr's own
// processing (the saturator's switch it on).
std::unique_ptr<Engine> engine (double squeeze, int block = 512)
{
    auto e = std::make_unique<Engine> ();
    e->setParam (kTailBase + pk::kTailOn, 0.0);
    e->prepare (kSr, block);
    e->setParam (kSqueeze, squeeze);
    e->reset ();
    return e;
}

// renders x on both channels (right: xr, or x) in blocks, calling `at (sample)` before each
std::vector<float> run (Engine& e, const std::vector<float>& x, std::vector<float>* right = nullptr, int block = 256,
                        const std::function<void (size_t)>& at = {}, const std::vector<float>* xr = nullptr)
{
    std::vector<float> l (x.size ()), r (x.size ());
    for (size_t a = 0; a < x.size (); a += (size_t)block)
    {
        if (at)
            at (a);
        const int m = (int)std::min ((size_t)block, x.size () - a);
        e.process (x.data () + a, (xr ? xr->data () : x.data ()) + a, l.data () + a, r.data () + a, m);
    }
    if (right)
        *right = r;
    return l;
}

// the output with the latency taken out (the same length as the input)
std::vector<float> aligned (Engine& e, const std::vector<float>& y)
{
    const size_t lat = (size_t)e.latency ();
    std::vector<float> o (y.size (), 0.0f);
    for (size_t i = lat; i < y.size (); ++i)
        o[i - lat] = y[i];
    return o;
}

} // namespace

TEST (knob_mapping)
{
    CHECK (Engine::pinkAmount (0.0) == 0.0 && Engine::ottDepth (0.0) == 0.0, "0 %%: nothing");
    CHECK (Engine::pinkAmount (0.5) == 1.0 && Engine::ottDepth (0.5) == 0.0, "50 %%: full pink, no OTT");
    CHECK (Engine::pinkAmount (1.0) == 1.0 && Engine::ottDepth (1.0) == 1.0, "100 %%: full pink and full OTT");
    // continuous and smooth through 50 %: both slopes nearly 0 on either side
    const double h = 1e-4;
    const double dp = (Engine::pinkAmount (0.5) - Engine::pinkAmount (0.5 - h)) / h;
    const double dt = (Engine::ottDepth (0.5 + h) - Engine::ottDepth (0.5)) / h;
    CHECK (dp < 0.01 && dt < 0.01, "flat at 50 %%: %.4f, %.4f", dp, dt);
    double lastP = -1, lastT = -1;
    bool mono = true;
    for (int i = 0; i <= 1000; ++i)
    {
        const double u = i / 1000.0, pa = Engine::pinkAmount (u), td = Engine::ottDepth (u);
        mono = mono && pa >= lastP && td >= lastT;
        lastP = pa;
        lastT = td;
    }
    CHECK (mono, "both rise with the knob");
    // the crossovers: one octave each (log-spaced 20 Hz .. 20 kHz), the groups at about 80 Hz and 2.5 kHz
    CHECK (std::fabs (Engine::crossover (0) - 39.9) < 0.1 && std::fabs (Engine::crossover (kXovers - 1) - 10024.0) < 1.0,
           "crossovers %.1f .. %.0f Hz", Engine::crossover (0), Engine::crossover (kXovers - 1));
    CHECK (Engine::group (1) == 0 && Engine::group (2) == 1 && Engine::group (7) == 1 && Engine::group (8) == 2,
           "OTT groups: below 80 Hz, to 2.5 kHz, above");
}

TEST (passthrough_at_zero)
{
    // Squeeze 0: the output is the input delayed by the latency (the end saturator's), bit for bit, at any
    // block size and with different left and right
    const auto xl = white (1.0, 0.3, 11), xr = pinkNoise (1.0, 0.2, 12);
    for (int block : {1, 17, 64, 480, 512})
    {
        auto e = engine (0.0, 512);
        std::vector<float> yr;
        const auto yl = run (*e, xl, &yr, block, {}, &xr);
        const auto al = aligned (*e, yl), ar = aligned (*e, yr);
        size_t diff = 0;
        for (size_t i = 0; i + (size_t)e->latency () < xl.size (); ++i)
            diff += (al[i] != xl[i]) + (ar[i] != xr[i]);
        CHECK (diff == 0, "block %d: %zu samples differ (latency %d)", block, diff, e->latency ());
    }
    // turned up and back to 0: bit for bit again once the knob has glided there
    {
        auto e = engine (0.8);
        std::vector<float> yr;
        const auto x = white (2.0, 0.2, 13);
        const auto y = run (*e, x, &yr, 256, [&] (size_t a) { e->setParam (kSqueeze, a < 24000 ? 0.8 : 0.0); });
        const auto al = aligned (*e, y);
        size_t diff = 0;
        for (size_t i = 48000; i + (size_t)e->latency () < x.size (); ++i)
            diff += al[i] != x[i];
        CHECK (diff == 0 && e->bypassed (), "back at 0: %zu samples differ", diff);
        CHECK (finite (y) && finite (yr), "finite");
    }
}

TEST (split_sums_flat)
{
    // the bands summed with no gain (the knob only just off 0): an all-pass, flat within 0.1 dB
    auto e = engine (1e-7);
    std::vector<float> x (65536, 0.0f);
    x[0] = 1.0f;
    const auto y = aligned (*e, run (*e, x));
    std::vector<std::complex<double>> h (y.begin (), y.end ());
    fft (h);
    double lo = 1e9, hi = -1e9;
    for (size_t i = 1; i < h.size () / 2; ++i)
    {
        const double f = (double)i * kSr / (double)h.size ();
        if (f < 10.0 || f > 22000.0)
            continue;
        const double g = 20.0 * std::log10 (std::abs (h[i]));
        lo = std::min (lo, g);
        hi = std::max (hi, g);
    }
    std::printf ("    the summed bands: %+.4f .. %+.4f dB (10 Hz .. 22 kHz)\n", lo, hi);
    CHECK (lo > -0.1 && hi < 0.1, "flat: %+.3f .. %+.3f dB", lo, hi);
    // the filters on their own (Dsp.h): a split's low + high is its all-pass, sample for sample
    dsp::SvfCoefs c;
    c.set (1000.0, kSr);
    dsp::Lr4 s;
    dsp::Allpass a;
    double worst = 0.0;
    Rng r (9);
    for (int i = 0; i < 4800; ++i)
    {
        const double v = r.next ();
        double l, hh;
        s.tick (v, c, l, hh);
        worst = std::max (worst, std::fabs (l + hh - a.tick (v, c)));
    }
    CHECK (worst < 1e-9, "low + high = all-pass: %.2e", worst);
}

namespace {
struct PinkResult
{
    double slopeIn, slopeOut, spreadIn, spreadOut, levelIn, levelOut;
    std::vector<double> in, out;
};
// x through Squeeze `u` (6 s; the last 3 s analysed), octaves o0 .. o1
PinkResult pinkRun (const std::vector<float>& x, double u, int o0 = 1, int o1 = 8)
{
    auto e = engine (u);
    const auto y = aligned (*e, run (*e, x));
    const size_t a = x.size () / 2, b = x.size () - 4096;
    PinkResult r;
    r.in = octaves (x, a, b);
    r.out = octaves (y, a, b);
    r.slopeIn = slope (r.in, o0, o1);
    r.slopeOut = slope (r.out, o0, o1);
    r.spreadIn = spread (r.in, o0, o1);
    r.spreadOut = spread (r.out, o0, o1);
    // the loudness: the power in the octaves (20 Hz .. 20 kHz; brown noise has much more under 20 Hz, which
    // follows the lowest band's gain)
    auto sum = [] (const std::vector<double>& o) {
        double s = 0.0;
        for (double v : o)
            s += std::pow (10.0, v / 10.0);
        return 10.0 * std::log10 (s);
    };
    r.levelIn = sum (r.in);
    r.levelOut = sum (r.out);
    return r;
}
void printOct (const char* what, const std::vector<double>& o)
{
    std::printf ("    %-6s", what);
    for (double v : o)
        std::printf (" %6.1f", v);
    std::printf ("\n");
}
} // namespace

TEST (pink_moves_towards_pink)
{
    // at 50 % (full pink, no OTT) the octave spectrum's slope error (against 0 dB per octave, pink) over
    // 40 Hz .. 10 kHz (octaves 1 .. 8) falls by a large factor, and the loudness stays
    struct Case
    {
        const char* name;
        std::vector<float> x;
        int o0, o1;
        double factor; // how far the slope error must fall at least
    };
    const Case cases[] = {
        {"white", white (6.0, 0.1), 1, 8, 4.0},
        {"brown", brown (6.0, 0.1), 1, 8, 4.0},
        // (nothing under its 110 Hz fundamental; its few strong harmonics near the crossovers fall in two bands
        // through the crossovers' slopes, so the bands cannot place them as exactly as noise)
        {"bright synth", brightSynth (6.0, 0.1), 2, 8, 2.5},
    };
    for (const Case& c : cases)
    {
        const PinkResult r = pinkRun (c.x, 0.5, c.o0, c.o1);
        std::printf ("    %s: slope %+.2f -> %+.2f dB/oct, spread %.1f -> %.1f dB, power %.1f -> %.1f dB\n", c.name, r.slopeIn,
                     r.slopeOut, r.spreadIn, r.spreadOut, r.levelIn, r.levelOut);
        printOct ("in", r.in);
        printOct ("out", r.out);
        CHECK (std::fabs (r.slopeOut) < std::fabs (r.slopeIn) / c.factor, "%s: slope error down at least %.1fx: %+.2f -> %+.2f",
               c.name, c.factor, r.slopeIn, r.slopeOut);
        CHECK (r.spreadOut < r.spreadIn / 2.0, "%s: closer to flat octaves: %.1f -> %.1f dB", c.name, r.spreadIn, r.spreadOut);
        CHECK (std::fabs (r.levelOut - r.levelIn) < 1.5, "%s: loudness kept: %.1f -> %.1f dB", c.name, r.levelIn, r.levelOut);
    }
    // a quarter of the way (pink amount 0.75): part of the way there
    const PinkResult q = pinkRun (white (6.0, 0.1), 0.25);
    const PinkResult h = pinkRun (white (6.0, 0.1), 0.5);
    std::printf ("    white at 25 %%: slope %+.2f dB/oct\n", q.slopeOut);
    CHECK (q.slopeOut > h.slopeOut + 0.2 && q.slopeOut < q.slopeIn - 0.5, "25 %%: between: %+.2f (50 %%: %+.2f, in %+.2f)", q.slopeOut,
           h.slopeOut, q.slopeIn);
}

TEST (pink_noise_unchanged)
{
    const auto x = pinkNoise (6.0, 0.1);
    const PinkResult r = pinkRun (x, 0.5, 1, 8);
    double worst = 0.0;
    for (int o = 1; o <= 8; ++o)
        worst = std::max (worst, std::fabs (r.out[(size_t)o] - r.in[(size_t)o]));
    printOct ("in", r.in);
    printOct ("out", r.out);
    std::printf ("    pink noise at 50 %%: octaves within %.2f dB, power %.2f -> %.2f dB\n", worst, r.levelIn, r.levelOut);
    CHECK (worst < 1.0, "pink noise nearly unchanged: %.2f dB", worst);
    CHECK (std::fabs (r.levelOut - r.levelIn) < 0.5, "loudness: %.2f -> %.2f", r.levelIn, r.levelOut);
}

TEST (near_silent_bands_not_boosted)
{
    // white noise high-passed at 2 kHz (24 dB/oct): the bands under it are 40+ dB down and get no boost
    auto x = white (4.0, 0.1);
    dsp::SvfCoefs c;
    c.set (2000.0, kSr);
    dsp::Svf s1, s2;
    for (auto& v : x)
        v = (float)s2.tick (s1.tick (v, c).hp, c).hp;
    for (double u : {0.5, 1.0})
    {
        auto e = engine (u);
        const auto y = aligned (*e, run (*e, x));
        double worst = -100.0;
        for (int b = 0; b <= 3; ++b)
            worst = std::max (worst, e->pinkGainDb (b));
        const auto oi = octaves (x, 96000, 180000), oo = octaves (y, 96000, 180000);
        const double top = *std::max_element (oo.begin (), oo.end ());
        double low = -300.0, lowIn = -300.0;
        for (int o = 0; o <= 3; ++o)
        {
            low = std::max (low, oo[(size_t)o]);
            lowIn = std::max (lowIn, oi[(size_t)o]);
        }
        std::printf ("    %.0f %%: the low bands' pink gain at most %+.2f dB; the octaves under 320 Hz %.1f dB under the loudest "
                     "(in: %.1f)\n",
                     100 * u, worst, top - low, *std::max_element (oi.begin (), oi.end ()) - lowIn);
        CHECK (worst <= 0.01, "%.0f %%: no pink boost in the near-silent bands: %+.2f dB", 100 * u, worst);
        // (at 50 % only the pink stage works; at 100 % OTT's upward branch lifts quiet bands, as OTT does)
        CHECK (top - low > (u == 0.5 ? 50.0 : 20.0), "%.0f %%: the near-silent octaves stay far down: %.1f dB under the loudest",
               100 * u, top - low);
        CHECK (finite (y), "finite");
    }
    // silence: silence out, every gain at 0 dB
    {
        auto e = engine (0.5);
        const std::vector<float> z (48000, 0.0f);
        const auto y = run (*e, z);
        CHECK (peak (y, 0, y.size ()) == 0.0, "silence in, silence out");
        double g = 0.0;
        for (int b = 0; b < kBands; ++b)
            g = std::max (g, std::fabs (e->pinkGainDb (b)));
        CHECK (g == 0.0, "silence: no pink gain (%.3f)", g);
    }
}

TEST (ott_boost)
{
    // pink-noise bursts: loud (-10 dBFS) and quiet (-50 dBFS) by turns, 400 ms each. At 100 % against 50 %
    // (where only the pink stage works, nearly nothing on pink noise): the quiet parts rise (upward), the
    // loud ones fall (downward), and the crest factor of a drum-like signal drops.
    const size_t seg = 19200;
    auto x = pinkNoise (4.0, 1.0, 21);
    for (size_t i = 0; i < x.size (); ++i)
        x[i] *= ((i / seg) % 2 == 0) ? 0.316f : 0.00316f;
    auto measure = [&] (double u, double& loud, double& quiet) {
        auto e = engine (u);
        const auto y = aligned (*e, run (*e, x));
        double ls = 0, qs = 0;
        int ln = 0, qn = 0;
        for (size_t s = 2; s < x.size () / seg - 1; ++s)
        {
            const double r = rms (y, s * seg + seg / 2, (s + 1) * seg); // (the second half of each segment)
            if (s % 2 == 0)
                ls += r * r, ++ln;
            else
                qs += r * r, ++qn;
        }
        loud = db (std::sqrt (ls / ln));
        quiet = db (std::sqrt (qs / qn));
    };
    double l50, q50, l100, q100;
    measure (0.5, l50, q50);
    measure (1.0, l100, q100);
    std::printf ("    loud %.1f -> %.1f dBFS, quiet %.1f -> %.1f dBFS (50 %% -> 100 %%)\n", l50, l100, q50, q100);
    CHECK (q100 > q50 + 6.0, "quiet parts rise: %.1f -> %.1f", q50, q100);
    CHECK (l100 < l50 - 3.0, "loud parts fall: %.1f -> %.1f", l50, l100);
    CHECK ((l100 - q100) < (l50 - q50) - 12.0, "the gap between them closes: %.1f -> %.1f dB", l50 - q50, l100 - q100);

    // a drum-like signal: noise hits decaying over 60 ms, four a second, over a quiet bed. The crest factor
    // of its level (the loudest 50 ms against the average; OTT's 6 ms attack lets the first milliseconds of a
    // hit through, as OTT does, so the sample peak is not the measure) drops, and it gets louder
    std::vector<float> d ((size_t)(4.0 * kSr));
    Rng r (31);
    for (size_t i = 0; i < d.size (); ++i)
        d[i] = (float)(0.25 * r.next () * std::exp (-(double)(i % 12000) / (0.06 * kSr)) + 0.01 * r.next ());
    auto measureDrums = [&] (double u, double& crest, double& level) {
        auto e = engine (u);
        const auto y = aligned (*e, run (*e, d));
        double top = 0.0;
        for (size_t s = 48000; s + 2400 <= y.size (); s += 2400)
            top = std::max (top, rms (y, s, s + 2400));
        level = db (rms (y, 48000, y.size () - 4800));
        crest = db (top) - level;
    };
    double c50, c100, v50, v100;
    measureDrums (0.5, c50, v50);
    measureDrums (1.0, c100, v100);
    std::printf ("    drums: level crest factor %.1f -> %.1f dB, level %.1f -> %.1f dBFS\n", c50, c100, v50, v100);
    CHECK (c100 < c50 - 1.5, "crest factor reduced: %.1f -> %.1f dB", c50, c100);
    // pink noise swelling and falling by 24 dB twice a second: the swell's depth (the level around its tops
    // against the level around its bottoms, 20 ms each, every cycle) shrinks
    {
        auto m = pinkNoise (4.0, 0.1, 33);
        for (size_t i = 0; i < m.size (); ++i)
            m[i] *= (float)std::pow (10.0, -0.6 + 0.6 * std::sin (2.0 * kPi * 2.0 * (double)i / kSr));
        auto depthOf = [] (const std::vector<float>& y) {
            double top = 0.0, bottom = 0.0;
            for (size_t c = 2; c < 7; ++c) // (cycles of 24000 samples: top at 6000, bottom at 18000)
            {
                top += rms (y, c * 24000 + 5520, c * 24000 + 6480);
                bottom += rms (y, c * 24000 + 17520, c * 24000 + 18480);
            }
            return db (top) - db (bottom);
        };
        auto at = [&] (double u) {
            auto e = engine (u);
            return depthOf (aligned (*e, run (*e, m)));
        };
        const double in = depthOf (m), d50 = at (0.5), d100 = at (1.0);
        std::printf ("    swelling pink noise: the swell %.1f dB (in) / %.1f dB (50 %%) -> %.1f dB (100 %%)\n", in, d50, d100);
        CHECK (d100 < d50 - 6.0, "the swell flattened: %.1f -> %.1f dB", d50, d100);
    }
}

TEST (continuous_through_half)
{
    // pink noise with drums: the level at 49.9 / 50 / 50.1 % within 0.2 dB, and a slow sweep 40 -> 60 % with
    // no jump between 50 ms windows
    auto x = pinkNoise (6.0, 0.1);
    Rng r (41);
    for (size_t i = 0; i < x.size (); ++i)
        x[i] += (float)(0.3 * r.next () * std::exp (-(double)(i % 24000) / (0.05 * kSr)));
    double lv[3];
    const double us[3] = {0.499, 0.5, 0.501};
    for (int k = 0; k < 3; ++k)
    {
        auto e = engine (us[k]);
        const auto y = run (*e, x);
        lv[k] = db (rms (y, 96000, y.size ()));
    }
    std::printf ("    49.9 / 50 / 50.1 %%: %.3f / %.3f / %.3f dBFS\n", lv[0], lv[1], lv[2]);
    CHECK (std::fabs (lv[0] - lv[1]) < 0.2 && std::fabs (lv[2] - lv[1]) < 0.2, "no jump at 50 %%");
    // the sweep: the gain (output against input, 50 ms windows) moves no more from one window to the next
    // than it does at a still 50 % (the drums move it by themselves)
    const size_t n = x.size (), w = 2400;
    auto worstStep = [&] (const std::vector<float>& y) {
        double worst = 0.0;
        for (size_t s = 96000 / w; s + 2 < n / w; ++s)
        {
            const double d0 = db (rms (y, s * w, (s + 1) * w)) - db (rms (x, s * w, (s + 1) * w));
            const double d1 = db (rms (y, (s + 1) * w, (s + 2) * w)) - db (rms (x, (s + 1) * w, (s + 2) * w));
            worst = std::max (worst, std::fabs (d1 - d0));
        }
        return worst;
    };
    auto still = engine (0.5);
    const double ref = worstStep (aligned (*still, run (*still, x, nullptr, 64)));
    auto e = engine (0.4);
    const auto y = aligned (*e, run (*e, x, nullptr, 64, [&] (size_t a) { e->setParam (kSqueeze, 0.4 + 0.2 * (double)a / (double)n); }));
    const double worst = worstStep (y);
    std::printf ("    sweep 40 -> 60 %%: the gain moves at most %.2f dB between 50 ms windows (still at 50 %%: %.2f dB)\n", worst, ref);
    CHECK (worst < ref + 0.5, "no jump in the sweep: %.2f dB (still: %.2f dB)", worst, ref);
}

TEST (fast_automation)
{
    // the knob thrown 0 -> 100 % -> 0 in 50 ms steps on a 100 Hz sine plus noise: finite, no denormals, and
    // no click (no sample-to-sample step far beyond what the still settings give)
    std::vector<float> x ((size_t)(3.0 * kSr));
    Rng r (51);
    for (size_t i = 0; i < x.size (); ++i)
        x[i] = (float)(0.25 * std::sin (2.0 * kPi * 100.0 * (double)i / kSr) + 0.02 * r.next ());
    auto maxStep = [] (const std::vector<float>& y, size_t from) {
        double m = 0.0;
        for (size_t i = from + 1; i < y.size (); ++i)
            m = std::max (m, (double)std::fabs (y[i] - y[i - 1]));
        return m;
    };
    double ref = 0.0;
    for (double u : {0.0, 0.5, 1.0})
    {
        auto e = engine (u);
        ref = std::max (ref, maxStep (run (*e, x), 24000));
    }
    auto e = engine (0.0);
    std::vector<float> yr;
    const auto y = run (*e, x, &yr, 32, [&] (size_t a) {
        const double ph = std::fmod ((double)a / (0.1 * kSr), 1.0);
        e->setParam (kSqueeze, ph < 0.5 ? 2.0 * ph : 2.0 - 2.0 * ph);
    });
    const double s = maxStep (y, 24000);
    int denormals = 0;
    for (float v : y)
        denormals += v != 0.0f && std::fabs (v) < std::numeric_limits<float>::min ();
    std::printf ("    largest step: %.4f (the still settings: %.4f)\n", s, ref);
    CHECK (finite (y) && finite (yr), "finite");
    CHECK (denormals == 0, "no denormals (%d)", denormals);
    CHECK (s < 1.5 * ref, "no clicks: %.4f against %.4f", s, ref);
    CHECK (peak (y, 0, y.size ()) < 4.0, "bounded: %.2f", peak (y, 0, y.size ()));
}

TEST (mix_output_speed)
{
    const auto x = white (4.0, 0.1);
    // Mix 0 at 100 %: the input's level (the all-passed dry signal)
    {
        auto e = engine (1.0);
        e->setParam (kMix, 0.0);
        e->reset ();
        const auto y = run (*e, x);
        const double d = db (rms (y, 48000, y.size ())) - db (rms (x, 48000, x.size ()));
        CHECK (std::fabs (d) < 0.05, "Mix 0: the dry level (%+.3f dB)", d);
    }
    // Output -6 dB at Squeeze 0: the input at half the level
    {
        auto e = engine (0.0);
        e->setParam (kOutput, -6.0);
        e->reset ();
        const auto y = run (*e, x);
        const double d = db (rms (y, 48000, y.size ())) - db (rms (x, 48000, x.size ()));
        CHECK (std::fabs (d + 6.0) < 0.01, "Output -6 dB: %+.3f dB", d);
    }
    // Speed: brown noise switching to white; how long the top band's cut takes to reach 90 % of its way
    auto settle = [&] (int speed) {
        auto e = engine (0.5);
        e->setParam (kSpeed, speed);
        const auto b = brown (2.0, 0.1), w = white (6.0, 0.1);
        run (*e, b);
        const double from = e->pinkGainDb (kBands - 1);
        std::vector<double> g;
        run (*e, w, nullptr, 240, [&] (size_t) { g.push_back (e->pinkGainDb (kBands - 1)); });
        const double to = g.back ();
        for (size_t i = 0; i < g.size (); ++i)
            if (std::fabs (g[i] - from) >= 0.9 * std::fabs (to - from))
                return (double)i * 240.0 / kSr;
        return 99.0;
    };
    const double fast = settle (kSpeedFast), slow = settle (kSpeedSlow);
    std::printf ("    the top band settles in %.2f s (Fast), %.2f s (Slow)\n", fast, slow);
    CHECK (fast < 0.6 && slow > 2.0 * fast, "Slow is slower: %.2f s against %.2f s", slow, fast);
}

TEST (recovers_from_nan)
{
    auto e = engine (1.0);
    auto x = white (1.0, 0.1);
    x[1000] = std::numeric_limits<float>::quiet_NaN ();
    x[1001] = std::numeric_limits<float>::infinity ();
    const auto y = run (*e, x);
    CHECK (finite (std::vector<float> (y.begin () + 24000, y.end ())), "finite again after a NaN in the input");
}

TEST (cpu_budget)
{
    // 10 s of stereo noise and drums: the defaults, and 100 % with the end saturator on; the best of three
    auto x = pinkNoise (10.0, 0.1);
    for (bool heavy : {false, true})
    {
        double secs = 1e9;
        std::vector<float> l;
        for (int i = 0; i < 3; ++i)
        {
            auto e = engine (heavy ? 1.0 : defaultNormalized (kSqueeze));
            if (heavy)
                e->setParam (kTailBase + pk::kTailOn, 1.0);
            e->reset ();
            const std::clock_t t0 = std::clock ();
            l = run (*e, x, nullptr, 512);
            secs = std::min (secs, (double)(std::clock () - t0) / CLOCKS_PER_SEC);
        }
        CHECK (finite (l), "finite");
        std::printf ("    CPU: %.2f%% of one core (%s)\n", 100.0 * secs / 10.0, heavy ? "100 %, the end saturator on" : "the defaults");
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
