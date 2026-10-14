// Headless tests for the Sweepr DSP. Run: ./sweepr_tests [filter]
// The bells are the measured reference EQ's (BellShape.h) and the engine's match them, the LFOs follow the song
// position (the same from any starting point, the random shapes too), run free at their rate and link, fast
// sweeps do not click, the tape stage stays finite and free of denormals, keeps aliasing low at full Drive
// on a high sine, holds the level across Drive, Mix 0 is the delayed input bit for bit, the wet path is
// aligned with it, switching Tape does not click, a NaN does not stick, sweepr_analyze measures the bells, and the
// CPU budget.
#include "pluginkit/testing/CpuClock.h"
#include "Analyze.h"
#include "Dsp.h"
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

using namespace sweepr;

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
// static bells, no tape, no end saturator, Output 0 dB
void plain (Engine& e)
{
    e.setParam (bandParam (0, kLfoDepth), 0.0);
    e.setParam (bandParam (1, kLfoDepth), 0.0);
    e.setParam (kTapeOn, 0.0);
    e.setParam (kOutput, 0.0);
}
// runs mono input (both channels) in blocks, the transport at `tempo` from `ppq0` while playing
std::vector<float> run (Engine& e, const std::vector<float>& x, std::vector<float>* right = nullptr, int block = 480, double ppq0 = 0.0,
                        bool play = true, const std::function<void (size_t)>& perBlock = {})
{
    std::vector<float> l (x.size ()), r (x.size ());
    for (size_t a = 0; a < x.size (); a += (size_t)block)
    {
        const int n = (int)std::min ((size_t)block, x.size () - a);
        e.setTransport (120.0, ppq0 + (double)a / kSr * 2.0, play);
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

TEST (bells_are_peq2s)
{
    // Reason's PEQ-2, measured through the Reason Rack Plugin at 48 kHz (BellShape.h): knob steps (Freq, Q,
    // Gain, 0 .. 127) and the prototype fitted to each impulse response (centre Hz, the poles' and the zeros'
    // dampings). The same steps on sweepr's knobs (normalized step / 127) give the same bell within 0.05 dB
    // (0.0001 dB from 40 Hz up; lower, PEQ-2's own centre wanders by up to 0.2 % around its formula, a
    // rounding of its coefficients it seems).
    struct Case
    {
        int f, q, g;
        double centre, poles, zeros;
    };
    const Case cases[] = {
        {64, 64, 127, 707.103, 0.43354, 3.38801},    {0, 64, 127, 29.796, 0.43220, 3.37703},   {127, 64, 127, 16000.000, 1.04698, 8.18184},
        {64, 0, 127, 707.107, 4.00570, 31.30340},    {64, 127, 127, 707.109, 0.06163, 0.48159}, {64, 64, 0, 707.104, 3.27715, 0.40589},
        {127, 64, 0, 16000.003, 7.91416, 0.98021},   {64, 0, 0, 707.104, 30.27945, 3.75028},   {64, 96, 80, 707.110, 0.15575, 0.26254},
        {64, 32, 32, 707.108, 4.94778, 1.74127},     {110, 64, 127, 6895.777, 0.49782, 3.89035}, {64, 64, 60, 707.111, 0.20482, 0.17975},
    };
    const auto& t = paramTable ();
    double worst = 0.0;
    for (const auto& c : cases)
    {
        const double fc = toPlain (bandParam (0, kFreq), c.f / 127.0), q = toPlain (bandParam (0, kQ), c.q / 127.0),
                     g = toPlain (bandParam (0, kGain), c.g / 127.0);
        const bell::Prototype ref {c.centre, c.poles, c.zeros};
        double err = 0.0;
        for (double f = 20.0; f < 20001.0; f *= 1.05)
            err = std::max (err, std::fabs (db (std::abs (dsp::bellResponse (f, fc, q, g, kSr))) - db (std::abs (bell::response (ref, f, kSr)))));
        worst = std::max (worst, err);
        CHECK (err < 0.05, "Freq %d, Q %d, Gain %d (%.1f Hz, Q %.2f, %+.2f dB): %.3f dB from PEQ-2's", c.f, c.q, c.g, fc, q, g, err);
    }
    std::printf ("    at most %.4f dB from PEQ-2's measured bells\n", worst);
    CHECK (std::fabs (t.info (bandParam (0, kGain)).min + 18.1417) < 1e-3 && std::fabs (toPlain (bandParam (0, kGain), 64.0 / 127.0)) < 1e-9,
           "Gain: PEQ-2's steps, 0 dB at step 64");
}

TEST (bells_match_their_response)
{
    // the defaults' bells (A 110 Hz Q 1 +13 dB, B 320 Hz Q 1.6 -15 dB), static, no tape: sine by sine
    auto e = engine (plain);
    double worst = 0.0;
    for (double f : {30.0, 60.0, 110.0, 180.0, 250.0, 320.0, 450.0, 800.0, 2000.0, 6000.0, 12000.0, 18000.0})
    {
        e->reset ();
        const auto x = sine (f, 0.5, 0.1);
        const auto y = run (*e, x);
        const double got = db (rms (y, 12000, y.size ())) - db (rms (x, 12000, x.size ()));
        const double want = db (std::abs (dsp::bellResponse (f, 110.0, 1.0, 13.0, kSr) * dsp::bellResponse (f, 320.0, 1.6, -15.0, kSr)));
        worst = std::max (worst, std::fabs (got - want));
        CHECK (std::fabs (got - want) < 0.25, "%.0f Hz: %.2f dB, the bells %.2f dB", f, got, want);
    }
    std::printf ("    worst %.3f dB from the bells\n", worst);
    // high up: a bell at 12 kHz is the digital one (it cramps towards Nyquist, as PEQ-2's)
    auto h = engine ([] (Engine& e) {
        plain (e);
        e.setParam (bandParam (0, kFreq), 12000.0);
        e.setParam (bandParam (0, kQ), 2.0);
        e.setParam (bandParam (0, kGain), 12.0);
        e.setParam (bandParam (1, kGain), 0.0);
    });
    for (double f : {6000.0, 9000.0, 12000.0, 15000.0, 18000.0, 21000.0})
    {
        h->reset ();
        const auto x = sine (f, 0.3, 0.1);
        const auto y = run (*h, x);
        const double got = db (rms (y, 9600, y.size ())) - db (rms (x, 9600, x.size ()));
        const double want = db (std::abs (dsp::bellResponse (f, 12000.0, 2.0, 12.0, kSr)));
        CHECK (std::fabs (got - want) < 0.4, "12 kHz bell at %.0f Hz: %.2f dB, the bell %.2f dB", f, got, want);
    }
}

TEST (wet_path_is_aligned_with_the_latency)
{
    // flat bells and no tape: the input comes back delayed by the latency the engine reports
    auto e = engine ([] (Engine& e) {
        plain (e);
        e.setParam (bandParam (0, kGain), 0.0);
        e.setParam (bandParam (1, kGain), 0.0);
    });
    const int lat = e->latency (), own = e->ownLatency ();
    CHECK (own > 0 && lat >= own && lat < 200, "latency %d (its own %d)", lat, own);
    const auto x = sine (440.0, 0.5, 0.25);
    const auto y = run (*e, x);
    double err = 0.0;
    for (size_t i = 4800; i < y.size (); ++i)
        err = std::max (err, (double)std::fabs (y[i] - x[i - (size_t)lat]));
    CHECK (db (err / 0.25) < -60.0, "the wet path against the delayed input: %.1f dB", db (err / 0.25));
    // the latency never changes with the settings (the end saturator's apart)
    for (auto set : std::vector<std::function<void (Engine&)>> {[] (Engine& e) { e.setParam (kTapeOn, 1.0); },
                                                                 [] (Engine& e) { e.setParam (kSpeed, 0.0); },
                                                                 [] (Engine& e) { e.setParam (kDrive, 1.0); }})
    {
        set (*e);
        e->reset ();
        CHECK (e->ownLatency () == own, "latency %d with another setting", e->ownLatency ());
    }
}

TEST (mix_zero_is_the_delayed_input_bit_for_bit)
{
    for (bool tape : {true, false})
    {
        auto e = engine ([tape] (Engine& e) {
            e.setParam (kMix, 0.0);
            e.setParam (kOutput, 0.0);
            e.setParam (kTapeOn, tape ? 1.0 : 0.0);
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
        CHECK (differ == 0, "Mix 0 (Tape %s): %zu samples differ from the input delayed by %d", tape ? "on" : "off", differ, lat);
    }
}

TEST (sync_lfo_follows_the_song_position)
{
    // 1/4, Sine, Depth +1 octave on A (the defaults): from the song's start and from beat 2.5 (a jump), the
    // swept frequency at the same song positions is the same, and it is 110 Hz x 2^sin (2 pi beats)
    auto record = [] (double ppq0, std::vector<std::pair<double, double>>& at) {
        auto e = engine ([] (Engine& e) { plain (e); e.setParam (bandParam (0, kLfoDepth), 1.0); });
        const auto x = sine (100.0, 3.0, 0.1);
        Engine* ep = e.get ();
        run (*e, x, nullptr, 480, ppq0, true, [&] (size_t end) {
            // (the last sample of the block: the frequency there, at its song position)
            at.push_back ({ppq0 + (double)(end - 1) / kSr * 2.0, ep->bandFreqNow (0)});
        });
    };
    std::vector<std::pair<double, double>> a, b;
    record (0.0, a);
    record (2.5, b);
    int compared = 0;
    double worstOct = 0.0, worstSine = 0.0;
    for (const auto& pb : b)
        for (const auto& pa : a)
            if (std::fabs (pa.first - pb.first) < 1e-9 && pb.first > 2.6)
            {
                worstOct = std::max (worstOct, std::fabs (std::log2 (pa.second / pb.second)));
                ++compared;
            }
    for (const auto& pa : a)
        if (pa.first > 0.1)
            worstSine = std::max (worstSine, std::fabs (std::log2 (pa.second / 110.0) - std::sin (2.0 * kPi * pa.first)));
    CHECK (compared > 50 && worstOct < 1e-6, "%d positions compared, at most %.2g octaves apart", compared, worstOct);
    CHECK (worstSine < 0.03, "the sweep follows sin (2 pi beats) within %.4f octaves", worstSine);

    // the random shapes too: Random on 1/16 from two starting points gives the same steps at the same places
    auto recordRandom = [] (double ppq0, int shape, std::vector<std::pair<double, double>>& at) {
        auto e = engine ([shape] (Engine& e) {
            plain (e);
            e.setParam (bandParam (1, kLfoDepth), 2.0);
            e.setParam (bandParam (1, kLfoShape), shape);
            e.setParam (bandParam (1, kLfoDiv), kDiv16);
        });
        const auto x = sine (100.0, 2.0, 0.1);
        Engine* ep = e.get ();
        run (*e, x, nullptr, 480, ppq0, true, [&] (size_t end) { at.push_back ({ppq0 + (double)(end - 1) / kSr * 2.0, ep->lfoNow (1)}); });
    };
    for (int shape : {kRandom, kSmoothRandom})
    {
        std::vector<std::pair<double, double>> ra, rb;
        recordRandom (0.0, shape, ra);
        recordRandom (1.0, shape, rb);
        int same = 0, cmp = 0;
        for (const auto& pb : rb)
            for (const auto& pa : ra)
                if (std::fabs (pa.first - pb.first) < 1e-9)
                {
                    ++cmp;
                    same += std::fabs (pa.second - pb.second) < 1e-9;
                }
        CHECK (cmp > 50 && same == cmp, "shape %d: %d of %d positions the same", shape, same, cmp);
    }
}

TEST (stopped_host_runs_on)
{
    // the host stopped: a synced LFO keeps moving at the tempo from where it was (the display moves)
    auto e = engine ([] (Engine& e) { plain (e); });
    const auto x = sine (100.0, 0.05, 0.1);
    run (*e, x, nullptr, 480, 0.0, false);
    CHECK (std::fabs (e->beatsNow () - 0.1) < 1e-6, "0.05 s at 120 BPM: %.4f beats", e->beatsNow ());
}

TEST (free_lfo_runs_at_its_rate)
{
    // Sync off at 2 Hz: four cycles in two seconds (the frequency crosses the knob's setting going up 4 times)
    auto e = engine ([] (Engine& e) {
        plain (e);
        e.setParam (bandParam (0, kLfoDepth), 1.0);
        e.setParam (bandParam (0, kLfoSync), 0.0);
        e.setParam (bandParam (0, kLfoRate), 2.0);
    });
    const auto x = sine (100.0, 2.0, 0.1);
    int ups = 0;
    double last = -1.0;
    Engine* ep = e.get ();
    run (*e, x, nullptr, 48, 0.0, true, [&] (size_t) {
        const double v = ep->lfoNow (0);
        ups += last < 0.0 && v >= 0.0;
        last = v;
    });
    CHECK (ups == 4, "%d upward crossings in 2 s at 2 Hz", ups);
}

TEST (lfo_switch_keeps_a_band_still)
{
    // A's LFO off: A stays at its Freq while B's LFO (on) keeps sweeping
    auto e = engine ([] (Engine& e) { e.setParam (kLfoOnA, 0.0); });
    double aLo = 1e9, aHi = 0.0, bLo = 1e9, bHi = 0.0;
    Engine* ep = e.get ();
    run (*e, bass (0.6), nullptr, 48, 0.0, true, [&] (size_t) {
        aLo = std::min (aLo, ep->bandFreqNow (0));
        aHi = std::max (aHi, ep->bandFreqNow (0));
        bLo = std::min (bLo, ep->bandFreqNow (1));
        bHi = std::max (bHi, ep->bandFreqNow (1));
    });
    CHECK (std::fabs (aLo - 110.0) < 1e-6 && std::fabs (aHi - 110.0) < 1e-6, "A held at %.2f .. %.2f Hz", aLo, aHi);
    CHECK (bLo < 200.0 && bHi > 500.0, "B swept %.0f .. %.0f Hz", bLo, bHi);
    CHECK (paramTable ().info (kLfoOnA).def == 1.0 && paramTable ().info (kLfoOnB).def == 1.0, "both LFOs on in a new instance");
}

TEST (link_makes_b_follow_a)
{
    // Link on, B's Phase 180: B's LFO is A's upside down (A's rate and shape, B's own phase offset)
    auto e = engine ([] (Engine& e) {
        plain (e);
        e.setParam (kLink, 1.0);
        e.setParam (bandParam (0, kLfoDiv), kDiv2);
        e.setParam (bandParam (1, kLfoDiv), kDiv16);   // (not used: linked)
        e.setParam (bandParam (1, kLfoShape), kSquare); // (not used)
        e.setParam (bandParam (1, kLfoPhase), 180.0);
    });
    const auto x = sine (100.0, 1.0, 0.1);
    double worst = 0.0;
    Engine* ep = e.get ();
    run (*e, x, nullptr, 480, 0.0, true, [&] (size_t) { worst = std::max (worst, std::fabs (ep->lfoNow (0) + ep->lfoNow (1))); });
    CHECK (worst < 1e-9, "B = -A within %.2g", worst);
}

TEST (fast_sweeps_do_not_click)
{
    // a 200 Hz sine through steep settings (Q 10, +18 dB) swept 4 octaves by a 40 Hz Square, a Saw and
    // Random: no sample jumps much further than the loudest static setting's sine can move (the bell at
    // 200 Hz: the sine 18 dB up). The room: PEQ-2's boost keeps its poles' width whatever the Gain (the
    // cookbook's narrows them), so a bell swept fast rings a little more (Random's jumps up to 1.6 times).
    const double amp = 0.25;
    const double staticStep = amp * std::pow (10.0, 18.0 / 20.0) * 2.0 * kPi * 200.0 / kSr;
    for (int shape : {kSquare, kSawUp, kRandom, kSine})
    {
        auto e = engine ([shape] (Engine& e) {
            plain (e);
            e.setParam (bandParam (0, kFreq), 200.0);
            e.setParam (bandParam (0, kQ), 10.0);
            e.setParam (bandParam (0, kGain), 18.0);
            e.setParam (bandParam (0, kLfoSync), 0.0);
            e.setParam (bandParam (0, kLfoRate), 40.0);
            e.setParam (bandParam (0, kLfoShape), shape);
            e.setParam (bandParam (0, kLfoDepth), 4.0);
            e.setParam (bandParam (1, kGain), 0.0);
        });
        const auto x = sine (200.0, 2.0, amp);
        const auto y = run (*e, x);
        const double step = maxStep (y, 4800);
        CHECK (finite (y) && step < 2.0 * staticStep, "shape %d: the largest step %.4f (static at most %.4f)", shape, step, staticStep);
    }
}

TEST (tape_is_finite_and_free_of_denormals)
{
    auto e = engine ([] (Engine& e) {
        e.setParam (kDrive, 1.0);
        e.setParam (kCompression, 1.0);
        e.setParam (kSpeed, 0.0);
    });
    std::vector<float> x = bass (1.0, 8.0); // very hot
    x.resize (x.size () + 96000, 0.0f);     // then two seconds of silence
    for (size_t i = 48000; i < 50000; ++i)
        x[i] = 1e-30f;                      // and a tiny signal
    std::vector<float> r;
    const auto y = run (*e, x, &r);
    int sub = 0;
    for (size_t i = 0; i < y.size (); ++i)
        sub += std::fpclassify (y[i]) == FP_SUBNORMAL || std::fpclassify (r[i]) == FP_SUBNORMAL;
    CHECK (finite (y) && finite (r), "finite");
    CHECK (sub == 0, "%d denormal samples", sub);
    CHECK (rms (y, 0, 48000) < 4.0, "hot input held: %.2f RMS", rms (y, 0, 48000));
}

TEST (high_drive_keeps_aliasing_low)
{
    // Drive 100 % (Speed 100 %, no compression, flat bells) on a 7 kHz sine at -6 dBFS and a 3.1 kHz one at
    // -12 dBFS: clipped to a square wave, whose harmonics above Nyquist would fold back between the real ones
    // (for 7 kHz to 20, 13, 6, 1, 8, 15 and 22 kHz); in the audible range, with the 4x oversampling and the
    // curve's anti-aliasing those stay far below the fundamental (Scream 4, at its base rate: -14 and -20 dB),
    // while the real harmonics are there
    struct Case
    {
        double hz, amp, limitDb;
    };
    for (const Case& k : {Case {7000.0, 0.5, -46.0}, Case {3100.0, 0.25, -52.0}})
    {
        auto e = engine ([] (Engine& e) {
            plain (e);
            e.setParam (bandParam (0, kGain), 0.0);
            e.setParam (bandParam (1, kGain), 0.0);
            e.setParam (kTapeOn, 1.0);
            e.setParam (kDrive, 1.0);
            e.setParam (kCompression, 0.0);
            e.setParam (kSpeed, 1.0);
        });
        const auto x = sine (k.hz, 1.0, k.amp);
        const auto y = run (*e, x);
        const size_t a = 9600, b = a + 32768;
        const double fund = toneLevel (y, a, b, k.hz);
        double worst = -300.0, worstF = 0.0;
        for (int h = 2; h <= 80; ++h)
        {
            double f = std::fmod (k.hz * h, kSr);
            if (f > kSr / 2)
                f = kSr - f;
            const double r = std::fmod (f, k.hz);
            if (k.hz * h < kSr / 2 || r < 1.0 || k.hz - r < 1.0 || f < 200.0 || f > 20000.0)
                continue; // (a real harmonic, or lands on one; above 20 kHz is the oversampler's transition band)
            const double lv = db (toneLevel (y, a, b, f) / fund);
            if (lv > worst)
            {
                worst = lv;
                worstF = f;
            }
        }
        const double h3 = db (toneLevel (y, a, b, 3.0 * k.hz) / fund);
        std::printf ("    %.0f Hz at %.0f dBFS: the worst alias %.1f dB (at %.0f Hz), the third harmonic %.1f dB\n", k.hz, db (k.amp), worst, worstF,
                     h3);
        CHECK (worst < k.limitDb, "%.0f Hz: aliasing %.1f dB at %.0f Hz", k.hz, worst, worstF);
        CHECK (h3 > -50.0, "the saturation is there: the third harmonic at %.1f dB", h3);
    }
}

TEST (tape_is_scream_4s)
{
    // Scream 4's Tape mode, measured (TapeShape.h): a 100 Hz sine through it at Damage, P1 and P2 = Drive,
    // Speed and Compression, its Master at 0 dB: the gain and the third and fifth harmonics (dB re the
    // fundamental). The same settings here (flat bells, Output 0 dB) give the same within 0.3 dB (the gain)
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
            e.setParam (bandParam (0, kGain), 0.0);
            e.setParam (bandParam (1, kGain), 0.0);
            e.setParam (kTapeOn, 1.0);
            e.setParam (kDrive, c.drive);
            e.setParam (kSpeed, c.speed);
            e.setParam (kCompression, c.comp);
            e.setParam (kOutput, 0.0);
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

TEST (tape_switch_does_not_click)
{
    auto e = engine ([] (Engine& e) { plain (e); });
    const auto x = sine (150.0, 2.0, 0.3);
    std::vector<float> y (x.size ()), r (x.size ());
    for (size_t a = 0; a < x.size (); a += 480)
    {
        e->setParam (kTapeOn, (a / 480) % 20 < 10 ? 1.0 : 0.0); // switched every 0.1 s
        e->setTransport (120.0, (double)a / kSr * 2.0, true);
        e->process (x.data () + a, x.data () + a, y.data () + a, r.data () + a, 480);
    }
    const double staticStep = 0.3 * std::pow (10.0, 13.0 / 20.0) * 2.0 * kPi * 150.0 / kSr;
    CHECK (maxStep (y, 4800) < 1.5 * staticStep, "the largest step %.4f (a steady sine at most %.4f)", maxStep (y, 4800), staticStep);
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

TEST (defaults)
{
    // a new instance: the settings the plug-in was made for (README, "Defaults")
    const auto& t = paramTable ();
    CHECK (t.info (bandParam (0, kFreq)).def == 110.0 && t.info (bandParam (0, kQ)).def == 1.0 && t.info (bandParam (0, kGain)).def == 13.0,
           "A: 110 Hz, Q 1, +13 dB");
    CHECK (t.info (bandParam (1, kFreq)).def == 320.0 && t.info (bandParam (1, kQ)).def == 1.6 && t.info (bandParam (1, kGain)).def == -15.0,
           "B: 320 Hz, Q 1.6, -15 dB");
    for (int b = 0; b < kBands; ++b)
        CHECK (t.info (bandParam (b, kLfoSync)).def == 1.0 && t.info (bandParam (b, kLfoDiv)).def == (double)kDiv4 &&
                   t.info (bandParam (b, kLfoShape)).def == (double)kSine && t.info (bandParam (b, kLfoDepth)).def == 1.0,
               "LFO %c: synced 1/4, Sine, +1 octave", 'A' + b);
    CHECK (t.info (kLink).def == 0.0 && t.info (kTapeOn).def == 1.0 && t.info (kDrive).def == 0.75 && t.info (kSpeed).def == 0.8 &&
               t.info (kCompression).def == 0.35 && t.info (kMix).def == 1.0 && t.info ((uint32_t)kTailBase + pk::kTailOn).def == 0.0,
           "Link off, Tape on, Drive 75 %%, Speed 80 %%, Compression 35 %%, Mix 100 %%, the end saturator off");
    CHECK (std::fabs (divisionBeats (kDiv4) - 1.0) < 1e-12 && std::fabs (divisionBeats (kDiv8D) - 0.75) < 1e-12 &&
               std::fabs (divisionBeats (kDiv4T) - 2.0 / 3.0) < 1e-12 && divisionBeats (kDiv4Bars) == 16.0,
           "the divisions in beats");
    // and the defaults move: the swept frequency of A covers about an octave each way within a beat
    auto e = engine ();
    double lo = 1e9, hi = 0.0;
    Engine* ep = e.get ();
    run (*e, bass (0.6), nullptr, 48, 0.0, true, [&] (size_t) {
        lo = std::min (lo, ep->bandFreqNow (0));
        hi = std::max (hi, ep->bandFreqNow (0));
    });
    CHECK (lo < 60.0 && hi > 200.0, "A swept %.0f .. %.0f Hz", lo, hi);
}

TEST (default_level)
{
    // the defaults level a bass as Scream 4's Tape does: 18 dB apart going in, a few dB apart coming out, and
    // never far above the clipper's level (-6 dBFS) less Output's 4.4 dB (a clipped wave, band-limited by the
    // oversampler and through the shelf, overshoots by a few dB)
    double out[2] {};
    for (int k = 0; k < 2; ++k)
    {
        auto e = engine ();
        const auto x = bass (4.0, k == 0 ? 0.125 : 1.0);
        const auto y = run (*e, x);
        out[k] = db (rms (y, 48000, y.size ()));
        double peak = 0.0;
        for (size_t i = 4800; i < y.size (); ++i)
            peak = std::max (peak, (double)std::fabs (y[i]));
        std::printf ("    the defaults on a bass at %+.0f dB: out %.1f dBFS RMS, peak %.1f dBFS\n", k == 0 ? -18.0 : 0.0, out[k], db (peak));
        CHECK (db (peak) < -6.0, "peak %.1f dBFS", db (peak));
    }
    CHECK (out[1] - out[0] < 6.0, "18 dB more in, %.1f dB more out", out[1] - out[0]);
}

TEST (analyze_tool_measures_the_bells)
{
    // sweepr_analyze's measurements (tools/Analyze.h) on white noise through the static bells: the delay found,
    // the response per 1/12 octave against the bells, the null depth of a signal against itself delayed
    auto e = engine (plain);
    std::vector<float> x (size_t (6.0 * kSr));
    uint32_t s = 9;
    for (auto& v : x)
    {
        s = s * 1664525u + 1013904223u;
        v = (float)(0.1 * ((int32_t)s / 2147483648.0));
    }
    const auto y = run (*e, x);
    const long d = analyze::delayOf (x, y);
    CHECK (d == e->latency (), "delay %ld, the latency %d", d, e->latency ());
    double worst = 0.0;
    for (const auto& b : analyze::response (x, y, d, kSr))
    {
        if (b.centreHz < 25.0 || b.centreHz > 18000.0)
            continue;
        const double want = db (std::abs (dsp::bellResponse (b.centreHz, 110.0, 1.0, 13.0, kSr) * dsp::bellResponse (b.centreHz, 320.0, 1.6, -15.0, kSr)));
        worst = std::max (worst, std::fabs (b.db - want));
    }
    std::printf ("    the measured response at most %.3f dB from the bells (25 Hz .. 18 kHz, 1/12 octave)\n", worst);
    CHECK (worst < 0.5, "%.2f dB", worst);
    std::vector<float> late (x.size () + 37, 0.0f);
    std::copy (x.begin (), x.end (), late.begin () + 37);
    const long d2 = analyze::delayOf (x, late);
    const auto n = analyze::nullDepth (x, late, d2);
    CHECK (d2 == 37 && n.raw < -100.0, "a copy 37 samples late: found %ld, null %.1f dB", d2, n.raw);
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
