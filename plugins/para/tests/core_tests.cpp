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
    CHECK (std::fabs (t.info (kHpFreq).def - 822.0) < 1e-9 && std::fabs (t.info (kLpFreq).def - 185.0) < 1e-9 &&
               t.info (kSlope).def == (double)kSlope24, "defaults: HP 822 Hz, LP 185 Hz, 24 dB");
    CHECK (t.info (kDragGain).def == 0.0 && kHostedParams == kTailBase + pk::kTailFields, "Drag Gain off, after the hosted IDs");
}

TEST (filters_meeting_sum_flat)
{
    // high-pass and low-pass at the same cutoff with resonance 0 sum to a flat response
    for (int slope : {kSlope12, kSlope18, kSlope24})
    {
        auto e = engine ();
        e->setParam (kSlope, slope);
        e->setParam (kHpFreq, 1000.0);
        e->setParam (kLpFreq, 1000.0);
        for (double f : {60.0, 300.0, 1000.0, 3000.0, 12000.0})
            CHECK (std::fabs (gainAt (*e, f)) < 0.3, "slope %d at %.0f Hz: %f dB", slope, f, gainAt (*e, f));
    }
}

TEST (notch_between_the_filters)
{
    // the defaults: high-pass at 800 Hz above a low-pass at 200 Hz leave a notch around 400 Hz
    auto e = engine ();
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

TEST (key_tracking_transpose_and_bend)
{
    // an octave up moves both cutoffs an octave up: the response at 2f for note 72 equals the
    // response at f for the root
    auto e = engine ();
    auto shifted = [&] (double f) {
        e->noteOn (72);
        const double up = gainAt (*e, 2.0 * f);
        e->noteOn (60);
        const double root = gainAt (*e, f);
        return std::fabs (up - root);
    };
    for (double f : {300.0, 400.0, 600.0})
        CHECK (shifted (f) < 0.3, "octave tracking at %.0f Hz: %f dB apart", f, shifted (f));
    // no note: the cutoffs stay put
    auto fresh = engine ();
    CHECK (std::fabs (gainAt (*fresh, 400.0) - gainAt (*e, 400.0)) < 0.3, "root == no note");
    // half key tracking: an octave up moves them half an octave
    e->setParam (kKey, 0.5);
    e->noteOn (72);
    const double half = gainAt (*e, 400.0 * std::sqrt (2.0));
    e->noteOn (60);
    CHECK (std::fabs (half - gainAt (*e, 400.0)) < 0.3, "half tracking: %f", half);
    e->setParam (kKey, 1.0);
    // Transpose +12 at the root is the same as playing an octave up
    e->setParam (kTranspose, 12.0);
    e->noteOn (60);
    CHECK (std::fabs (gainAt (*e, 800.0) - gainAt (*fresh, 400.0)) < 0.3, "transpose: %f", gainAt (*e, 800.0));
    e->setParam (kTranspose, 0.0);
    // pitch bend: full bend with a 12 semitone range is an octave
    e->setParam (kPbRange, 12.0);
    e->setPitchBend (1.0f);
    CHECK (std::fabs (gainAt (*e, 800.0) - gainAt (*fresh, 400.0)) < 0.3, "bend: %f", gainAt (*e, 800.0));
    e->setPitchBend (0.0f);
    // a different root note moves the reference
    e->setParam (kRoot, 72.0);
    e->noteOn (72);
    CHECK (std::fabs (gainAt (*e, 400.0) - gainAt (*fresh, 400.0)) < 0.3, "root 72: %f", gainAt (*e, 400.0));
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
    // the engine reports how far it moved the filters from their settings, so the display can add
    // that to the settings (and show edits before the next block)
    Meters m;
    auto e = engine ();
    e->setMeters (&m);
    e->noteOn (72); // an octave above the root, key 100 %
    run (*e, tones ({{440.0, -12.0}}, 0.1));
    CHECK (m.blocks.load () > 0, "blocks counted");
    CHECK (std::fabs (m.hpShift.load () - 12.0) < 0.05 && std::fabs (m.lpShift.load () - 12.0) < 0.05, "shift %f / %f",
           m.hpShift.load (), m.lpShift.load ());
    CHECK (std::fabs (822.0 * std::pow (2.0, m.hpShift.load () / 12.0) - m.hpHz.load ()) < 1.0, "shift matches the cutoff");
    // Vocal: the low-pass swept above the high-pass pushes it; the display's push is the engine's
    e->setParam (kMovement, kVocal);
    e->setParam (kLpFreq, 2000.0);
    run (*e, tones ({{440.0, -12.0}}, 0.1));
    double hz = hpCutoff (822.0, 12.0, 0.0), lz = lpCutoff (2000.0, 12.0, 0.0);
    float hm, lm;
    vocalPush (hz, lz, m.leaderLp.load (), hm, lm);
    CHECK (m.leaderLp.load () && std::fabs (hz - m.hpHz.load ()) < 1.0 && std::fabs (hm - m.hpMul.load ()) < 0.01,
           "vocal push: %f vs %f, fade %f vs %f", hz, m.hpHz.load (), hm, m.hpMul.load ());
}

TEST (vocal_fades_out_within_a_minor_third)
{
    // the high-pass at 822 Hz, the low-pass swept up to 1 kHz (3.4 semitones past it): in Vocal the
    // high-pass is gone, so nothing is left at 8 kHz; in Free it still passes
    for (int mode : {kFree, kVocal})
    {
        auto e = engine ();
        e->setParam (kMovement, mode);
        std::vector<float> l (480, 0.0f), r (480, 0.0f);
        for (int k = 0; k <= 20; ++k)
        {
            e->setParam (kLpFreq, 185.0 * std::pow (1000.0 / 185.0, k / 20.0));
            e->process (l.data (), r.data (), l.data (), r.data (), 480);
        }
        const double top = gainAt (*e, 8000.0);
        if (mode == kVocal)
            CHECK (top < -40.0, "vocal: no high band left at 8 kHz: %f dB", top);
        else
            CHECK (top > -1.0, "free: the high-pass passes 8 kHz: %f dB", top);
    }
}

TEST (liquid_overshoots_and_flows_back)
{
    // the same sweep of the low-pass in Vocal and in Liquid: Liquid runs ahead while it moves and
    // settles on the same cutoff after it stops
    Meters mv, ml;
    auto ev = engine (), el = engine ();
    ev->setMeters (&mv);
    el->setMeters (&ml);
    ev->setParam (kMovement, kVocal);
    el->setParam (kLiquid, 1.0);
    std::vector<float> l (480, 0.0f), r (480, 0.0f);
    for (int k = 0; k <= 20; ++k) // 185 Hz to 1.5 kHz in 0.2 s
    {
        const double f = 185.0 * std::pow (1500.0 / 185.0, k / 20.0);
        ev->setParam (kLpFreq, f);
        el->setParam (kLpFreq, f);
        ev->process (l.data (), r.data (), l.data (), r.data (), 480);
        el->process (l.data (), r.data (), l.data (), r.data (), 480);
    }
    const double ahead = 12.0 * std::log2 (ml.lpHz.load () / mv.lpHz.load ());
    CHECK (ahead > 2.0, "Liquid runs ahead of the sweep: %.1f semitones", ahead);
    for (int k = 0; k < 150; ++k) // 1.5 s at rest
    {
        ev->process (l.data (), r.data (), l.data (), r.data (), 480);
        el->process (l.data (), r.data (), l.data (), r.data (), 480);
    }
    const double settled = 12.0 * std::log2 (ml.lpHz.load () / mv.lpHz.load ());
    CHECK (std::fabs (settled) < 0.2, "and settles where Vocal does: %.2f semitones", settled);
}

TEST (dry_wet_and_output)
{
    auto e = engine ();
    e->setParam (kDryWet, 0.0);
    CHECK (std::fabs (gainAt (*e, 400.0)) < 0.05, "dry: %f", gainAt (*e, 400.0));
    e->setParam (kOutput, -6.0);
    CHECK (std::fabs (gainAt (*e, 400.0) + 6.0) < 0.05, "output: %f", gainAt (*e, 400.0));
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
