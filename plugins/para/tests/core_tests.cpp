// Headless tests for the Para DSP. Run: ./para_tests [filter]
#include "Engine.h"
#include "Params.h"
#include "Svf.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <vector>

using namespace para;

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

static Sig tones (std::vector<std::pair<double, double>> freqDb, double secs)
{
    Sig s;
    const size_t n = (size_t)(secs * kSr);
    s.l.assign (n, 0.0f);
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

static std::unique_ptr<Engine> engine ()
{
    auto e = std::make_unique<Engine> ();
    e->prepare (kSr, 512);
    return e;
}

// Gain of the device at f (dB) with the current settings, measured over the second half of a second.
static double gainAt (Engine& e, double f)
{
    e.reset ();
    auto out = run (e, tones ({{f, -12.0}}, 1.0));
    return toneDb (out.l, f, 24000, 48000) + 12.0;
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
    CHECK (t.toText (kSplit, 7.0) == "7.0 st", "%s", t.toText (kSplit, 7.0).c_str ());
    CHECK (t.toText (kRoot, 60.0) == "C3", "%s", t.toText (kRoot, 60.0).c_str ());
    double parsed = 0;
    CHECK (t.fromText (kRoot, "F#2", parsed) && parsed == 54.0, "parse F#2: %f", parsed);
    CHECK (t.fromText (kRoot, "c-2", parsed) && parsed == 0.0, "parse C-2: %f", parsed);
    CHECK (std::fabs (resonanceToQ (0.0) - 0.5) < 1e-9 && std::fabs (resonanceToQ (1.0) - 10.0) < 1e-9, "Q range");
    CHECK (std::fabs (resonanceToQ (0.0, kSlope24) - M_SQRT1_2) < 1e-9 && resonanceToQ (0.0, kSlope18) == 1.0, "base Q");
    CHECK (std::fabs (t.info (kHpFreq).def - 300.0) < 1e-9 && std::fabs (t.info (kLpFreq).def - 100.0) < 1e-9 &&
               t.info (kSlope).def == (double)kSlope24, "defaults: HP 300 Hz, LP 100 Hz, 24 dB");
    CHECK (t.info (kDragGain).def == 0.0 && kHostedParams == kTailBase + pk::kTailFields, "Drag Gain off, after the hosted IDs");
}

TEST (filters_meeting_sum_flat)
{
    // high-pass and low-pass at the same cutoff with resonance 0 sum to a flat response, at every slope
    for (int slope = 0; slope < kNumSlopes; ++slope)
    {
        auto e = engine ();
        e->setParam (kSlope, slope);
        e->setParam (kHpFreq, 1000.0);
        e->setParam (kLpFreq, 1000.0);
        for (double f : {60.0, 300.0, 1000.0, 3000.0, 12000.0})
            CHECK (std::fabs (gainAt (*e, f)) < 0.3, "slope %d at %.0f Hz: %f dB", slope, f, gainAt (*e, f));
    }
}

// The display's model of a filter at f: the analog response at the frequency the bilinear transform
// warps f to (as FilterView draws it), in dB.
static double modelDb (int slope, bool hp, double f, double fc, double res, double sr = kSr)
{
    const double c = std::clamp (fc, 5.0, 0.49 * sr);
    const double fw = c * std::tan (M_PI * f / sr) / std::tan (M_PI * c / sr);
    return 20.0 * std::log10 (std::max (1e-15, std::abs (filterResponse (slope, hp, fw, c, res))));
}

// One filter alone (the other at -inf) at fc, resonance res.
static std::unique_ptr<Engine> single (int slope, bool hp, double fc, double res)
{
    auto e = engine ();
    e->setParam (kSlope, slope);
    e->setParam (hp ? kHpFreq : kLpFreq, fc);
    e->setParam (hp ? kHpRes : kLpRes, res);
    e->setParam (hp ? kLpGain : kHpGain, kGainMinDb);
    e->reset ();
    return e;
}

TEST (slopes)
{
    const auto& t = paramTable ();
    const auto& info = t.info (kSlope);
    CHECK (info.choices.size () == (size_t)kNumSlopes && info.def == (double)kSlope24, "%zu slopes, 24 dB by default", info.choices.size ());
    const char* names[kNumSlopes] = {"6 dB", "12 dB", "18 dB", "24 dB", "36 dB", "48 dB", "60 dB", "72 dB", "84 dB", "96 dB", "Brickwall"};
    for (int s = 0; s < kNumSlopes; ++s)
        CHECK (t.toText (kSlope, s) == names[s], "%d: %s", s, t.toText (kSlope, s).c_str ());
    // the model's slope far from the cutoff is the slope's (dB per octave), both filters
    const int dbPerOct[kNumSlopes - 1] = {6, 12, 18, 24, 36, 48, 60, 72, 84, 96};
    for (int s = 0; s < kNumSlopes - 1; ++s)
    {
        const double hp = modelDb (s, true, 1.0, 1000.0, 0.0, 1e9) - modelDb (s, true, 0.5, 1000.0, 0.0, 1e9);
        const double lp = modelDb (s, false, 1e6, 1000.0, 0.0, 1e9) - modelDb (s, false, 2e6, 1000.0, 0.0, 1e9);
        CHECK (std::fabs (hp - dbPerOct[s]) < 0.01 && std::fabs (lp - dbPerOct[s]) < 0.01, "%s: %f / %f dB per octave", names[s], hp, lp);
    }
    // resonance 0: -3 dB at the cutoff (first order, 18 dB, Brickwall) or -6 (Linkwitz-Riley); the handle's peak
    for (int s = 0; s < kNumSlopes; ++s)
    {
        const bool lr = s != kSlope6 && s != kSlope18 && s != kSlopeBrickwall;
        const double at = modelDb (s, true, 1000.0, 1000.0, 0.0);
        CHECK (std::fabs (at - (lr ? -6.02 : -3.01)) < 0.02, "%s at the cutoff: %f dB", names[s], at);
        // full resonance: a peak at the cutoff, as high as 24 dB's from 24 dB on (not a power of it)
        const double peak = modelDb (s, true, 1000.0, 1000.0, 1.0);
        const double expect = s == kSlope12 ? 20.0 : (s == kSlope18 || !lr ? 23.0 : 46.0);
        CHECK (std::fabs (peak - expect) < 0.1, "%s, resonance 1: %f dB at the cutoff", names[s], peak);
    }
}

TEST (slopes_measured_match_the_model)
{
    // each filter alone, at each slope, at two resonances: what comes out is what the display draws,
    // down to -120 dB
    const double fc = 1000.0;
    for (int s = 0; s < kNumSlopes; ++s)
        for (bool hp : {true, false})
            for (double res : {0.0, 0.6})
            {
                auto e = single (s, hp, fc, res);
                double worst = 0.0, worstF = 0.0;
                for (double f : {62.5, 125.0, 250.0, 500.0, 707.0, 800.0, 900.0, 950.0, 1000.0, 1050.0, 1111.0, 1250.0, 1414.0, 2000.0,
                                 4000.0, 8000.0, 16000.0})
                {
                    const double model = modelDb (s, hp, f, fc, res);
                    if (model < -120.0)
                        continue;
                    const double d = std::fabs (gainAt (*e, f) - model);
                    if (d > worst)
                    {
                        worst = d;
                        worstF = f;
                    }
                }
                CHECK (worst < (res > 0.0 ? 0.3 : 0.2), "slope %d %s res %.1f: %f dB off the model at %.0f Hz", s, hp ? "HP" : "LP", res,
                       worst, worstF);
            }
}

TEST (slopes_measured_db_per_octave)
{
    // where each high-pass is 60 dB down (6 and 12 dB: 40), the measured slope over a quarter octave
    // is the slope's dB per octave
    const int dbPerOct[kNumSlopes - 1] = {6, 12, 18, 24, 36, 48, 60, 72, 84, 96};
    for (int s = 0; s < kNumSlopes - 1; ++s)
    {
        const double fc = s <= kSlope12 ? 8000.0 : 2000.0, target = s <= kSlope12 ? -40.0 : -60.0;
        double lo = 1.0, hi = fc; // the frequency where the model is at target
        for (int k = 0; k < 60; ++k)
        {
            const double mid = std::sqrt (lo * hi);
            (modelDb (s, true, mid, fc, 0.0) < target ? lo : hi) = mid;
        }
        auto e = single (s, true, fc, 0.0);
        const double f = lo, f2 = f * std::pow (2.0, 0.25);
        const double measured = 4.0 * (gainAt (*e, f2) - gainAt (*e, f));
        CHECK (std::fabs (measured - dbPerOct[s]) < 0.05 * dbPerOct[s], "slope %d: %.2f dB per octave at %.0f Hz (%d)", s, measured, f,
               dbPerOct[s]);
    }
}

TEST (brickwall_is_steep)
{
    // -3 dB at the cutoff, 40 dB down a tenth of the way past it, 75 or more from 0.8 x (1.25 x) on;
    // far steeper there than 96 dB
    const double fc = 2000.0;
    auto hp = single (kSlopeBrickwall, true, fc, 0.0), lp = single (kSlopeBrickwall, false, fc, 0.0);
    CHECK (std::fabs (gainAt (*hp, fc) + 3.01) < 0.2 && std::fabs (gainAt (*lp, fc) + 3.01) < 0.2, "at the cutoff: %f / %f",
           gainAt (*hp, fc), gainAt (*lp, fc));
    CHECK (gainAt (*hp, 0.9 * fc) < -38.0 && gainAt (*lp, fc / 0.9) < -38.0, "a tenth past: %f / %f", gainAt (*hp, 0.9 * fc),
           gainAt (*lp, fc / 0.9));
    for (double x : {0.8, 0.7, 0.5, 0.25, 0.1})
        CHECK (gainAt (*hp, x * fc) < -75.0 && gainAt (*lp, fc / x) < -75.0, "%.2f x: %f / %f", x, gainAt (*hp, x * fc), gainAt (*lp, fc / x));
    // the pass band is flat right next to the cutoff
    CHECK (std::fabs (gainAt (*hp, 1.15 * fc)) < 0.1 && std::fabs (gainAt (*hp, 4.0 * fc)) < 0.05 && std::fabs (gainAt (*lp, 0.87 * fc)) < 0.1,
           "pass band: %f / %f / %f", gainAt (*hp, 1.15 * fc), gainAt (*hp, 4.0 * fc), gainAt (*lp, 0.87 * fc));
    auto steep = single (kSlope96, true, fc, 0.0);
    CHECK (gainAt (*hp, 0.8 * fc) < gainAt (*steep, 0.8 * fc) - 30.0, "steeper than 96 dB: %f vs %f", gainAt (*hp, 0.8 * fc),
           gainAt (*steep, 0.8 * fc));
    // resonance: a bell at the cutoff
    auto res = single (kSlopeBrickwall, true, fc, 1.0);
    CHECK (gainAt (*res, 1.02 * fc) > 15.0 && gainAt (*res, 0.8 * fc) < -50.0, "resonance: %f at the edge, %f below",
           gainAt (*res, 1.02 * fc), gainAt (*res, 0.8 * fc));
}

// The largest step between neighbouring samples from sample `from` on.
static double largestStep (const std::vector<float>& x, size_t from)
{
    double step = 0.0;
    for (size_t i = from + 1; i < x.size (); ++i)
        step = std::max (step, (double)std::fabs (x[i] - x[i - 1]));
    return step;
}

TEST (moving_cutoffs_stay_stable_and_smooth)
{
    // Split swept +-24 semitones five times a second (and the resonance with it), block by block as
    // automation would, at the steepest slopes: the output stays bounded and moves no faster than the
    // input does (no clicks); the same with Vocal sweeping the low-pass through the high-pass
    auto in = tones ({{150.0, -6.0}, {1000.0, -12.0}, {5000.0, -18.0}}, 2.0);
    const double inStep = largestStep (in.l, 0);
    for (int s : {kSlope60, kSlope84, kSlope96, kSlopeBrickwall, kSlope6})
        for (double res : {0.0, 0.5})
            for (int mode : {kFree, kVocal})
            {
                auto e = engine ();
                e->setParam (kSlope, s);
                e->setParam (kHpRes, res);
                e->setParam (kLpRes, res);
                e->setParam (kMovement, mode);
                e->reset ();
                Sig out;
                out.l.resize (in.l.size ());
                out.r.resize (in.r.size ());
                for (size_t pos = 0, k = 0; pos < in.l.size (); pos += 64, ++k)
                {
                    const double ph = 2.0 * M_PI * 5.0 * (double)pos / kSr;
                    if (mode == kFree)
                        e->setParam (kSplit, 24.0 * std::sin (ph));
                    else // the low-pass swept through the high-pass and back
                        e->setParam (kLpFreq, 100.0 * std::pow (2.0, 3.0 + 3.0 * std::sin (ph)));
                    e->process (in.l.data () + pos, in.r.data () + pos, out.l.data () + pos, out.r.data () + pos, 64);
                }
                bool finite = true;
                double peak = 0.0;
                for (float v : out.l)
                {
                    finite &= std::isfinite (v);
                    peak = std::max (peak, (double)std::fabs (v));
                }
                const double step = largestStep (out.l, 2400);
                CHECK (finite && peak < (res > 0.0 ? 8.0 : 2.5), "slope %d res %.1f %s: peak %f", s, res, mode == kFree ? "Free" : "Vocal", peak);
                CHECK (step < (res > 0.0 ? 4.0 : 1.6) * inStep, "slope %d res %.1f %s: largest step %f (the input's %f)", s, res,
                       mode == kFree ? "Free" : "Vocal", step, inStep);
            }
}

TEST (slope_changes_do_not_click)
{
    // every slope in turn, a new one every 50 ms, the filters meeting (they sum flat at every slope):
    // the 10 ms crossfades never jump
    auto e = engine ();
    e->setParam (kHpFreq, 700.0);
    e->setParam (kLpFreq, 700.0);
    e->reset ();
    auto in = tones ({{200.0, -6.0}, {3000.0, -12.0}}, 2.0);
    Sig out;
    out.l.resize (in.l.size ());
    out.r.resize (in.r.size ());
    for (size_t pos = 0, k = 0; pos < in.l.size (); pos += 240, ++k)
    {
        if (k % 10 == 0)
            e->setParam (kSlope, (double)((k / 10 * 7) % kNumSlopes)); // jumping around the list
        e->process (in.l.data () + pos, in.r.data () + pos, out.l.data () + pos, out.r.data () + pos, 240);
    }
    const double step = largestStep (out.l, 4800), inStep = largestStep (in.l, 0);
    CHECK (step < 1.3 * inStep, "largest step %f (the input's %f)", step, inStep);
    // the level stays (all of them sum flat): no dips around the changes
    double lowest = 1e9;
    for (size_t a = 4800; a + 480 <= out.l.size (); a += 480)
        lowest = std::min (lowest, toneDb (out.l, 200.0, a, a + 480));
    CHECK (lowest > -6.0 - 1.5, "the 200 Hz tone stays: lowest %f dB", lowest);
}

TEST (notch_between_the_filters)
{
    // a high-pass at 800 Hz above a low-pass at 200 Hz leaves a notch around 400 Hz
    auto e = engine ();
    e->setParam (kHpFreq, 800.0);
    e->setParam (kLpFreq, 200.0);
    CHECK (gainAt (*e, 400.0) < -8.0, "notch: %f dB", gainAt (*e, 400.0));
    CHECK (std::fabs (gainAt (*e, 40.0)) < 0.5 && std::fabs (gainAt (*e, 8000.0)) < 0.5, "outside: %f / %f",
           gainAt (*e, 40.0), gainAt (*e, 8000.0));
    e->setParam (kSlope, kSlope12);
    const double d12 = gainAt (*e, 400.0);
    e->setParam (kSlope, kSlope18);
    const double d18 = gainAt (*e, 400.0);
    e->setParam (kSlope, kSlope24);
    CHECK (d18 < d12 - 3.0 && gainAt (*e, 400.0) < d18 - 3.0, "steeper is deeper: %f / %f / %f", d12, d18, gainAt (*e, 400.0));
    // resonance lifts the edges (12 dB, where the notch settings below were measured)
    e->setParam (kSlope, kSlope12);
    e->setParam (kHpRes, 0.8);
    CHECK (gainAt (*e, 800.0) > 3.0, "resonance at the high-pass corner: %f", gainAt (*e, 800.0));
    e->setParam (kHpRes, 0.0);
    // Split moves them: -12 semitones closes the notch, +12 widens it
    e->setParam (kSplit, -24.0);
    CHECK (gainAt (*e, 400.0) > -1.5, "closed: %f", gainAt (*e, 400.0));
    e->setParam (kSplit, 12.0);
    CHECK (gainAt (*e, 400.0) < d12 - 6.0, "wider: %f", gainAt (*e, 400.0));
}

TEST (notes_do_not_move_the_filters)
{
    // the notes only trigger the envelope: whatever the note, transpose, bend or root, the cutoffs stay
    auto e = engine (), fresh = engine ();
    for (auto* x : {e.get (), fresh.get ()})
    {
        x->setParam (kHpFreq, 800.0);
        x->setParam (kLpFreq, 200.0);
    }
    e->setParam (kTranspose, 12.0);
    e->setParam (kPbRange, 12.0);
    e->setParam (kRoot, 48.0);
    e->setPitchBend (1.0f);
    e->noteOn (84);
    for (double f : {100.0, 400.0, 1600.0})
        CHECK (std::fabs (gainAt (*e, f) - gainAt (*fresh, f)) < 0.3, "%.0f Hz: %f vs %f dB", f, gainAt (*e, f), gainAt (*fresh, f));
}

TEST (envelope_splits_on_note_on)
{
    // +24 semitones of envelope widen the notch right after a note, then it recovers
    auto e = engine ();
    e->setParam (kEnvAmount, 24.0);
    e->setParam (kEnvAttack, 1.0);
    e->setParam (kEnvDecay, 100.0);
    e->reset ();
    auto in = tones ({{400.0, -12.0}}, 1.5);
    Sig out;
    out.l.resize (in.l.size ());
    out.r.resize (in.r.size ());
    e->noteOn (60);
    for (size_t pos = 0; pos < in.l.size (); pos += 480)
        e->process (in.l.data () + pos, in.r.data () + pos, out.l.data () + pos, out.r.data () + pos, 480);
    const double early = toneDb (out.l, 400.0, 480, 2400) + 12.0, late = toneDb (out.l, 400.0, 48000, 72000) + 12.0;
    CHECK (early < late - 6.0, "envelope widens the notch: %f early, %f late", early, late);
    CHECK (std::fabs (late - gainAt (*engine (), 400.0)) < 0.5, "recovered: %f", late);
    CHECK (e->envLevel () < 0.01, "envelope decayed: %f", e->envLevel ());
}

TEST (filter_gains_down_to_minus_inf)
{
    // with the high-pass at -inf only the low-pass is heard, and the other way round
    auto e = engine ();
    e->setParam (kHpFreq, 822.0);
    e->setParam (kLpFreq, 185.0);
    e->setParam (kHpGain, kGainMinDb);
    CHECK (gainAt (*e, 5000.0) < -40.0 && std::fabs (gainAt (*e, 60.0)) < 0.5, "HP -inf: %f / %f", gainAt (*e, 5000.0), gainAt (*e, 60.0));
    e->setParam (kHpGain, 0.0);
    e->setParam (kLpGain, kGainMinDb);
    CHECK (gainAt (*e, 40.0) < -40.0 && std::fabs (gainAt (*e, 8000.0)) < 0.5, "LP -inf: %f / %f", gainAt (*e, 40.0), gainAt (*e, 8000.0));
    e->setParam (kLpGain, -6.0);
    CHECK (std::fabs (gainAt (*e, 40.0) + 6.0) < 0.3, "LP -6 dB: %f", gainAt (*e, 40.0));
    CHECK (paramTable ().toText (kHpGain, kGainMinDb) == "-inf dB", "%s", paramTable ().toText (kHpGain, kGainMinDb).c_str ());
}

TEST (linked_resonance)
{
    // linked, the low-pass takes the high-pass resonance
    auto e = engine ();
    e->setParam (kHpFreq, 822.0);
    e->setParam (kLpFreq, 185.0);
    e->setParam (kSlope, kSlope12);
    const double flat = gainAt (*e, 185.0);
    e->setParam (kHpRes, 0.8);
    CHECK (std::fabs (gainAt (*e, 185.0) - flat) < 0.5, "unlinked: the low-pass corner is unchanged");
    e->setParam (kResLink, 1.0);
    CHECK (gainAt (*e, 185.0) > flat + 3.0, "linked: the low-pass resonates too: %f vs %f", gainAt (*e, 185.0), flat);
}

TEST (vocal_movement)
{
    auto sweep = [] (Engine& e, uint32_t id, double from, double to) {
        // move one cutoff in steps, processing in between, as automation would
        std::vector<float> l (480, 0.0f), r (480, 0.0f);
        for (int k = 0; k <= 20; ++k)
        {
            e.setParam (id, from * std::pow (to / from, k / 20.0));
            e.process (l.data (), r.data (), l.data (), r.data (), 480);
        }
    };
    // the low-pass swept up past the high-pass: free, the high-pass still passes the top;
    // vocal, it was pushed along and faded out, so only the low-pass is heard
    for (int mode : {kFree, kVocal})
    {
        auto e = engine ();
        e->setParam (kMovement, mode);
        sweep (*e, kLpFreq, 185.0, 3000.0);
        const double top = gainAt (*e, 10000.0);
        if (mode == kFree)
            CHECK (top > -1.0, "free: the high-pass passes 10 kHz: %f", top);
        else
            CHECK (top < -12.0, "vocal: the high-pass faded, the low-pass cuts 10 kHz: %f", top);
        if (mode == kVocal) // free, the crossed filters partly cancel between their cutoffs (that is what Vocal avoids)
            CHECK (std::fabs (gainAt (*e, 1000.0)) < 1.5, "vocal: 1 kHz passes: %f", gainAt (*e, 1000.0));
    }
    // a high-pass swept down past the low-pass: vocal fades the low-pass out
    {
        auto e = engine ();
        e->setParam (kMovement, kVocal);
        e->setParam (kLpFreq, 3000.0);
        sweep (*e, kHpFreq, 822.0, 100.0);
        CHECK (gainAt (*e, 30.0) < -12.0 && std::fabs (gainAt (*e, 1000.0)) < 1.5, "vocal, HP leads: %f / %f", gainAt (*e, 30.0),
               gainAt (*e, 1000.0));
        e->setParam (kMovement, kFree);
        CHECK (gainAt (*e, 30.0) > -1.0, "free: the low-pass passes 30 Hz: %f", gainAt (*e, 30.0));
    }
}

TEST (meters_for_the_display)
{
    // the engine reports how far it moves the filters from their settings (here Split: +6 moves the
    // high-pass up 3 semitones and the low-pass down 3), so the display can add that to the settings
    Meters m;
    auto e = engine ();
    e->setMeters (&m);
    e->setParam (kHpFreq, 822.0);
    e->setParam (kSplit, 6.0);
    run (*e, tones ({{440.0, -12.0}}, 0.1));
    CHECK (m.blocks.load () > 0, "blocks counted");
    CHECK (std::fabs (m.hpShift.load () - 3.0) < 0.05 && std::fabs (m.lpShift.load () + 3.0) < 0.05, "shift %f / %f",
           m.hpShift.load (), m.lpShift.load ());
    CHECK (std::fabs (822.0 * std::pow (2.0, m.hpShift.load () / 12.0) - m.hpHz.load ()) < 1.0, "shift matches the cutoff");
    // Vocal: the low-pass swept above the high-pass pushes it; the display's push is the engine's
    e->setParam (kSplit, 0.0);
    e->setParam (kMovement, kVocal);
    e->setParam (kLpFreq, 900.0);
    run (*e, tones ({{440.0, -12.0}}, 0.1));
    double hz = 822.0, lz = 900.0;
    float hm, lm;
    vocalPush (hz, lz, m.leaderLp.load (), 12.0, 80.0, hm, lm);
    CHECK (m.leaderLp.load () && std::fabs (hz - m.hpHz.load ()) < 1.0 && std::fabs (hm - m.hpMul.load ()) < 0.01,
           "vocal push: %f vs %f, fade %f vs %f", hz, m.hpHz.load (), hm, m.hpMul.load ());
}

TEST (vocal_dip_starts_at_dip_start)
{
    // the high-pass at 300 Hz (the default), the low-pass swept up to 200 Hz (more than an octave past
    // the 80 Hz Dip Start, more than the default Fade): in Vocal the high-pass is gone, so nothing is
    // left at 8 kHz; in Free it still passes
    for (int mode : {kFree, kVocal})
    {
        auto e = engine ();
        e->setParam (kMovement, mode);
        std::vector<float> l (480, 0.0f), r (480, 0.0f);
        for (int k = 0; k <= 20; ++k)
        {
            e->setParam (kLpFreq, 60.0 * std::pow (200.0 / 60.0, k / 20.0));
            e->process (l.data (), r.data (), l.data (), r.data (), 480);
        }
        for (int k = 0; k < 100; ++k) // the swing settles
            e->process (l.data (), r.data (), l.data (), r.data (), 480);
        const double top = gainAt (*e, 8000.0);
        if (mode == kVocal)
            CHECK (top < -40.0, "vocal: no high band left at 8 kHz: %f dB", top);
        else
            CHECK (top > -1.0, "free: the high-pass passes 8 kHz: %f dB", top);
    }
    // the dip starts at Dip Start and dives over Fade: below 80 Hz the high band is untouched, half an
    // octave past it (113 Hz) it is 3 dB down, with Fade at 3 it is gone a minor third past (95 Hz);
    // the high-pass rises along with the low-pass
    auto at = [] (double lp, double fade, double dip, double& hpHz) {
        Meters m;
        auto e = engine ();
        e->setMeters (&m);
        e->setParam (kMovement, kVocal);
        e->setParam (kFade, fade);
        e->setParam (kDipStart, dip);
        e->setParam (kLpFreq, lp);
        const double g = gainAt (*e, 8000.0);
        hpHz = m.hpHz.load ();
        return g;
    };
    double hz = 0.0;
    CHECK (at (75.0, 12.0, 80.0, hz) > -0.5 && std::fabs (hz - 300.0) < 1.0, "below Dip: untouched (%f dB, HP %.0f Hz)",
           at (75.0, 12.0, 80.0, hz), hz);
    const double half = at (113.1, 12.0, 80.0, hz);
    CHECK (half < -1.5 && half > -5.0, "half way, equal-power: %f dB (-3 expected)", half);
    CHECK (std::fabs (12.0 * std::log2 (hz / 300.0) - 6.0) < 0.2, "the high-pass rose with it: %.0f Hz", hz);
    CHECK (at (95.1, 3.0, 80.0, hz) < -40.0, "Fade 3: gone a minor third past Dip (%f dB)", at (95.1, 3.0, 80.0, hz));
    CHECK (at (113.1, 12.0, 200.0, hz) > -0.5, "Dip Start at 200 Hz: not yet (%f dB)", at (113.1, 12.0, 200.0, hz));
    CHECK (std::fabs (paramTable ().info (kDipStart).def - 80.0) < 1e-9, "80 Hz by default");
}

TEST (vocal_overshoots_and_flows_back)
{
    // the same sweep of the low-pass in Free and in Vocal: Vocal runs ahead while it moves (Split swings
    // with the sweep) and settles on the same cutoff after it stops
    Meters mf, mv;
    auto ef = engine (), ev = engine ();
    ef->setMeters (&mf);
    ev->setMeters (&mv);
    ev->setParam (kMovement, kVocal);
    std::vector<float> l (480, 0.0f), r (480, 0.0f);
    for (int k = 0; k <= 20; ++k) // 185 Hz to 1.5 kHz in 0.2 s
    {
        const double f = 185.0 * std::pow (1500.0 / 185.0, k / 20.0);
        ef->setParam (kLpFreq, f);
        ev->setParam (kLpFreq, f);
        ef->process (l.data (), r.data (), l.data (), r.data (), 480);
        ev->process (l.data (), r.data (), l.data (), r.data (), 480);
    }
    const double ahead = 12.0 * std::log2 (mv.lpHz.load () / mf.lpHz.load ());
    CHECK (ahead > 2.0, "Vocal runs ahead of the sweep: %.1f semitones", ahead);
    for (int k = 0; k < 150; ++k) // 1.5 s at rest
    {
        ef->process (l.data (), r.data (), l.data (), r.data (), 480);
        ev->process (l.data (), r.data (), l.data (), r.data (), 480);
    }
    const double settled = 12.0 * std::log2 (mv.lpHz.load () / mf.lpHz.load ());
    CHECK (std::fabs (settled) < 0.2, "and settles where Free does: %.2f semitones", settled);
}

TEST (low_pass_floor_keeps_the_sub)
{
    // Split +36 takes the low-pass down 18 semitones from 100 Hz (to 35 Hz): the floor holds it at 40 Hz
    auto lpAt = [] (double floorHz) {
        Meters m;
        auto e = engine ();
        e->setMeters (&m);
        e->setParam (kLpFreq, 100.0);
        e->setParam (kSplit, 36.0);
        e->setParam (kLpFloor, floorHz);
        run (*e, tones ({{440.0, -12.0}}, 0.1));
        return (double)m.lpHz.load ();
    };
    CHECK (std::fabs (lpAt (40.0) - 40.0) < 0.5, "held at the 40 Hz floor: %.1f Hz", lpAt (40.0));
    CHECK (std::fabs (lpAt (20.0) - 35.4) < 0.5, "a lower floor lets it go: %.1f Hz", lpAt (20.0));
    CHECK (std::fabs (paramTable ().info (kLpFloor).def - 40.0) < 1e-9, "40 Hz by default");
}

TEST (dry_wet_and_output)
{
    auto e = engine ();
    e->setParam (kDryWet, 0.0);
    CHECK (std::fabs (gainAt (*e, 400.0)) < 0.05, "dry: %f", gainAt (*e, 400.0));
    e->setParam (kOutput, -6.0);
    CHECK (std::fabs (gainAt (*e, 400.0) + 6.0) < 0.05, "output: %f", gainAt (*e, 400.0));
}

// Where the biggest sample of the left channel is.
static size_t peakAt (const std::vector<float>& x)
{
    size_t k = 0;
    for (size_t i = 1; i < x.size (); ++i)
        if (std::fabs (x[i]) > std::fabs (x[k]))
            k = i;
    return k;
}

TEST (drive_params)
{
    const auto& t = paramTable ();
    for (uint32_t id = 0; id < kNumParams; ++id)
        CHECK (t.info (id).id == id, "entry %u has ID %u", id, t.info (id).id);
    CHECK (kHpDriveOn == kTailExtBase + 17 && kTailExt2Base == kHpDriveOn + 3, "the drive's IDs follow the end saturator's block");
    CHECK (kLpDriveOn == kTailExt2Base + pk::kTailExt2Fields && kLpDrive == kLpDriveOn + 1 && kNumParams == kLpDrive + 1 &&
               std::string (t.info (kTailExt2Base + pk::kTailExt2Advanced).name) == "Saturator Gently Advanced" &&
               t.info (kTailExt2Base + pk::kTailExt2Threshold).def == -18.0 && t.info (kTailExt2Base + pk::kTailExt2Advanced).def == 0.0,
           "Gently's Advanced block (the end saturator's), then the low-pass drive, the last");
    CHECK (std::string (t.info (kHpDriveOn).name) == "High-Pass Drive On" && std::string (t.info (kHpDrive).name) == "High-Pass Drive" &&
               std::string (t.info (kLpDriveOn).name) == "Low-Pass Drive On" && std::string (t.info (kLpDrive).name) == "Low-Pass Drive",
           "one drive per filter");
    // off, 0 dB and Pre are normalized 0, so a value never stored (a rack slot from before) is the default
    for (uint32_t id : {kHpDriveOn, kHpDrive, kDrivePos, kLpDriveOn, kLpDrive})
        CHECK (t.defaultNormalized (id) == 0.0, "%s: default %f", t.info (id).name, t.defaultNormalized (id));
    CHECK (t.toText (kDrivePos, kDrivePost) == "Post", "%s", t.toText (kDrivePos, kDrivePost).c_str ());
    CHECK (t.info (kHpDrive).max == 36.0 && t.info (kLpDrive).max == 36.0, "+36 dB");
}

TEST (drive_latency_is_the_same_everywhere)
{
    // an impulse through the dry path comes out at latency (), with the drive off or on (at 0 dB and
    // below the curve's knee it is linear), Pre or Post: the delay never moves
    Engine probe;
    probe.prepare (kSr, 512);
    const int lat = probe.latency ();
    CHECK (lat > 0, "the oversampled drive has a latency: %d", lat);
    for (double sr : {44100.0, 96000.0})
    {
        Engine a, b;
        a.prepare (sr, 512);
        b.prepare (sr, 512);
        b.setParam (kHpDriveOn, 1.0);
        b.setParam (kLpDriveOn, 1.0);
        b.setParam (kDrivePos, kDrivePost);
        CHECK (a.latency () == b.latency (), "%.0f Hz: %d vs %d", sr, a.latency (), b.latency ());
    }
    for (int on = 0; on < 2; ++on)
        for (int pos : {kDrivePre, kDrivePost})
        {
            auto e = engine ();
            e->setParam (kDryWet, 0.0);
            e->setParam (kHpDriveOn, on);
            e->setParam (kLpDriveOn, on);
            e->setParam (kDrivePos, pos);
            e->reset ();
            CHECK (e->latency () == lat, "latency %d", e->latency ());
            Sig in;
            in.l.assign (4096, 0.0f);
            in.l[100] = 0.1f;
            in.r = in.l;
            auto out = run (*e, in);
            const size_t k = peakAt (out.l);
            CHECK (k == (size_t)(100 + lat), "on %d, %s: the impulse at %zu, expected %d", on, pos ? "Post" : "Pre", k, 100 + lat);
            if (!on)
                CHECK (out.l[k] == 0.1f, "off: exactly the input, delayed (%.9f)", out.l[k]);
        }
    // the same with blocks larger than the prepared size (the engine splits them)
    auto e = engine (), f = engine ();
    for (auto* x : {e.get (), f.get ()})
    {
        x->setParam (kHpDriveOn, 1.0);
        x->setParam (kLpDriveOn, 1.0);
        x->setParam (kHpDrive, 12.0);
        x->setParam (kLpDrive, 12.0);
        x->reset ();
    }
    auto in = tones ({{110.0, -6.0}, {3000.0, -12.0}}, 0.3);
    auto small = run (*e, in, 512), large = run (*f, in, 2000);
    double d = 0.0;
    for (size_t i = 0; i < small.l.size (); ++i)
        d = std::max (d, (double)std::fabs (small.l[i] - large.l[i]));
    CHECK (d < 1e-6, "blocks of 2000 after prepare (512): %g", d);
}

TEST (drive_off_leaves_the_sound)
{
    // off, the drive only delays: with the filters set (static), Pre and Post come out the same, and
    // the gains the other tests measure are unchanged (Pre and Post, off, against the defaults: Pre).
    // The very same run was checked against Para from before the drive: bit-exact, latency () later.
    auto pre = engine (), post = engine ();
    for (auto* x : {pre.get (), post.get ()})
    {
        x->setParam (kHpFreq, 800.0);
        x->setParam (kLpFreq, 200.0);
        x->setParam (kHpRes, 0.6);
        x->setParam (kDryWet, 0.7);
        x->reset ();
    }
    post->setParam (kDrivePos, kDrivePost);
    post->reset ();
    auto in = tones ({{55.0, -6.0}, {440.0, -12.0}, {3000.0, -18.0}}, 0.5);
    auto a = run (*pre, in), b = run (*post, in, 333);
    double d = 0.0;
    for (size_t i = 0; i < a.l.size (); ++i)
        d = std::max (d, (double)std::fabs (a.l[i] - b.l[i]));
    CHECK (d < 1e-6, "off: Pre and Post differ by %g", d);
    CHECK (std::fabs (gainAt (*post, 400.0) - gainAt (*pre, 400.0)) < 1e-3, "notch: %f vs %f", gainAt (*post, 400.0), gainAt (*pre, 400.0));
    // Drive on at 0 dB with a quiet signal (below the curve's knee): the same as off, but for the
    // oversampling filters' ripple
    auto on = engine (), off = engine ();
    on->setParam (kHpDriveOn, 1.0);
    on->setParam (kLpDriveOn, 1.0);
    on->reset ();
    auto quiet = tones ({{220.0, -30.0}, {2000.0, -36.0}}, 0.3);
    auto x = run (*on, quiet), y = run (*off, quiet);
    d = 0.0;
    for (size_t i = 4800; i < x.l.size (); ++i)
        d = std::max (d, (double)std::fabs (x.l[i] - y.l[i]));
    CHECK (d < 1e-3 * 0.03, "on at 0 dB, below the knee: %g off", d);
}

TEST (drive_adds_harmonics)
{
    // the filters meeting at 1 kHz sum flat; a 100 Hz sine at -6 dB through the drive at +12 dB gets
    // the Analog curve's odd harmonics (it is symmetric: no even ones)
    auto h3 = [] (bool on, double db, double& h2) {
        auto e = engine ();
        e->setParam (kHpFreq, 1000.0);
        e->setParam (kLpFreq, 1000.0);
        e->setParam (kHpDriveOn, on ? 1.0 : 0.0);
        e->setParam (kLpDriveOn, on ? 1.0 : 0.0);
        e->setParam (kHpDrive, db);
        e->setParam (kLpDrive, db);
        e->reset ();
        auto out = run (*e, tones ({{100.0, -6.0}}, 0.5));
        h2 = toneDb (out.l, 200.0, 12000, 24000);
        return toneDb (out.l, 300.0, 12000, 24000);
    };
    double even = 0.0;
    const double off = h3 (false, 12.0, even), on = h3 (true, 12.0, even);
    CHECK (off < -80.0, "off: no third harmonic (%f dB)", off);
    CHECK (on > -30.0, "on, +12 dB: third harmonic at %f dB", on);
    CHECK (even < on - 40.0, "no even harmonics: %f dB", even);
    CHECK (h3 (true, 24.0, even) > on, "more drive, more harmonics: %f vs %f", h3 (true, 24.0, even), on);
    // the level stays bounded: the curve tops out at 1
    auto e = engine ();
    e->setParam (kHpFreq, 1000.0);
    e->setParam (kLpFreq, 1000.0);
    e->setParam (kHpDriveOn, 1.0);
    e->setParam (kLpDriveOn, 1.0);
    e->setParam (kHpDrive, 36.0);
    e->setParam (kLpDrive, 36.0);
    e->setParam (kDrivePos, kDrivePost);
    auto out = run (*e, tones ({{100.0, 0.0}}, 0.3));
    CHECK (std::fabs (out.l[peakAt (out.l)]) < 1.1f, "peak %f", out.l[peakAt (out.l)]);
}

TEST (drive_pre_and_post)
{
    // only the low-pass, at 500 Hz, and a 200 Hz sine driven hard. Pre: the harmonics are made before
    // the low-pass, which takes the ones above 500 Hz away. Post: they are made after it and stay.
    auto level = [] (int pos, double f) {
        auto e = engine ();
        e->setParam (kHpGain, kGainMinDb);
        e->setParam (kLpFreq, 500.0);
        e->setParam (kHpDriveOn, 1.0);
        e->setParam (kLpDriveOn, 1.0);
        e->setParam (kHpDrive, 18.0);
        e->setParam (kLpDrive, 18.0);
        e->setParam (kDrivePos, pos);
        e->reset ();
        auto out = run (*e, tones ({{200.0, -6.0}}, 0.5));
        return toneDb (out.l, f, 12000, 24000);
    };
    for (double f : {1000.0, 1400.0, 1800.0}) // the 5th, 7th and 9th harmonics
    {
        const double pre = level (kDrivePre, f), post = level (kDrivePost, f);
        CHECK (post > -45.0, "Post: %.0f Hz stays (%f dB)", f, post);
        CHECK (pre < post - 20.0, "Pre: %.0f Hz filtered away (%f vs %f dB)", f, pre, post);
    }
    CHECK (std::fabs (level (kDrivePre, 200.0) - level (kDrivePost, 200.0)) < 3.0, "the fundamental: %f vs %f",
           level (kDrivePre, 200.0), level (kDrivePost, 200.0));
}

TEST (drive_switches_without_clicks)
{
    // moving the drive between Pre and Post (it fades out and back in) and switching it on and off (a
    // crossfade) never jump: a 200 Hz sine's steps stay those of the sine
    auto e = engine ();
    e->setParam (kHpFreq, 1000.0);
    e->setParam (kLpFreq, 1000.0);
    e->setParam (kHpDrive, 12.0);
    e->setParam (kLpDrive, 12.0);
    e->reset ();
    auto in = tones ({{200.0, -6.0}}, 2.0);
    Sig out;
    out.l.resize (in.l.size ());
    out.r.resize (in.r.size ());
    for (size_t pos = 0, k = 0; pos < in.l.size (); pos += 256, ++k)
    {
        if (k % 40 == 10)
        {
            const double on = e->param (kHpDriveOn) < 0.5 ? 1.0 : 0.0;
            e->setParam (kHpDriveOn, on);
            e->setParam (kLpDriveOn, on);
        }
        if (k % 40 == 30)
            e->setParam (kDrivePos, e->param (kDrivePos) < 0.5 ? kDrivePost : kDrivePre);
        e->process (in.l.data () + pos, in.r.data () + pos, out.l.data () + pos, out.r.data () + pos, 256);
    }
    auto largestStep = [] (const std::vector<float>& x, size_t from) {
        double step = 0.0;
        for (size_t i = from + 1; i < x.size (); ++i)
            step = std::max (step, (double)std::fabs (x[i] - x[i - 1]));
        return step;
    };
    // the largest step of the sine left alone, driven (steeper) or not
    double steady = 0.0;
    for (int pos : {kDrivePre, kDrivePost})
    {
        auto s = engine ();
        s->setParam (kHpFreq, 1000.0);
        s->setParam (kLpFreq, 1000.0);
        s->setParam (kHpDrive, 12.0);
        s->setParam (kLpDrive, 12.0);
        s->setParam (kHpDriveOn, 1.0);
        s->setParam (kLpDriveOn, 1.0);
        s->setParam (kDrivePos, pos);
        s->reset ();
        steady = std::max (steady, largestStep (run (*s, tones ({{200.0, -6.0}}, 0.2)).l, 4800));
    }
    const double step = largestStep (out.l, 4800);
    CHECK (step < 1.1 * steady, "largest step %f (the sine's, driven: %f)", step, steady);
    // after a move it is back at full level
    const double settled = toneDb (out.l, 200.0, (size_t)(256 * 38), (size_t)(256 * 40));
    CHECK (settled > -8.0, "back after the move: %f dB", settled);
}

TEST (bypassed_is_the_latency)
{
    // where Para is built in without its end saturator (Smemplr's rack), switching it off leaves the
    // delay its latency stands for
    Engine e (false);
    e.prepare (kSr, 512);
    CHECK (e.latency () > 0, "the drive's delay: %d", e.latency ());
    std::vector<float> l (1024, 0.0f), r (1024, 0.0f);
    l[10] = r[10] = 0.5f;
    e.processBypassed (l.data (), r.data (), 300);
    e.processBypassed (l.data () + 300, r.data () + 300, 724);
    CHECK (peakAt (l) == (size_t)(10 + e.latency ()) && l[peakAt (l)] == 0.5f, "at %zu", peakAt (l));
}

TEST (fuzz_and_automation)
{
    uint32_t seed = 5;
    auto rnd = [&] {
        seed = seed * 1664525u + 1013904223u;
        return (double)((seed >> 8) & 0xFFFFFF) / 16777216.0;
    };
    for (int iter = 0; iter < 40; ++iter)
    {
        Engine e;
        e.prepare (iter % 3 == 0 ? 96000.0 : (iter % 3 == 1 ? 44100.0 : kSr), 512);
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
            for (uint32_t id = 0; id < kNumParams; ++id)
                if (rnd () < 0.3)
                    e.setParam (id, paramTable ().toPlain (id, rnd ()));
            if (rnd () < 0.2)
                e.noteOn ((int)(rnd () * 127));
            if (rnd () < 0.2)
                e.setPitchBend ((float)(rnd () * 2 - 1));
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
        CHECK (finite && pk < 5000.0, "iteration %d: finite %d peak %f", iter, finite, pk); // Q up to 14, +24 dB output
    }
}

TEST (performance)
{
    auto e = engine ();
    e->setParam (kSlope, kSlope24);
    e->setParam (kEnvAmount, 12.0);
    e->noteOn (64);
    auto in = tones ({{55.0, -6.0}, {1000.0, -12.0}, {8000.0, -20.0}}, 10.0);
    const auto t0 = std::chrono::steady_clock::now ();
    run (*e, in);
    const double secs = std::chrono::duration<double> (std::chrono::steady_clock::now () - t0).count ();
    std::printf ("    CPU: %.2f%% of one core (stereo, 24 dB)\n", 100.0 * secs / 10.0);
    CHECK (secs / 10.0 < 0.05, "too slow");
    // with the drive on (4x oversampled)
    e->setParam (kHpDriveOn, 1.0);
    e->setParam (kLpDriveOn, 1.0);
    e->setParam (kHpDrive, 12.0);
    e->setParam (kLpDrive, 12.0);
    const auto t1 = std::chrono::steady_clock::now ();
    run (*e, in);
    const double secs1 = std::chrono::duration<double> (std::chrono::steady_clock::now () - t1).count ();
    std::printf ("    CPU: %.2f%% of one core with the drive on\n", 100.0 * secs1 / 10.0);
    CHECK (secs1 / 10.0 < 0.05, "too slow with the drive");
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
