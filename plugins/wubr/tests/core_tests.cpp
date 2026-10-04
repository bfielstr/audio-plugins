// Headless tests for the Wubr DSP. Run: ./wubr_tests [filter]
#include "Engine.h"
#include "Params.h"
#include "Shape.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>
#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <vector>

using namespace wubr;

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

// The tests measure band 1 alone moving its gain, synced at 1/4: the defaults set to that.
static std::unique_ptr<Engine> engine ()
{
    auto e = std::make_unique<Engine> ();
    e->setParam (bandParam (0, kTarget), kTargetGain);
    e->setParam (bandParam (0, kRateMode), kSynced);
    e->setParam (bandParam (1, kBandOn), 0.0);
    e->setParam (kLinkRate, 0.0);
    e->prepare (kSr, 512);
    return e;
}

// the level (dB rms) of a sine through the engine, in windows of `win` seconds over `secs` seconds
static std::vector<double> levels (Engine& e, double hz, double secs, double win, double amp = 0.1)
{
    const int n = (int)(secs * kSr), w = (int)(win * kSr);
    std::vector<float> l (512), r (512);
    std::vector<double> out;
    double acc = 0.0;
    int inWin = 0;
    long long t = 0;
    for (int pos = 0; pos < n; pos += 512)
    {
        const int m = std::min (512, n - pos);
        for (int i = 0; i < m; ++i)
            l[(size_t)i] = r[(size_t)i] = (float)(amp * std::sin (2.0 * M_PI * hz * (double)(t + i) / kSr));
        e.process (l.data (), r.data (), l.data (), r.data (), m);
        for (int i = 0; i < m; ++i)
        {
            acc += (double)l[(size_t)i] * l[(size_t)i];
            if (++inWin == w)
            {
                out.push_back (10.0 * std::log10 (acc / w + 1e-20) - 20.0 * std::log10 (amp / std::sqrt (2.0)));
                acc = 0.0;
                inWin = 0;
            }
        }
        t += m;
    }
    return out;
}

TEST (shape_evaluation)
{
    Shape s;
    s.n = 3;
    s.p[0] = {0.0, -1.0, 0.0};
    s.p[1] = {0.5, 1.0, 0.0};
    s.p[2] = {1.0, -1.0, 0.0};
    CHECK (std::fabs (s.valueAt (0.0) + 1.0) < 1e-9 && std::fabs (s.valueAt (0.5) - 1.0) < 1e-9 && std::fabs (s.valueAt (0.25)) < 1e-9,
           "a triangle: %.3f %.3f %.3f", s.valueAt (0.0), s.valueAt (0.25), s.valueAt (0.5));
    // a curve bends the segment but keeps its ends
    s.p[0].curve = 0.8;
    CHECK (s.valueAt (0.25) > 0.3 && std::fabs (s.valueAt (0.5) - 1.0) < 1e-9, "bent up early: %.3f", s.valueAt (0.25));
    s.p[0].curve = -0.8;
    CHECK (s.valueAt (0.25) < -0.3, "bent late: %.3f", s.valueAt (0.25));
    // from parameters: the first point at 0, the last at 1, in order
    auto p = defaultParams ();
    const Shape d = shapeOf (0, [&] (uint32_t id) { return p[id]; });
    CHECK (d.n == 3 && d.p[0].x == 0.0 && d.p[2].x == 1.0 && d.hold == 2, "default shape: %d points, hold %d", d.n, d.hold);
}

TEST (lfo_moves_the_band_gain)
{
    // band 1 at 120 Hz, +-12 dB, a triangle at 1/4 = 2 Hz at 120 BPM: a 120 Hz tone swings about 24 dB
    // each half second; a 5 kHz tone, far above the band, hardly moves
    auto e = engine ();
    e->setTransport (120.0, 0.0, false);
    auto lv = levels (*e, 120.0, 2.0, 0.02);
    const auto [lo, hi] = std::minmax_element (lv.begin () + 10, lv.end ());
    std::printf ("    120 Hz: %.1f .. %.1f dB\n", *lo, *hi);
    CHECK (*hi > 11.0 && *lo < -11.0, "swings +-12 dB with the shape: %.1f .. %.1f dB", *lo, *hi);
    // the period: the loudest windows come every 0.5 s (25 windows of 20 ms)
    int firstPeak = -1, secondPeak = -1;
    for (int i = 5; i + 1 < (int)lv.size (); ++i)
        if (lv[(size_t)i] >= lv[(size_t)i - 1] && lv[(size_t)i] >= lv[(size_t)i + 1] && lv[(size_t)i] > *hi - 2.0)
        {
            if (firstPeak < 0)
                firstPeak = i;
            else if (i - firstPeak > 5)
            {
                secondPeak = i;
                break;
            }
        }
    CHECK (secondPeak - firstPeak >= 23 && secondPeak - firstPeak <= 27, "a cycle every 0.5 s: %d windows", secondPeak - firstPeak);
    auto e2 = engine ();
    auto hiTone = levels (*e2, 5000.0, 1.0, 0.02);
    const auto [lo2, hi2] = std::minmax_element (hiTone.begin () + 10, hiTone.end ());
    CHECK (*hi2 - *lo2 < 3.0, "5 kHz hardly moves: %.1f .. %.1f dB", *lo2, *hi2);
}

TEST (free_rate_and_sync_to_the_song)
{
    // Free at 4 Hz: two peaks 0.25 s apart; Sync follows the song position when the host plays
    auto e = engine ();
    e->setParam (bandParam (0, kRateMode), kFree);
    e->setParam (bandParam (0, kRateHz), 4.0);
    auto lv = levels (*e, 120.0, 1.0, 0.01);
    int peaks = 0;
    for (int i = 1; i + 1 < (int)lv.size (); ++i)
        if (lv[(size_t)i] > lv[(size_t)i - 1] && lv[(size_t)i] >= lv[(size_t)i + 1] && lv[(size_t)i] > 6.0)
            ++peaks;
    CHECK (peaks >= 3 && peaks <= 5, "about 4 cycles in a second: %d peaks", peaks);
    // synced, playing: at song position 0.5 beats (half a 1/4 cycle) the triangle is at its top
    // started half a beat into the song (1/4 at 120 BPM: a cycle a beat), it is at the top of the triangle;
    // 0.2 s later it lines up with one started at the song's beginning 0.45 s before
    auto s = engine ();
    s->setTransport (120.0, 0.5, true);
    auto at = levels (*s, 120.0, 0.2, 0.01);
    auto z = engine ();
    z->setTransport (120.0, 0.0, true);
    auto from0 = levels (*z, 120.0, 0.45, 0.01);
    CHECK (at[1] > 9.0, "the song position sets the phase: %.1f dB", at[1]);
    CHECK (std::fabs (at.back () - from0.back ()) < 0.5, "and it keeps time with the song: %.1f vs %.1f dB", at.back (), from0.back ());
}

TEST (frequency_target_sweeps_the_centre)
{
    // Frequency: the band sits at +12 dB and its centre moves over 4 octaves around 400 Hz: a 100 Hz
    // tone and a 1600 Hz tone take turns being lifted
    auto make = [] {
        auto e = engine ();
        e->setParam (bandParam (0, kTarget), kTargetFreq);
        e->setParam (bandParam (0, kFreq), 400.0);
        e->setParam (bandParam (0, kWidth), 1.0);
        e->setParam (bandParam (0, kGain), 12.0);
        e->setParam (bandParam (0, kSweep), 4.0);
        return e;
    };
    auto a = make (), b = make ();
    auto low = levels (*a, 100.0, 1.0, 0.02), high = levels (*b, 1600.0, 1.0, 0.02);
    const auto [lLo, lHi] = std::minmax_element (low.begin () + 5, low.end ());
    const auto [hLo, hHi] = std::minmax_element (high.begin () + 5, high.end ());
    std::printf ("    100 Hz %.1f .. %.1f dB, 1600 Hz %.1f .. %.1f dB\n", *lLo, *lHi, *hLo, *hHi);
    CHECK (*lHi - *lLo > 6.0 && *hHi - *hLo > 6.0, "both tones are swept over");
    // the low tone is loudest where the high one is quietest (the centre is at the other end)
    const size_t iLow = (size_t)(std::max_element (low.begin () + 5, low.end ()) - low.begin ());
    CHECK (high[iLow] < *hHi - 6.0, "not both at once: %.1f dB at the low tone's peak", high[iLow]);
}

TEST (envelope_holds_and_releases)
{
    // Envelope, MIDI: a note starts the shape; with the hold on point 2 (the top, +12 dB) it stays there
    // while the note is held; let go, it runs to the end (-12 dB) and stays
    auto e = engine ();
    e->setParam (kMode, kEnvelope);
    e->setParam (bandParam (0, kHold), 2.0);
    auto before = levels (*e, 120.0, 0.2, 0.05);
    CHECK (before.back () < -9.0, "at rest at the end of the shape: %.1f dB", before.back ());
    e->noteOn (60);
    auto held = levels (*e, 120.0, 1.0, 0.05); // the shape's top is 0.25 s in (1/4 at 120 BPM: 0.5 s a cycle)
    CHECK (held.back () > 9.0 && held[10] > 9.0, "held at the top: %.1f / %.1f dB", held[10], held.back ());
    e->noteOff (60);
    auto after = levels (*e, 120.0, 0.6, 0.05);
    CHECK (after.back () < -9.0, "let go: to the end: %.1f dB", after.back ());
    // an LFO would not hold
    auto l = engine ();
    auto lfo = levels (*l, 120.0, 1.0, 0.05);
    const auto [lo, hi] = std::minmax_element (lfo.begin () + 4, lfo.end ());
    CHECK (*hi - *lo > 15.0, "LFO keeps moving");
}

TEST (transient_trigger)
{
    // Envelope, Transient: a quiet tone, then a hit: the hit starts the shape (it runs to the hold point)
    auto e = engine ();
    e->setParam (kMode, kEnvelope);
    e->setParam (kTrigger, kTransient);
    e->setParam (bandParam (0, kHold), 2.0);
    Meters m;
    e->setMeters (&m);
    std::vector<float> l (512), r (512);
    const int n = (int)(1.0 * kSr);
    for (int pos = 0; pos < n; pos += 512)
    {
        for (int i = 0; i < 512; ++i)
        {
            const long long t = pos + i;
            double x = 0.01 * std::sin (2.0 * M_PI * 120.0 * t / kSr);
            if (t >= (long long)(0.5 * kSr) && t < (long long)(0.52 * kSr))
                x += 0.8 * std::sin (2.0 * M_PI * 90.0 * t / kSr); // a hit
            l[(size_t)i] = r[(size_t)i] = (float)x;
        }
        e->process (l.data (), r.data (), l.data (), r.data (), 512);
    }
    CHECK (m.triggers.load () == 2, "the tone starting, then the hit: %u triggers", m.triggers.load ());
    CHECK (std::fabs (m.pos[0].load () - 0.5f) < 0.01f && m.value[0].load () > 0.99f, "held at the hold point: %.2f", m.pos[0].load ());
}

TEST (notes_by_key_and_a_hold_moved_back)
{
    // the same key twice and one note-off: nothing hangs (notes are kept by key, not counted)
    auto e = engine ();
    e->setParam (kMode, kEnvelope);
    e->setParam (bandParam (0, kHold), 2.0);
    e->noteOn (60);
    e->noteOn (60);
    levels (*e, 120.0, 0.4, 0.05);
    e->noteOff (60);
    auto after = levels (*e, 120.0, 0.6, 0.05);
    CHECK (after.back () < -9.0, "let go once: it plays to the end (%.1f dB)", after.back ());
    // held at the top, then the hold moved back to the first point: the band follows it down
    auto h = engine ();
    h->setParam (kMode, kEnvelope);
    h->setParam (bandParam (0, kHold), 2.0);
    h->noteOn (60);
    auto top = levels (*h, 120.0, 0.5, 0.05);
    h->setParam (bandParam (0, kHold), 1.0);
    auto back = levels (*h, 120.0, 0.3, 0.05);
    CHECK (top.back () > 9.0 && back.back () < -9.0, "back to the new hold: %.1f -> %.1f dB", top.back (), back.back ());
    h->allNotesOff ();
}

TEST (band_off_fades_and_split_blocks_stay_in_time)
{
    // a band at +12 dB switched off mid-tone: no jump bigger than the tone's own steps
    auto e = engine ();
    e->setParam (bandParam (0, kTarget), kTargetGain);
    e->setParam (bandParam (0, kGain), 12.0);
    e->setParam (bandParam (0, kDepth), 0.0);
    const int n = 48000;
    std::vector<float> l (n), r (n);
    for (int i = 0; i < n; ++i)
        l[(size_t)i] = r[(size_t)i] = (float)(0.1 * std::sin (2.0 * M_PI * 120.0 * i / kSr));
    e->process (l.data (), r.data (), l.data (), r.data (), n / 2);
    e->setParam (bandParam (0, kBandOn), 0.0);
    e->process (l.data () + n / 2, r.data () + n / 2, l.data () + n / 2, r.data () + n / 2, n / 2);
    double steady = 0.0, around = 0.0;
    for (int i = 1000; i < n / 2 - 1000; ++i)
        steady = std::max (steady, (double)std::fabs (l[(size_t)i] - l[(size_t)i - 1]));
    for (int i = n / 2 - 100; i < n / 2 + 2000; ++i)
        around = std::max (around, (double)std::fabs (l[(size_t)i] - l[(size_t)i - 1]));
    CHECK (around <= steady * 1.05, "no click: %.4f vs %.4f", around, steady);
    CHECK (std::fabs (20.0 * std::log10 (std::fabs (l[(size_t)n - 100]) + 1e-9)) < 60.0, "finite");
    // a synced LFO while the host plays: a block split in two (as at a note) sounds like one block
    auto run = [] (bool split) {
        auto x = engine ();
        x->setTransport (120.0, 3.25, true);
        std::vector<float> a (4800), b (4800);
        for (int i = 0; i < 4800; ++i)
            a[(size_t)i] = b[(size_t)i] = (float)(0.1 * std::sin (2.0 * M_PI * 120.0 * i / kSr));
        if (split)
        {
            x->process (a.data (), b.data (), a.data (), b.data (), 1700);
            x->process (a.data () + 1700, b.data () + 1700, a.data () + 1700, b.data () + 1700, 3100);
        }
        else
            x->process (a.data (), b.data (), a.data (), b.data (), 4800);
        return a;
    };
    const auto whole = run (false), split = run (true);
    double diff = 0.0;
    for (size_t i = 0; i < whole.size (); ++i)
        diff = std::max (diff, (double)std::fabs (whole[i] - split[i]));
    CHECK (diff < 1e-4, "split at a note, still in time: %.6f", diff);
}

TEST (linked_rates)
{
    // linked, band 2 runs at band 1's rate (its own Hz ignored), each with its own phase; unlinked, at its own
    auto run = [] (bool link) {
        Meters m;
        auto e = engine ();
        e->setMeters (&m);
        e->setParam (kLinkRate, link ? 1.0 : 0.0);
        e->setParam (bandParam (1, kBandOn), 1.0);
        for (int b = 0; b < kBands; ++b)
            e->setParam (bandParam (b, kRateMode), kFree);
        e->setParam (bandParam (0, kRateHz), 2.0);
        e->setParam (bandParam (1, kRateHz), 0.5);
        std::vector<float> l (480), r (480);
        double pos1 = 0.0, pos2 = 0.0;
        for (int k = 0; k < 10; ++k) // 0.1 s
        {
            e->process (l.data (), r.data (), l.data (), r.data (), 480);
            pos1 = m.pos[0].load ();
            pos2 = m.pos[1].load ();
        }
        return std::pair<double, double> (pos1, pos2);
    };
    const auto linked = run (true), apart = run (false);
    std::printf ("    after 0.1 s: linked %.3f / %.3f, apart %.3f / %.3f\n", linked.first, linked.second, apart.first, apart.second);
    CHECK (std::fabs (linked.first - linked.second) < 0.01 && std::fabs (linked.first - 0.2) < 0.01, "linked: both at band 1's 2 Hz");
    CHECK (std::fabs (apart.second - 0.05) < 0.01, "apart: band 2 at its own 0.5 Hz (%.3f)", apart.second);
}

TEST (defaults_and_the_end_saturator)
{
    const auto& t = paramTable ();
    CHECK (t.size () == kNumParams, "every parameter: %u", (unsigned)t.size ());
    for (int b = 0; b < kBands; ++b)
        CHECK (t.info (bandParam (b, kBandOn)).def == 1.0 && t.info (bandParam (b, kTarget)).def == kTargetFreq &&
                   t.info (bandParam (b, kRateMode)).def == kFree && t.info (bandParam (b, kRateHz)).def == 0.75 &&
                   t.info (bandParam (b, kGain)).def == 0.0,
               "band %d: on, Frequency, free at 0.75 Hz, Gain 0 dB", b + 1);
    CHECK (t.info (kLinkRate).def == 1.0 && kLinkRate == kTailExtBase + pk::kTailExtFields, "rates linked, after the saturator's block");
    CHECK (kTailExt2Base == kLinkRate + 1, "then Gentlr's Advanced block");
    CHECK (kTailExt3Base == kTailExt2Base + pk::kTailExt2Fields && kNumParams == kTailExt3Base + pk::kTailExt3Fields &&
               std::string (t.info (kTailExt3Base + pk::kTailExt3High).name) == "Saturator Gentlr High (unused)" &&
               std::string (t.info (kTailExt2Base + pk::kTailExt2Advanced).name) == "Saturator Gentlr Advanced" &&
               t.info (kTailExt2Base + pk::kTailExt2Threshold).def == -18.0 && t.info (kTailExt2Base + pk::kTailExt2Advanced).def == 0.0,
           "Gentlr's Advanced block (the end saturator's) is the last");
    CHECK (t.info (kTailBase + pk::kTailOn).def == 0.0 && t.info (kTailExtBase + pk::kTailExtClarity).id == kTailExtBase + pk::kTailExtClarity,
           "the end Smacheratr, off");
    // dry passes when the bands are off and the mix is 0
    auto e = engine ();
    e->setParam (kDryWet, 0.0);
    auto lv = levels (*e, 120.0, 0.5, 0.05);
    CHECK (std::fabs (lv.back ()) < 0.1, "dry: %.2f dB", lv.back ());
}

TEST (fuzz_and_cpu)
{
    uint32_t seed = 7;
    auto rnd = [&] {
        seed = seed * 1664525u + 1013904223u;
        return (double)((seed >> 8) & 0xFFFFFF) / 16777216.0;
    };
    auto e = engine ();
    std::vector<float> l (256), r (256);
    bool finite = true;
    const std::clock_t t0 = std::clock (); // CPU time: other programs running do not count
    for (int blk = 0; blk < 2000; ++blk)
    {
        if (blk % 20 == 0)
            for (int k = 0; k < 6; ++k)
            {
                const uint32_t id = (uint32_t)(rnd () * kNumParams) % kNumParams;
                const auto& info = paramTable ().info (id);
                e->setParam (id, info.min + rnd () * (info.max - info.min));
            }
        e->setParam (bandParam (1, kBandOn), 1.0);
        for (int i = 0; i < 256; ++i)
            l[(size_t)i] = r[(size_t)i] = (float)(rnd () * 2.0 - 1.0) * 0.5f;
        if (blk % 50 == 0)
            e->trigger ();
        e->process (l.data (), r.data (), l.data (), r.data (), 256);
        for (float v : l)
            finite &= std::isfinite (v) && std::fabs (v) < 100.0f;
    }
    const double secs = (double)(std::clock () - t0) / CLOCKS_PER_SEC;
    std::printf ("    %.1f%% of real time\n", 100.0 * secs / (2000.0 * 256 / kSr));
    CHECK (finite, "finite and bounded");
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
