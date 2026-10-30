// Headless tests for the Tapr DSP. Run: ./tapr_tests [filter]
// The tape is Scream 4's Tape mode (gains and harmonics measured at eight settings), the Cut bells are
// Scream's (their measured shapes, and the engine plays them), Cut Post sits after the curve as in Scream,
// Cut Pre drives the curve, Compression Post leaves the curve as hard, Mix 0 is the delayed input bit for bit,
// the wet path is aligned with the latency, switches neither click nor blow up, Drive up to 200 % (the gains,
// the aliasing and the level held past 100 %, stable), a NaN does not stick, the defaults level a bass, and
// the CPU budget.
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

using namespace tapr;

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
double maxStep (const std::vector<float>& x, size_t a)
{
    double m = 0.0;
    for (size_t i = std::max<size_t> (a, 1); i < x.size (); ++i)
        m = std::max (m, (double)std::fabs (x[i] - x[i - 1]));
    return m;
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
// the tape alone (Cut off), Output 0 dB
void plain (Engine& e)
{
    e.setParam (kCutOn, 0.0);
    e.setParam (kOutput, 0.0);
}
// runs mono input (both channels) in blocks
std::vector<float> run (Engine& e, const std::vector<float>& x, std::vector<float>* right = nullptr, int block = 480,
                        const std::function<void (size_t)>& perBlock = {})
{
    std::vector<float> l (x.size ()), r (x.size ());
    for (size_t a = 0; a < x.size (); a += (size_t)block)
    {
        const int n = (int)std::min ((size_t)block, x.size () - a);
        e.process (x.data () + a, x.data () + a, l.data () + a, r.data () + a, n);
        if (perBlock)
            perBlock (a + (size_t)n);
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

} // namespace

TEST (tape_is_scream_4s)
{
    // Scream 4's Tape mode, measured (sweepr's TapeShape.h): a 100 Hz sine through it at Damage, P1 and P2 =
    // Drive, Speed and Compression, its Master at 0 dB: the gain and the third and fifth harmonics (dB re
    // the fundamental). The same settings here (Cut off, Output 0 dB) give the same within 0.3 dB (the gain)
    // and 1.5 dB (the harmonics).
    struct Case
    {
        double drive, speed, comp, inDb, gain, h3, h5;
    };
    const double m = 4.4378; // (Scream's Master in the patch measured)
    const Case cases[] = {
        {0.75, 0.8, 0.35, -48.0, 28.68 + m, -42.7, -64.2}, {0.75, 0.8, 0.35, -24.0, 14.65 + m, -17.8, -43.1},
        {0.75, 0.8, 0.35, -12.0, 3.00 + m, -14.8, -33.0},  {0.75, 0.8, 0.35, 0.0, -8.93 + m, -14.2, -30.4},
        {0.0, 0.8, 0.35, 0.0, -17.07 + m, -38.6, -62.8},   {32.0 / 127.0, 0.8, 0.35, -12.0, -2.93 + m, -34.3, -62.4},
        {0.75, 0.8, 0.0, -12.0, 3.61 + m, -9.6, -14.0},    {0.75, 0.8, 1.0, -12.0, -2.12 + m, -30.4, -52.8},
    };
    for (const auto& c : cases)
    {
        auto e = engine ([&c] (Engine& e) {
            plain (e);
            e.setParam (kDrive, c.drive);
            e.setParam (kSpeed, c.speed);
            e.setParam (kCompression, c.comp);
        });
        const double amp = std::pow (10.0, c.inDb / 20.0);
        const auto x = sine (100.0, 1.5, amp);
        const auto y = run (*e, x);
        const size_t a = 24000, b = a + 38400;
        const double fund = toneLevel (y, a, b, 100.0);
        const double gain = db (fund / amp), h3 = db (toneLevel (y, a, b, 300.0) / fund), h5 = db (toneLevel (y, a, b, 500.0) / fund);
        CHECK (std::fabs (gain - c.gain) < 0.3 && std::fabs (h3 - c.h3) < 1.5 && std::fabs (h5 - c.h5) < 1.5,
               "Drive %.0f %%, Compression %.0f %%, %.0f dBFS: gain %+.2f dB (Scream %+.2f), H3 %.1f (%.1f), H5 %.1f (%.1f)", 100 * c.drive,
               100 * c.comp, c.inDb, gain, c.gain, h3, c.h3, h5, c.h5);
    }
}

TEST (cut_is_scream_4s)
{
    // Scream 4's Cut bells, measured (CutShape.h): a slider step's gain and the prototype fitted to its impulse
    // response (centre Hz, the poles' and the zeros' dampings, at 48 kHz). The same gain here gives the same
    // bell within 0.02 dB from 20 Hz to 20 kHz
    struct Case
    {
        int band;
        double gainDb, centre, poles, zeros;
    };
    const Case cases[] = {
        {0, -18.209, 126.31, 8.1390, 1.0003}, {0, -4.939, 126.34, 1.7685, 1.0016}, {0, 17.924, 126.33, 1.0001, 7.8754},
        {1, -18.209, 982.62, 8.1591, 1.0028}, {1, 9.098, 982.62, 1.0027, 2.8581},  {2, -18.209, 9826.23, 10.9042, 1.3401},
        {2, 4.497, 9826.23, 1.3401, 2.2490},  {2, 17.924, 9826.23, 1.3401, 10.5528},
    };
    double worst = 0.0;
    for (const auto& c : cases)
    {
        const sweepr::bell::Prototype ours = cut::prototype (cut::kCentreHz[c.band], c.gainDb, kSr), ref {c.centre, c.poles, c.zeros};
        for (double f = 20.0; f < 20001.0; f *= 1.05)
            worst = std::max (worst, std::fabs (db (std::abs (sweepr::bell::response (ours, f, kSr))) -
                                                db (std::abs (sweepr::bell::response (ref, f, kSr)))));
    }
    std::printf ("    at most %.4f dB from Scream's measured Cut bells\n", worst);
    CHECK (worst < 0.02, "%.3f dB", worst);
    // and the engine plays them: Tape off, Lo +12, Mid -9, Hi +6, sine by sine
    auto e = engine ([] (Engine& e) {
        plain (e);
        e.setParam (kTapeOn, 0.0);
        e.setParam (kCutOn, 1.0);
        e.setParam (kCutLo, 12.0);
        e.setParam (kCutMid, -9.0);
        e.setParam (kCutHi, 6.0);
    });
    for (double f : {50.0, 126.0, 400.0, 983.0, 3000.0, 9826.0, 16000.0})
    {
        e->reset ();
        const auto x = sine (f, 0.5, 0.1);
        const auto y = run (*e, x);
        const double got = db (rms (y, 12000, y.size ())) - db (rms (x, 12000, x.size ()));
        std::complex<double> h = 1.0;
        const double g[3] = {12.0, -9.0, 6.0};
        for (int b = 0; b < 3; ++b)
            h *= sweepr::bell::response (cut::prototype (cut::kCentreHz[b], g[b], kSr), f, kSr);
        CHECK (std::fabs (got - db (std::abs (h))) < 0.1, "%.0f Hz: %.2f dB, the bells %.2f dB", f, got, db (std::abs (h)));
    }
}

TEST (cut_bells_move)
{
    // the Mid bell moved to 2.5 kHz at +12 dB: the boost is there, and no longer at 983 Hz
    auto e = engine ([] (Engine& e) {
        plain (e);
        e.setParam (kTapeOn, 0.0);
        e.setParam (kCutOn, 1.0);
        e.setParam (kCutMid, 12.0);
        e.setParam (kCutMidFreq, 2500.0);
    });
    double got[2] {};
    const double f[2] = {2500.0, 982.6};
    for (int k = 0; k < 2; ++k)
    {
        e->reset ();
        const auto x = sine (f[k], 0.5, 0.1);
        const auto y = run (*e, x);
        got[k] = db (rms (y, 12000, y.size ())) - db (rms (x, 12000, x.size ()));
    }
    const double want = db (std::abs (sweepr::bell::response (cut::prototype (2500.0, 12.0, kSr), 982.6, kSr)));
    CHECK (std::fabs (got[0] - 12.0) < 0.1 && std::fabs (got[1] - want) < 0.1, "at 2.5 kHz %.2f dB (12), at 983 Hz %.2f dB (%.2f)", got[0], got[1],
           want);
    CHECK (paramTable ().info (kCutLoFreq).def == cut::kCentreHz[0] && paramTable ().info (kCutHiFreq).def == cut::kCentreHz[2],
           "Scream's centres by default");
}

TEST (cut_post_is_after_the_curve)
{
    // Scream 4's order (Cut after the tape): Drive 75 %, Speed 100 %, Compression 35 %, Lo at -18.2 dB, a 100
    // Hz sine at 0 dBFS: Scream's output (its Master at 0 dB) is 21.85 dB below the input, the third
    // harmonic -8.5 dB and the fifth -20.4 dB (the curve's harmonics, then the cut)
    auto e = engine ([] (Engine& e) {
        plain (e);
        e.setParam (kSpeed, 1.0);
        e.setParam (kCutOn, 1.0);
        e.setParam (kCutLo, -18.0);
    });
    const auto x = sine (100.0, 1.5, 1.0);
    const auto y = run (*e, x);
    const size_t a = 24000, b = a + 38400;
    const double fund = toneLevel (y, a, b, 100.0);
    const double gain = db (fund), h3 = db (toneLevel (y, a, b, 300.0) / fund), h5 = db (toneLevel (y, a, b, 500.0) / fund);
    CHECK (std::fabs (gain + 21.85) < 0.5 && std::fabs (h3 + 8.5) < 1.5 && std::fabs (h5 + 20.4) < 1.5,
           "gain %+.2f dB (Scream -21.85), H3 %.1f (-8.5), H5 %.1f (-20.4)", gain, h3, h5);
}

TEST (cut_pre_shapes_what_saturates)
{
    // Lo +12 dB on a 100 Hz sine at -24 dBFS, no compression: after the tape (Post) it only lifts the
    // output; before it (Pre) it drives the curve harder, so the third harmonic is far stronger
    double h3[2] {}, out[2] {};
    for (int place : {kPre, kPost})
    {
        auto e = engine ([place] (Engine& e) {
            plain (e);
            e.setParam (kCompression, 0.0);
            e.setParam (kDrive, 0.4);
            e.setParam (kCutOn, 1.0);
            e.setParam (kCutLo, 12.0);
            e.setParam (kCutPlace, (double)place);
        });
        const auto y = run (*e, sine (100.0, 1.5, std::pow (10.0, -24.0 / 20.0)));
        const double fund = toneLevel (y, 24000, 62400, 100.0);
        out[place] = db (fund);
        h3[place] = db (toneLevel (y, 24000, 62400, 300.0) / fund);
    }
    std::printf ("    Pre: out %.1f dB, H3 %.1f dB; Post: out %.1f dB, H3 %.1f dB\n", out[kPre], h3[kPre], out[kPost], h3[kPost]);
    CHECK (h3[kPre] > h3[kPost] + 10.0, "Pre drives harder: H3 %.1f against %.1f", h3[kPre], h3[kPost]);
}

TEST (compression_post_rides_the_saturated_sound)
{
    // Drive 100 %, a 100 Hz sine at -12 dBFS. Pre: more Compression is softer (H3 falls). Post: the curve is
    // driven as hard whatever Compression is (H3 stays near a square wave's -9.5 dB) and the level comes down
    double h3[2][2] {}, out[2][2] {};
    for (int place : {kPre, kPost})
        for (int k = 0; k < 2; ++k)
        {
            auto e = engine ([place, k] (Engine& e) {
                plain (e);
                e.setParam (kDrive, 1.0);
                e.setParam (kSpeed, 1.0);
                e.setParam (kCompression, k == 0 ? 0.0 : 1.0);
                e.setParam (kCompPlace, (double)place);
            });
            const auto y = run (*e, sine (100.0, 1.5, 0.25));
            const double fund = toneLevel (y, 24000, 62400, 100.0);
            out[place][k] = db (fund);
            h3[place][k] = db (toneLevel (y, 24000, 62400, 300.0) / fund);
        }
    std::printf ("    Pre: H3 %.1f -> %.1f dB, out %.1f -> %.1f dB; Post: H3 %.1f -> %.1f dB, out %.1f -> %.1f dB\n", h3[0][0], h3[0][1],
                 out[0][0], out[0][1], h3[1][0], h3[1][1], out[1][0], out[1][1]);
    CHECK (h3[kPre][1] < h3[kPre][0] - 10.0, "Pre: Compression softens the curve (H3 %.1f -> %.1f)", h3[kPre][0], h3[kPre][1]);
    CHECK (std::fabs (h3[kPost][1] - h3[kPost][0]) < 2.0 && out[kPost][1] < out[kPost][0] - 6.0,
           "Post: as hard (H3 %.1f -> %.1f), %.1f dB quieter", h3[kPost][0], h3[kPost][1], out[kPost][0] - out[kPost][1]);
}

TEST (mix_zero_is_the_delayed_input_bit_for_bit)
{
    for (bool on : {true, false})
    {
        auto e = engine ([on] (Engine& e) {
            e.setParam (kMix, 0.0);
            e.setParam (kOutput, 0.0);
            e.setParam (kTapeOn, on ? 1.0 : 0.0);
            e.setParam (kCutLo, on ? 9.0 : 0.0);
        });
        const int lat = e->latency ();
        const auto x = bass (1.0);
        std::vector<float> r;
        const auto y = run (*e, x, &r);
        size_t differ = 0;
        for (size_t i = 0; i < y.size (); ++i)
        {
            const float want = i >= (size_t)lat ? x[i - (size_t)lat] : 0.0f;
            differ += y[i] != want || r[i] != want;
        }
        CHECK (differ == 0, "Mix 0 (Tape %s): %zu samples differ from the input delayed by %d", on ? "on" : "off", differ, lat);
    }
}

TEST (wet_path_is_aligned_with_the_latency)
{
    // Tape and Cut off: the input comes back delayed by the latency the engine reports
    auto e = engine ([] (Engine& e) {
        plain (e);
        e.setParam (kTapeOn, 0.0);
        e.setParam (kCutOn, 0.0);
    });
    const int lat = e->latency ();
    CHECK (lat > 0 && lat < 200, "latency %d", lat);
    const auto x = sine (440.0, 0.5, 0.25);
    const auto y = run (*e, x);
    double err = 0.0;
    for (size_t i = 4800; i < y.size (); ++i)
        err = std::max (err, (double)std::fabs (y[i] - x[i - (size_t)lat]));
    CHECK (db (err / 0.25) < -60.0, "the wet path against the delayed input: %.1f dB", db (err / 0.25));
    for (uint32_t id : {kCompPlace, kCutPlace})
    {
        e->setParam (id, kPost);
        CHECK (e->latency () == lat, "the latency stays with another place");
    }
}

TEST (switches_do_not_click_or_blow_up)
{
    // Tape, Cut and both places switched every 0.1 s on a hot bass: finite, and the switches fade
    auto e = engine ([] (Engine& e) { e.setParam (kCutLo, 12.0); });
    const auto x = bass (2.0, 2.0);
    std::vector<float> y (x.size ()), r (x.size ());
    for (size_t a = 0; a < x.size (); a += 480)
    {
        const int k = (int)(a / 4800);
        e->setParam (kTapeOn, k % 2 == 0 ? 1.0 : 0.0);
        e->setParam (kCutOn, k % 3 == 0 ? 1.0 : 0.0);
        e->setParam (kCompPlace, k % 4 < 2 ? kPre : kPost);
        e->setParam (kCutPlace, k % 5 < 2 ? kPre : kPost);
        e->process (x.data () + a, x.data () + a, y.data () + a, r.data () + a, 480);
    }
    CHECK (finite (y) && finite (r), "finite");
    double peak = 0.0;
    for (float v : y)
        peak = std::max (peak, (double)std::fabs (v));
    CHECK (peak < 4.0, "peak %.2f", peak);
}

TEST (drive_goes_to_200)
{
    // Drive 0 .. 200 %: 0.6 dB a percent on (-10 dB at 0, +50 dB at 100 %, +110 dB at 200 %), 75 % by default;
    // up to 100 % the same gains as before, past it the leveller holds the level, the aliasing stays where it is
    // at 100 %, nothing runs away
    const auto& t = paramTable ();
    CHECK (t.info (kDrive).min == 0.0 && t.info (kDrive).max == 2.0 && t.info (kDrive).def == 0.75 && toPlain (kDrive, 1.0) == 2.0 &&
               toPlain (kDrive, 0.5) == 1.0 && t.toText (kDrive, 2.0) == "200 %" && t.toText (kDrive, 0.75) == "75 %",
           "Drive 0 .. 200 %% (%s), 75 %% by default", t.toText (kDrive, 2.0).c_str ());
    CHECK (std::fabs (sweepr::tape::driveDb (0.0) + 10.02) < 1e-9 && std::fabs (sweepr::tape::driveDb (1.0) - 50.176) < 1e-9 &&
               std::fabs (sweepr::tape::driveDb (2.0) - 110.372) < 1e-9,
           "-10, +50 and +110 dB into the curve");
    // every percent up to 100 comes back as itself (the presets' and the old states' values)
    int wrong = 0;
    for (int k = 0; k <= 100; ++k)
        wrong += toPlain (kDrive, toNormalized (kDrive, k / 100.0)) == k / 100.0 ? 0 : 1;
    CHECK (wrong == 0, "0 .. 100 %% exact (%d not)", wrong);
    // a 3.1 kHz sine at -12 dBFS, Compression 0 (the hardest clipping): the aliasing (what is not a harmonic)
    // at 150 and 200 % within 1 dB of 100 %'s, the level the same; with the default Compression held as well
    auto measure = [] (double drive, double comp, double& aliasDb, double& level) {
        auto e = engine ([&] (Engine& e) {
            plain (e);
            e.setParam (kDrive, drive);
            e.setParam (kCompression, comp);
        });
        const auto y = run (*e, sine (3100.0, 1.0, 0.25));
        const size_t n = 4096, a = y.size () - n;
        double harm = 0.0, all = 0.0;
        for (size_t k = 1; k < n / 2; ++k)
        {
            const double hz = k * kSr / n;
            const double l = toneLevel (y, a, a + n, hz);
            all += l * l;
            if (std::fabs (hz / 3100.0 - std::round (hz / 3100.0)) * 3100.0 < 4.0 * kSr / n)
                harm += l * l;
        }
        aliasDb = 10.0 * std::log10 ((all - harm) / harm);
        level = db (rms (y, a, y.size ()));
        return finite (y);
    };
    for (double comp : {0.0, 0.35})
    {
        double a100, l100;
        measure (1.0, comp, a100, l100);
        for (double drive : {1.5, 2.0})
        {
            double a, l;
            const bool ok = measure (drive, comp, a, l);
            CHECK (ok && a < a100 + 1.0 && std::fabs (l - l100) < 0.5, "Compression %.0f %%, Drive %.0f %%: aliasing %.1f dB (100 %%: %.1f), level %.2f dBFS (%.2f)",
                   comp * 100, drive * 100, a, a100, l, l100);
        }
    }
    // a hot bass and silence at 200 %: finite, bounded by the curve as at 100 % (its flat top through Speed's
    // shelf), silence stays silent
    for (double comp : {0.0, 0.35})
    {
        double peak[2] = {0.0, 0.0}, quiet = 0.0;
        bool ok = true;
        for (int k = 0; k < 2; ++k)
        {
            auto e = engine ([&] (Engine& e) {
                plain (e);
                e.setParam (kDrive, k == 0 ? 1.0 : 2.0);
                e.setParam (kCompression, comp);
            });
            auto x = bass (2.0, 4.0);
            std::fill (x.begin () + 48000, x.begin () + 72000, 0.0f);
            const auto y = run (*e, x);
            for (float v : y)
                peak[k] = std::max (peak[k], (double)std::fabs (v));
            ok = ok && finite (y);
            quiet = rms (y, 60000, 72000);
        }
        CHECK (ok && peak[1] < 0.8 && peak[1] < peak[0] * 1.05 && quiet < 1e-6, "Drive 200 %%, Compression %.0f %%: peak %.3f (100 %%: %.3f), silence %.1f dBFS",
               comp * 100, peak[1], peak[0], db (quiet));
    }
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

TEST (defaults_level_a_bass)
{
    // the defaults level a bass as Scream 4's Tape does: 18 dB apart going in, a few dB apart coming out
    double out[2] {};
    for (int k = 0; k < 2; ++k)
    {
        auto e = engine ();
        const auto y = run (*e, bass (4.0, k == 0 ? 0.125 : 1.0));
        out[k] = db (rms (y, 48000, y.size ()));
    }
    std::printf ("    the defaults on a bass: %.1f and %.1f dBFS RMS\n", out[0], out[1]);
    CHECK (out[1] - out[0] < 6.0, "18 dB more in, %.1f dB more out", out[1] - out[0]);
}

TEST (cpu_budget)
{
    // 10 s of stereo bass: the defaults, and the same with Soft Clip and Hard Clip on; the best of three
    auto x = bass (10.0);
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
                }
            });
            const pk::testing::CpuClock t0 = pk::testing::cpuClock ();
            l = run (*e, x, nullptr, 512);
            secs = std::min (secs, (double)(pk::testing::cpuClock () - t0) / pk::testing::kCpuClocksPerSec);
        }
        CHECK (finite (l), "finite");
        std::printf ("    CPU: %.2f%% of one core (%s)\n", 100.0 * secs / 10.0, heavy ? "the defaults, the clips on" : "the defaults");
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
