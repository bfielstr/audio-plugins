// Headless tests for the Smempler DSP core. Run: ./smempler_tests [filter]
#include "Engine.h"
#include "Fft.h"
#include "Filter.h"
#include "Params.h"
#include "Rack.h"
#include "SampleData.h"
#include "Slices.h"

#include "dr_wav.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

using namespace smempler;

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

// ---------------------------------------------------------------------------
// helpers
constexpr double kHostSr = 48000.0;

static std::shared_ptr<SampleData> sine (double freq, double secs, double sr = 44100.0, bool stereo = false)
{
    std::vector<float> l ((size_t)(secs * sr)), r;
    for (size_t i = 0; i < l.size (); ++i)
        l[i] = 0.5f * (float)std::sin (2.0 * M_PI * freq * i / sr);
    if (stereo)
        r = l;
    return SampleData::fromBuffers (l, r, sr, "sine");
}

// Decaying noise bursts at the given times (seconds): a synthetic drum loop.
static std::shared_ptr<SampleData> bursts (const std::vector<double>& times, double secs, double sr = 44100.0)
{
    std::vector<float> l ((size_t)(secs * sr), 0.0f);
    uint32_t seed = 42;
    for (double t : times)
    {
        const size_t s = (size_t)(t * sr);
        for (size_t i = 0; i < (size_t)(0.12 * sr) && s + i < l.size (); ++i)
        {
            const float env = std::exp (-(float)i / (float)(0.02 * sr));
            l[s + i] += 0.8f * env * randomBipolar (seed);
        }
    }
    return SampleData::fromBuffers (l, {}, sr, "bursts");
}

struct Out
{
    std::vector<float> l, r;
};

static Out run (Engine& e, int frames, HostInfo host = {}, int block = 256)
{
    Out o;
    o.l.resize ((size_t)frames);
    o.r.resize ((size_t)frames);
    for (int pos = 0; pos < frames; pos += block)
    {
        const int n = std::min (block, frames - pos);
        e.render (o.l.data () + pos, o.r.data () + pos, n, host);
        host.ppq += n * host.bpm / 60.0 / kHostSr;
    }
    return o;
}

static Engine* makeEngine (std::shared_ptr<SampleData> s)
{
    auto* e = new Engine ();
    e->prepare (kHostSr, 512);
    e->setSample (s);
    e->setParam (kVolume, 0.0);
    e->setParam (kVelVol, 0.0);
    e->setParam (kLoopOn, 0.0); // looping is on by default; tests switch it on where they need it
    e->setParam (kVoices, 7);   // 8 voices (the default is 1): the tests play chords
    return e;
}

static double rms (const std::vector<float>& x, size_t a = 0, size_t b = SIZE_MAX)
{
    b = std::min (b, x.size ());
    if (b <= a)
        return 0.0;
    double s = 0.0;
    for (size_t i = a; i < b; ++i)
        s += (double)x[i] * x[i];
    return std::sqrt (s / (double)(b - a));
}

static double peak (const std::vector<float>& x)
{
    double p = 0.0;
    for (float v : x)
        p = std::max (p, (double)std::fabs (v));
    return p;
}

static bool finite (const std::vector<float>& x)
{
    for (float v : x)
        if (!std::isfinite (v))
            return false;
    return true;
}

// Frequency from zero crossings between [a, b)
static double freqOf (const std::vector<float>& x, size_t a, size_t b, double sr = kHostSr)
{
    int crossings = 0;
    size_t first = 0, last = 0;
    for (size_t i = a + 1; i < b && i < x.size (); ++i)
        if (x[i - 1] < 0.0f && x[i] >= 0.0f)
        {
            if (crossings == 0)
                first = i;
            last = i;
            ++crossings;
        }
    if (crossings < 2)
        return 0.0;
    return (crossings - 1) * sr / (double)(last - first);
}

// Index after which the signal stays below `thr` (end of sound).
static size_t soundEnd (const std::vector<float>& x, float thr = 1e-3f)
{
    size_t last = 0;
    for (size_t i = 0; i < x.size (); ++i)
        if (std::fabs (x[i]) > thr)
            last = i;
    return last;
}

static size_t soundStart (const std::vector<float>& x, float thr = 1e-3f)
{
    for (size_t i = 0; i < x.size (); ++i)
        if (std::fabs (x[i]) > thr)
            return i;
    return x.size ();
}

// ---------------------------------------------------------------------------
TEST (params_roundtrip)
{
    for (uint32_t id = 0; id < kNumParams; ++id)
    {
        const auto& p = paramInfo (id);
        CHECK (p.id == id, "table order mismatch at %u (%s)", id, p.name);
        for (double n : {0.0, 0.25, 0.5, 0.75, 1.0})
        {
            const double plain = toPlain (id, n);
            CHECK (plain >= p.min - 1e-9 && plain <= p.max + 1e-9, "%s out of range", p.name);
            const double back = toPlain (id, toNormalized (id, plain));
            CHECK (std::fabs (back - plain) < 1e-6 * std::max (1.0, std::fabs (plain)), "%s: %f -> %f", p.name, plain,
                   back);
        }
        const double d = toPlain (id, defaultNormalized (id));
        CHECK (std::fabs (d - p.def) < 1e-6 * std::max (1.0, std::fabs (p.def)), "%s default %f vs %f", p.name, d,
               p.def);
        double parsed = 0;
        const std::string txt = toText (id, p.def);
        if (p.disp != Disp::Degrees)
        {
            CHECK (fromText (id, txt, parsed), "%s cannot parse '%s'", p.name, txt.c_str ());
            CHECK (std::fabs (parsed - p.def) <= std::max (0.051 * std::fabs (p.def), 0.02),
                   "%s parse '%s' -> %f (def %f)", p.name, txt.c_str (), parsed, p.def);
        }
    }
    CHECK (toText (kWarpBeats, 16) == "4 Bars", "%s", toText (kWarpBeats, 16).c_str ());
    CHECK (toText (kWarpBeats, 3) == "3 Beats", "%s", toText (kWarpBeats, 3).c_str ());
    CHECK (toText (kPan, -0.5) == "25L", "%s", toText (kPan, -0.5).c_str ());
    CHECK (toText (kFilterFreq, 22000) == "22.00 kHz", "%s", toText (kFilterFreq, 22000).c_str ());
}

TEST (fft_roundtrip)
{
    for (int n : {256, 2048, 4096})
    {
        Fft f (n);
        std::vector<float> x ((size_t)n), y ((size_t)n);
        uint32_t seed = 7;
        for (auto& v : x)
            v = randomBipolar (seed);
        std::vector<Fft::cf> s ((size_t)f.bins ());
        f.forward (x.data (), s.data ());
        f.inverse (s.data (), y.data ());
        double err = 0;
        for (int i = 0; i < n; ++i)
            err = std::max (err, (double)std::fabs (x[(size_t)i] - y[(size_t)i]));
        CHECK (err < 1e-4, "n=%d err=%g", n, err);
        // a pure tone lands in its bin
        for (int i = 0; i < n; ++i)
            x[(size_t)i] = (float)std::cos (2 * M_PI * 10 * i / n);
        f.forward (x.data (), s.data ());
        CHECK (std::fabs (std::abs (s[10]) - n / 2.0) < 1e-2 * n, "bin10=%f", std::abs (s[10]));
        CHECK (std::abs (s[11]) < 1e-2 * n, "bin11=%f", std::abs (s[11]));
    }
}

static double toneAmp (const std::vector<float>& x, double f, size_t a, size_t b);

// Loads an effect into a rack slot with its own defaults, on; sets one of its values (plain).
static void loadFx (Engine& e, int slot, int type)
{
    e.setParam (slotParam (slot, kSlotType), (double)type);
    e.setParam (slotParam (slot, kSlotOn), 1.0);
    const auto& t = fxTable (type);
    for (uint32_t j = 0; j < kSlotBlock; ++j)
        e.setParam (slotBlockParam (slot, j), j < t.size () ? t.defaultNormalized (j) : 0.0);
}
static void setFx (Engine& e, int slot, uint32_t id, double plain)
{
    e.setParam (slotBlockParam (slot, id), fxTable (e.rackType (slot)).toNormalized (id, plain));
}

TEST (rack_effects)
{
    // a 440 Hz sample; Para's notch between its filters lands on it, and stays put with the note
    auto s = sine (440.0, 1.0);
    std::unique_ptr<Engine> e (makeEngine (s));
    const int base = e->latency ();
    CHECK (base > 0, "the saturator at the very end reports its latency: %d", base);
    e->noteOn (60, 1.0f);
    auto o = run (*e, 24000);
    const double dry = rms (o.l, 12000, 24000);
    loadFx (*e, 0, kFxPara);
    setFx (*e, 0, para::kHpFreq, 700.0);
    setFx (*e, 0, para::kLpFreq, 275.0);
    e->reset ();
    e->noteOn (60, 1.0f);
    o = run (*e, 24000);
    const double notched = rms (o.l, 12000, 24000);
    CHECK (notched < dry * 0.4, "notch on the sample: %f vs %f", notched, dry);
    // the same effect twice: two notches are deeper than one
    loadFx (*e, 1, kFxPara);
    setFx (*e, 1, para::kHpFreq, 700.0);
    setFx (*e, 1, para::kLpFreq, 275.0);
    e->reset ();
    e->noteOn (60, 1.0f);
    o = run (*e, 24000);
    CHECK (rms (o.l, 12000, 24000) < notched * 0.5, "two Paras: %f vs %f", rms (o.l, 12000, 24000), notched);
    // off: the slot passes the sound
    e->setParam (slotParam (0, kSlotOn), 0.0);
    e->setParam (slotParam (1, kSlotOn), 0.0);
    e->reset ();
    e->noteOn (60, 1.0f);
    o = run (*e, 24000);
    CHECK (std::fabs (rms (o.l, 12000, 24000) / dry - 1.0) < 0.02, "both off: untouched (%f vs %f)", rms (o.l, 12000, 24000), dry);
    // Multidyn in the first slot instead: its preset lifts a quiet sample, and its latency counts
    e->setParam (slotParam (1, kSlotType), (double)kFxEmpty);
    loadFx (*e, 0, kFxMultidyn);
    CHECK (e->latency () > base, "Multidyn's look-ahead is reported: %d", e->latency ());
    e->setParam (kGain, -30.0);
    e->reset ();
    e->noteOn (60, 1.0f);
    o = run (*e, 48000);
    const double lifted = rms (o.l, 24000, 48000);
    e->setParam (slotParam (0, kSlotType), (double)kFxEmpty);
    CHECK (e->latency () == base, "taken out: back to %d (%d)", base, e->latency ());
    e->reset ();
    e->noteOn (60, 1.0f);
    o = run (*e, 48000);
    CHECK (lifted > rms (o.l, 24000, 48000) * 2.0, "upward compression lifts it: %f vs %f", lifted, rms (o.l, 24000, 48000));
}

TEST (rack_order_and_widr)
{
    auto s = sine (440.0, 1.0);
    // Smacheratr driven hard, then Para's notch, or the other way round: the harmonics the saturator
    // makes after the notch are not notched, so the two orders sound different
    auto render = [&] (bool satFirst) {
        std::unique_ptr<Engine> e (makeEngine (s));
        loadFx (*e, satFirst ? 0 : 1, kFxSmacheratr);
        setFx (*e, satFirst ? 0 : 1, smacheratr::kDrive, 24.0);
        loadFx (*e, satFirst ? 1 : 0, kFxPara);
        setFx (*e, satFirst ? 1 : 0, para::kHpFreq, 1600.0);
        setFx (*e, satFirst ? 1 : 0, para::kLpFreq, 700.0);
        e->noteOn (60, 1.0f);
        return run (*e, 24000);
    };
    const auto a = render (true), b = render (false);
    CHECK (std::fabs (toneAmp (a.l, 1320.0, 12000, 24000) - toneAmp (b.l, 1320.0, 12000, 24000)) >
               0.3 * std::max (toneAmp (a.l, 1320.0, 12000, 24000), toneAmp (b.l, 1320.0, 12000, 24000)),
           "the order matters: 3rd harmonic %f vs %f", toneAmp (a.l, 1320.0, 12000, 24000), toneAmp (b.l, 1320.0, 12000, 24000));
    // Widr in the rack widens a mono sample; off, it is mono again
    std::unique_ptr<Engine> e (makeEngine (s));
    loadFx (*e, 0, kFxWidr);
    e->noteOn (60, 1.0f);
    auto o = run (*e, 36000);
    double sideE = 0.0, midE = 0.0;
    for (size_t i = 12000; i < o.l.size (); ++i)
    {
        sideE += 0.25 * (o.l[i] - o.r[i]) * (o.l[i] - o.r[i]);
        midE += 0.25 * (o.l[i] + o.r[i]) * (o.l[i] + o.r[i]);
    }
    CHECK (sideE > 0.02 * midE, "Widr widens it: side %f of the mid", sideE / midE);
    e->setParam (slotParam (0, kSlotOn), 0.0);
    e->reset ();
    e->noteOn (60, 1.0f);
    o = run (*e, 36000);
    double side2 = 0.0;
    for (size_t i = 12000; i < o.l.size (); ++i)
        side2 += 0.25 * (o.l[i] - o.r[i]) * (o.l[i] - o.r[i]);
    CHECK (side2 < 1e-9 * midE + 1e-12, "off: mono again (%g)", side2);
}

TEST (defaults_one_voice_and_root_note)
{
    const auto& t = paramTable ();
    CHECK (voicesFromIndex ((int)t.info (kVoices).def) == 1, "one voice by default");
    CHECK (t.info (kRootKey).def == 60.0 && t.toText (kRootKey, 60.0) == "C3", "root C3");
    CHECK (t.info (kTailBase + pk::kTailOn).def == 0.0 && t.info (kTailBase + pk::kTailDrive).def == 0.0, "saturator off, 0 dB");
    // a 440 Hz sample with the root on C4: C4 plays 440 Hz, C3 an octave down
    auto s = sine (440.0, 1.0);
    std::unique_ptr<Engine> e (makeEngine (s));
    e->setParam (kRootKey, 72.0);
    e->noteOn (72, 1.0f);
    auto o = run (*e, 12000);
    CHECK (std::fabs (freqOf (o.l, 2000, 12000) / 440.0 - 1.0) < 0.003, "root plays the sample's pitch: %f", freqOf (o.l, 2000, 12000));
    e->allNotesOff ();
    run (*e, 9600);
    e->reset ();
    e->noteOn (60, 1.0f);
    o = run (*e, 12000);
    CHECK (std::fabs (freqOf (o.l, 2000, 12000) / 220.0 - 1.0) < 0.003, "an octave below the root: %f", freqOf (o.l, 2000, 12000));
}

// Amplitude of frequency f in x[a, b).
static double toneAmp (const std::vector<float>& x, double f, size_t a, size_t b)
{
    double s = 0, c = 0;
    for (size_t i = a; i < b; ++i)
    {
        s += x[i] * std::sin (2.0 * M_PI * f * i / kHostSr);
        c += x[i] * std::cos (2.0 * M_PI * f * i / kHostSr);
    }
    return 2.0 * std::sqrt (s * s + c * c) / (double)(b - a);
}

TEST (mid_side_eq_tapers_the_sides)
{
    // mid: 100 Hz; side: 50 Hz and 3 kHz. The side high-pass at 300 Hz removes the 50 Hz side
    // and keeps the rest.
    const double sr = kHostSr;
    std::vector<float> l ((size_t)sr), r ((size_t)sr);
    for (size_t i = 0; i < l.size (); ++i)
    {
        const double t = (double)i / sr;
        const double m = 0.2 * std::sin (2 * M_PI * 100 * t), sd = 0.2 * std::sin (2 * M_PI * 50 * t) + 0.1 * std::sin (2 * M_PI * 3000 * t);
        l[i] = (float)(m + sd);
        r[i] = (float)(m - sd);
    }
    auto smp = SampleData::fromBuffers (l, r, sr);
    auto measure = [&] (bool msOn, int slope, double sideDb) {
        std::unique_ptr<Engine> e (makeEngine (smp));
        e->setParam (kFilterOn, 0.0);
        loadFx (*e, 0, kFxMsEq);
        e->setParam (slotParam (0, kSlotOn), msOn ? 1.0 : 0.0);
        setFx (*e, 0, mseq::kSideHp, 300.0);
        setFx (*e, 0, mseq::kSlope, slope);
        setFx (*e, 0, mseq::kSideGain, sideDb);
        e->noteOn (60, 1.0f);
        auto o = run (*e, 36000);
        std::vector<float> mid (o.l.size ()), side (o.l.size ());
        for (size_t i = 0; i < o.l.size (); ++i)
        {
            mid[i] = 0.5f * (o.l[i] + o.r[i]);
            side[i] = 0.5f * (o.l[i] - o.r[i]);
        }
        struct R { double mid100, side50, side3k; };
        return R {toneAmp (mid, 100.0, 12000, 36000), toneAmp (side, 50.0, 12000, 36000), toneAmp (side, 3000.0, 12000, 36000)};
    };
    const auto off = measure (false, MsEq::k24, 0.0);
    const auto on = measure (true, MsEq::k24, 0.0);
    CHECK (off.side50 > 0.1, "off: the low side is there: %f", off.side50);
    CHECK (on.side50 < off.side50 * 0.03, "24 dB: the low side is gone: %f vs %f", on.side50, off.side50);
    CHECK (std::fabs (on.side3k / off.side3k - 1.0) < 0.05, "the high side stays: %f vs %f", on.side3k, off.side3k);
    CHECK (std::fabs (on.mid100 / off.mid100 - 1.0) < 0.02, "the mid stays: %f vs %f", on.mid100, off.mid100);
    const auto gentle = measure (true, MsEq::k6, 0.0);
    CHECK (gentle.side50 > on.side50 * 3.0 && gentle.side50 < off.side50 * 0.3, "6 dB tapers more gently: %f", gentle.side50);
    const auto quiet = measure (true, MsEq::k24, -6.0);
    CHECK (std::fabs (quiet.side3k / on.side3k - 0.501) < 0.03, "side gain -6 dB: %f", quiet.side3k / on.side3k);
    for (int sl : {MsEq::k6, MsEq::k12, MsEq::k24})
        CHECK (std::fabs (MsEq::responseDb (300.0, 300.0, sl) + 3.01) < 0.05, "slope %d: -3 dB at the cutoff", sl);
}

TEST (effects_fuzz)
{
    // every effect on, random settings and automation: finite output
    auto s = sine (220.0, 1.0, 44100.0, true);
    uint32_t seed = 77;
    auto r01 = [&] {
        seed = seed * 1664525u + 1013904223u;
        return (float)((seed >> 8) & 0xFFFFFF) / 16777216.0f;
    };
    for (int iter = 0; iter < 30; ++iter)
    {
        std::unique_ptr<Engine> e (makeEngine (s));
        for (uint32_t id = kRackBase; id < kNumParams; ++id)
            e->setParam (id, toPlain (id, r01 ()));
        for (int slot = 0; slot < kRackSlots; ++slot) // every kind of effect somewhere
            e->setParam (slotParam (slot, kSlotType), (double)(1 + (slot + iter) % (kNumFxTypes - 1)));
        e->setParam (kTailBase + pk::kTailOn, 1.0);
        bool finite = true;
        for (int step = 0; step < 6; ++step)
        {
            e->noteOn (36 + (int)(r01 () * 48), 1.0f);
            const uint32_t id = kRackBase + (uint32_t)(r01 () * (kNumParams - kRackBase - 1));
            e->setParam (id, toPlain (id, r01 ()));
            auto o = run (*e, 2000);
            for (float v : o.l)
                finite &= std::isfinite (v);
        }
        CHECK (finite, "iteration %d produced non-finite output", iter);
    }
}

TEST (classic_pitch_and_samplerate)
{
    auto s = sine (440.0, 1.0);
    std::unique_ptr<Engine> e (makeEngine (s));
    for (auto [note, expect] : std::vector<std::pair<int, double>> {{60, 440.0}, {72, 880.0}, {48, 220.0}, {67, 440.0 * std::pow (2, 7 / 12.0)}})
    {
        e->reset ();
        e->noteOn (note, 1.0f);
        auto o = run (*e, 12000);
        const double f = freqOf (o.l, 2000, 12000);
        CHECK (std::fabs (f / expect - 1.0) < 0.003, "note %d: %f Hz (want %f)", note, f, expect);
        e->allNotesOff ();
        run (*e, 9600);
    }
    // duration halves an octave up: 1 s sample -> 0.5 s
    e->reset ();
    e->noteOn (72, 1.0f);
    auto o = run (*e, 48000);
    const double end = soundEnd (o.l) / kHostSr;
    CHECK (std::fabs (end - 0.5) < 0.01, "end %f s", end);
}

TEST (classic_loop_sustains_and_releases)
{
    auto s = sine (220.0, 0.5, 44100.0, true);
    std::unique_ptr<Engine> e (makeEngine (s));
    e->setParam (kLoopOn, 1);
    e->setParam (kLength, 0.5);
    e->setParam (kAmpR, 100.0);
    e->noteOn (60, 1.0f);
    auto o = run (*e, 96000); // 2 s, sample is only 0.5 s
    CHECK (rms (o.l, 90000, 96000) > 0.2, "loop not sustaining: rms %f", rms (o.l, 90000, 96000));
    e->noteOff (60);
    auto rel = run (*e, 24000);
    CHECK (rms (rel.l, 12000, 24000) < 1e-4, "release didn't finish: %f", rms (rel.l, 12000, 24000));
    CHECK (e->activeVoices () == 0, "voices %d", e->activeVoices ());
}

TEST (loop_crossfade_smooths_discontinuity)
{
    // A ramp has a big jump at any loop boundary.
    std::vector<float> ramp (44100);
    for (size_t i = 0; i < ramp.size (); ++i)
        ramp[i] = -0.5f + (float)i / ramp.size ();
    auto s = SampleData::fromBuffers (ramp, {}, 44100.0);
    auto maxJump = [&] (double fade) {
        std::unique_ptr<Engine> e (makeEngine (s));
        e->setParam (kFilterOn, 0);
        e->setParam (kLoopOn, 1);
        e->setParam (kLength, 0.3);
        e->setParam (kLoopFade, fade);
        e->noteOn (60, 1.0f);
        auto o = run (*e, 144000);
        double j = 0;
        for (size_t i = 60000; i < o.l.size (); ++i)
            j = std::max (j, (double)std::fabs (o.l[i] - o.l[i - 1]));
        return j;
    };
    const double hard = maxJump (0.0), soft = maxJump (0.5);
    CHECK (hard > 0.15, "expected a jump without fade: %f", hard);
    CHECK (soft < hard * 0.2, "fade didn't smooth: %f vs %f", soft, hard);
}

TEST (length_is_the_loop_and_fade_fades_the_start_in)
{
    // a constant level: the loop's crossfade is inaudible, the fade-in at the start is not
    auto s = SampleData::fromBuffers (std::vector<float> (44100, 0.5f), {}, 44100.0);
    auto play = [&] (double length, double fade, bool loop, int frames) {
        std::unique_ptr<Engine> e (makeEngine (s));
        e->setParam (kFilterOn, 0);
        e->setParam (kLoopOn, loop ? 1 : 0);
        e->setParam (kLength, length);
        e->setParam (kLoopFade, fade);
        e->noteOn (60, 1.0f);
        return run (*e, frames);
    };
    CHECK (paramTable ().info (kLoopFade).def == 0.0, "Fade off by default");
    // Length no longer shortens the sample: with Loop off it plays to the end (1 s)
    auto o = play (0.25, 0.0, false, 60000);
    const double end = soundEnd (o.l) / kHostSr;
    CHECK (std::fabs (end - 1.0) < 0.02, "plays to the end flag: %f s", end);
    // with Fade the loop's start fades in on the first pass; without, it starts at once
    const double hard = rms (play (0.3, 0.0, true, 4800).l, 480, 960);
    const double soft = rms (play (0.3, 0.5, true, 4800).l, 480, 960);
    CHECK (hard > 0.3, "no fade: starts at full level (%f)", hard);
    CHECK (soft < hard * 0.4, "fade: the start fades in (%f vs %f)", soft, hard);
    auto held = play (0.3, 0.5, true, 48000);
    CHECK (rms (held.l, 24000, 48000) > hard * 0.9, "then holds the level through the loop (%f)", rms (held.l, 24000, 48000));
}

TEST (oneshot_trigger_and_gate)
{
    auto s = sine (300.0, 0.5);
    std::unique_ptr<Engine> e (makeEngine (s));
    e->setParam (kMode, kModeOneShot);
    e->noteOn (60, 1.0f);
    run (*e, 480);
    e->noteOff (60);
    auto o = run (*e, 48000);
    const double end = (soundEnd (o.l) + 480) / kHostSr;
    CHECK (std::fabs (end - 0.5) < 0.01, "trigger should play whole sample, ended at %f", end);

    e->setParam (kTriggerGate, 1);
    e->setParam (kFadeOut, 20.0);
    e->noteOn (60, 1.0f);
    run (*e, 4800);
    e->noteOff (60);
    auto g = run (*e, 4800);
    const double gend = soundEnd (g.l) / kHostSr;
    CHECK (gend < 0.025, "gate should stop within fade-out, ended %f s after note-off", gend);

    // fade in
    e->setParam (kTriggerGate, 0);
    e->setParam (kFadeIn, 100.0);
    e->setParam (kFadeOut, 0.0);
    e->noteOn (60, 1.0f);
    auto f = run (*e, 9600);
    CHECK (rms (f.l, 0, 1200) < 0.5 * rms (f.l, 4800, 9600), "fade in missing: %f vs %f", rms (f.l, 0, 1200),
           rms (f.l, 4800, 9600));

    // one-shot is monophonic
    e->setParam (kFadeIn, 0.0);
    e->noteOn (60, 1.0f);
    e->noteOn (64, 1.0f);
    run (*e, 960);
    CHECK (e->activeVoices () == 1, "one-shot voices %d", e->activeVoices ());
}

TEST (voice_limit_and_retrig)
{
    auto s = sine (200.0, 2.0);
    std::unique_ptr<Engine> e (makeEngine (s));
    e->setParam (kVoices, 1); // index 1 -> 2 voices
    e->noteOn (60, 1.0f);
    e->noteOn (64, 1.0f);
    e->noteOn (67, 1.0f);
    run (*e, 960);
    CHECK (e->activeVoices () == 2, "expected 2 voices, got %d", e->activeVoices ());

    e->reset ();
    e->setParam (kVoices, 7);
    e->setParam (kRetrig, 1);
    e->setParam (kAmpR, 2000.0);
    e->noteOn (60, 1.0f);
    e->noteOff (60);
    e->noteOn (60, 1.0f);
    run (*e, 960);
    CHECK (e->activeVoices () == 1, "retrig should cut the old voice, got %d", e->activeVoices ());
    e->reset ();
    e->setParam (kRetrig, 0);
    e->noteOn (60, 1.0f);
    e->noteOff (60);
    e->noteOn (60, 1.0f);
    run (*e, 960);
    CHECK (e->activeVoices () == 2, "without retrig notes overlap, got %d", e->activeVoices ());
}

TEST (spread_is_stereo)
{
    auto s = sine (300.0, 1.0);
    std::unique_ptr<Engine> e (makeEngine (s));
    e->noteOn (60, 1.0f);
    auto a = run (*e, 9600);
    double diff = 0;
    for (size_t i = 0; i < a.l.size (); ++i)
        diff = std::max (diff, (double)std::fabs (a.l[i] - a.r[i]));
    CHECK (diff < 1e-6, "mono sample without spread should be centred: %f", diff);
    e->reset ();
    e->setParam (kSpread, 1.0);
    e->noteOn (60, 1.0f);
    run (*e, 9600);
    CHECK (e->activeVoices () == 2, "spread uses two voices, got %d", e->activeVoices ());
    auto b = run (*e, 9600);
    diff = 0;
    for (size_t i = 0; i < b.l.size (); ++i)
        diff = std::max (diff, (double)std::fabs (b.l[i] - b.r[i]));
    CHECK (diff > 0.05, "spread should decorrelate L/R: %f", diff);
}

TEST (pitch_bend_and_transpose)
{
    auto s = sine (440.0, 1.0);
    std::unique_ptr<Engine> e (makeEngine (s));
    e->setPitchBend (1.0f);
    e->noteOn (60, 1.0f);
    auto o = run (*e, 12000);
    const double want = 440.0 * std::pow (2.0, 5.0 / 12.0);
    CHECK (std::fabs (freqOf (o.l, 2000, 12000) / want - 1) < 0.003, "bend %f", freqOf (o.l, 2000, 12000));
    e->reset ();
    e->setPitchBend (0.0f);
    e->setParam (kTranspose, -12);
    e->setParam (kDetune, 50);
    e->noteOn (60, 1.0f);
    o = run (*e, 12000);
    const double want2 = 220.0 * std::pow (2.0, 0.5 / 12.0);
    CHECK (std::fabs (freqOf (o.l, 2000, 12000) / want2 - 1) < 0.003, "transpose %f", freqOf (o.l, 2000, 12000));
}

TEST (glide_moves_gradually)
{
    auto s = sine (440.0, 2.0);
    std::unique_ptr<Engine> e (makeEngine (s));
    e->setParam (kGlideMode, kGlideMono);
    e->setParam (kGlideTime, 200.0);
    e->noteOn (60, 1.0f);
    run (*e, 4800);
    e->noteOn (72, 1.0f); // legato
    auto o = run (*e, 24000);
    CHECK (e->activeVoices () == 1, "mono glide keeps one voice: %d", e->activeVoices ());
    const double f0 = freqOf (o.l, 0, 2400), fmid = freqOf (o.l, 3600, 6000), fend = freqOf (o.l, 14400, 24000);
    CHECK (f0 < 560 && fmid > 500 && fmid < 820 && std::fabs (fend - 880) < 5, "glide %f %f %f", f0, fmid, fend);
    e->noteOff (72); // back to held 60
    auto back = run (*e, 24000);
    CHECK (std::fabs (freqOf (back.l, 14400, 24000) - 440) < 5, "return to held note %f",
           freqOf (back.l, 14400, 24000));
}

TEST (sustain_pedal)
{
    auto s = sine (300.0, 0.5, 44100.0);
    std::unique_ptr<Engine> e (makeEngine (s));
    e->setParam (kLoopOn, 1);
    e->setSustain (true);
    e->noteOn (60, 1.0f);
    run (*e, 480);
    e->noteOff (60);
    auto o = run (*e, 9600);
    CHECK (rms (o.l, 4800, 9600) > 0.1, "pedal should hold the note");
    e->setSustain (false);
    o = run (*e, 9600);
    CHECK (rms (o.l, 7200, 9600) < 1e-4, "pedal up should release");
}

TEST (envelope_timing)
{
    auto s = sine (1000.0, 2.0);
    std::unique_ptr<Engine> e (makeEngine (s));
    e->setParam (kAmpA, 100.0);
    e->setParam (kFilterOn, 0);
    e->noteOn (60, 1.0f);
    auto o = run (*e, 14400);
    const double early = rms (o.l, 2200, 2600), mid = rms (o.l, 4600, 5000), full = rms (o.l, 9600, 14400);
    CHECK (std::fabs (mid / full - 1.0) < 0.1, "attack should be complete at 100 ms: %f", mid / full);
    CHECK (early / full > 0.35 && early / full < 0.65, "half way through a linear attack: %f", early / full);
    e->setParam (kAmpR, 200.0);
    e->noteOff (60);
    auto r = run (*e, 14400);
    CHECK (soundEnd (r.l, 1e-4f) < 0.21 * kHostSr, "release longer than 200 ms: %zu", soundEnd (r.l, 1e-4f));
    CHECK (e->activeVoices () == 0, "voice should end after release");
}

TEST (amp_envelope_loop)
{
    auto s = sine (1000.0, 3.0);
    std::unique_ptr<Engine> e (makeEngine (s));
    e->setParam (kFilterOn, 0);
    e->setParam (kAmpA, 10.0);
    e->setParam (kAmpD, 100.0);
    e->setParam (kAmpS, 0.0);
    e->setParam (kAmpLoopMode, kAmpLoopLoop);
    e->setParam (kAmpLoopTime, 1.0);
    e->noteOn (60, 1.0f);
    auto o = run (*e, 48000);
    // without looping a 0 sustain would be silent after ~110 ms; with looping it pulses on
    CHECK (rms (o.l, 36000, 48000) > 0.05, "loop mode should keep re-triggering: %f", rms (o.l, 36000, 48000));
    e->reset ();
    e->setParam (kAmpLoopMode, kAmpLoopNone);
    e->noteOn (60, 1.0f);
    o = run (*e, 48000);
    CHECK (rms (o.l, 36000, 48000) < 1e-3, "no loop: silent after decay (%f)", rms (o.l, 36000, 48000));
}

TEST (envelope_breakpoints_and_curves)
{
    // attack 10 ms -> peak, breakpoint 1: 100 ms down to 0.2, breakpoint 2: 100 ms up to 0.8,
    // decay 50 ms to sustain 0.5
    Envelope env;
    EnvSettings s;
    s.attackMs = 10.0f;
    s.points = 2;
    s.ptMs[0] = 100.0f;
    s.ptLevel[0] = 0.2f;
    s.ptCurve[0] = 0.0f;
    s.ptMs[1] = 100.0f;
    s.ptLevel[1] = 0.8f;
    s.ptCurve[1] = 0.0f;
    s.decayMs = 50.0f;
    s.sustain = 0.5f;
    const float sr = 1000.0f; // 1 sample per ms
    env.noteOn ();
    std::vector<float> v;
    for (int i = 0; i < 400; ++i)
        v.push_back (env.process (s, sr));
    CHECK (std::fabs (v[9] - 1.0f) < 0.02f, "peak at 10 ms: %f", v[9]);
    CHECK (std::fabs (v[60] - 0.6f) < 0.03f, "halfway down to breakpoint 1 (linear): %f", v[60]);
    CHECK (std::fabs (v[109] - 0.2f) < 0.02f, "breakpoint 1 level: %f", v[109]);
    CHECK (std::fabs (v[209] - 0.8f) < 0.02f, "breakpoint 2 level: %f", v[209]);
    CHECK (std::fabs (v[300] - 0.5f) < 0.02f, "sustain: %f", v[300]);
    CHECK (env.stage == Envelope::Sustain, "stage %d", (int)env.stage);

    // curve bends: negative = fast start, positive = slow start, same end points
    for (float c : {-1.0f, -0.5f, 0.5f, 1.0f})
    {
        CHECK (std::fabs (envCurve (0.0f, c)) < 1e-5f && std::fabs (envCurve (1.0f, c) - 1.0f) < 1e-5f, "ends %f", c);
        CHECK (c < 0 ? envCurve (0.5f, c) > 0.6f : envCurve (0.5f, c) < 0.4f, "bend %f -> %f", c, envCurve (0.5f, c));
    }
    Envelope a, b;
    EnvSettings lin, bent;
    lin.attackMs = bent.attackMs = 100.0f;
    bent.curveA = 0.8f;
    a.noteOn ();
    b.noteOn ();
    float va = 0, vb = 0;
    for (int i = 0; i < 50; ++i)
    {
        va = a.process (lin, sr);
        vb = b.process (bent, sr);
    }
    CHECK (vb < va * 0.6f, "slow-start attack should lag linear at the midpoint: %f vs %f", vb, va);
    // incremental evaluation matches the closed form
    Envelope c3;
    EnvSettings sc;
    sc.attackMs = 0.1f;
    sc.decayMs = 200.0f;
    sc.sustain = 0.0f;
    sc.curveD = -0.7f;
    c3.noteOn ();
    c3.process (sc, sr);
    float worst = 0;
    for (int i = 1; i < 200; ++i)
    {
        const float got = c3.process (sc, sr);
        const float want = 1.0f - envCurve ((float)i / 200.0f, -0.7f);
        worst = std::max (worst, std::fabs (got - want));
    }
    CHECK (worst < 0.01f, "incremental vs closed form error %f", worst);
}

TEST (lfo_tremolo)
{
    auto s = sine (1000.0, 3.0);
    std::unique_ptr<Engine> e (makeEngine (s));
    e->setParam (kLfoOn, 1);
    e->setParam (kLfoVol, 1.0);
    e->setParam (kLfoRate, 4.0);
    e->setParam (kLfoRetrig, 1);
    e->noteOn (60, 1.0f);
    auto o = run (*e, 48000);
    double lo = 1e9, hi = 0;
    for (size_t a = 0; a + 1200 <= 48000; a += 1200)
    {
        const double r = rms (o.l, a, a + 1200);
        lo = std::min (lo, r);
        hi = std::max (hi, r);
    }
    CHECK (lo < hi * 0.3, "tremolo depth too small: %f..%f", lo, hi);
}

TEST (filter_attenuates_and_is_stable)
{
    const float sr = 48000.0f;
    for (int circuit = 0; circuit < 5; ++circuit)
        for (int type = 0; type < 5; ++type)
            for (int slope = 0; slope < 2; ++slope)
            {
                MultiFilter f;
                FilterSettings s;
                s.type = type;
                s.circuit = circuit;
                s.slope24 = slope == 1;
                s.cutoff = 500.0f;
                s.res = 1.0f;
                s.driveDb = 24.0f;
                s.morph = 0.4f;
                f.setup (s, sr);
                uint32_t seed = 3;
                float pk = 0.0f;
                bool ok = true;
                for (int i = 0; i < 96000; ++i)
                {
                    if (i % 16 == 0)
                    {
                        s.cutoff = 50.0f + 15000.0f * (0.5f + 0.5f * std::sin (i * 0.0003f));
                        f.setup (s, sr);
                    }
                    const float y = f.process (randomBipolar (seed), 0);
                    ok &= std::isfinite (y);
                    pk = std::max (pk, std::fabs (y));
                }
                CHECK (ok && pk < 40.0f, "circuit %d type %d slope %d unstable, peak %f", circuit, type, slope, pk);
            }
    // attenuation of a 5 kHz tone through a 500 Hz low-pass
    for (int circuit = 0; circuit < 5; ++circuit)
        for (int slope = 0; slope < 2; ++slope)
        {
            MultiFilter f;
            FilterSettings s;
            s.circuit = circuit;
            s.slope24 = slope == 1;
            s.cutoff = 500.0f;
            f.setup (s, sr);
            double in = 0, out = 0;
            for (int i = 0; i < 48000; ++i)
            {
                const float x = 0.1f * std::sin (2.0f * (float)M_PI * 5000.0f * i / sr);
                const float y = f.process (x, 0);
                if (i > 4800)
                {
                    in += x * x;
                    out += y * y;
                }
            }
            const double db = 10 * std::log10 (out / in);
            CHECK (db < (slope ? -55 : -30), "circuit %d slope %d only %f dB", circuit, slope, db);
            // and the display model agrees on the slope
            const float disp = filterResponseDb (s, 5000.0f);
            // Below about -70 dB the measurement hits the float noise floor (MSVC's /fp:fast makes
            // it a few dB higher), so there both only need to agree that the tone is gone.
            CHECK (std::fabs (disp - db) < 6.0 || (disp < -70.0 && db < -66.0),
                   "display %f vs measured %f (circuit %d slope %d)", disp, db, circuit, slope);
        }
}

TEST (filter_in_engine_tracks_cutoff)
{
    auto s = sine (4000.0, 1.0);
    std::unique_ptr<Engine> e (makeEngine (s));
    e->noteOn (60, 1.0f);
    const double open = rms (run (*e, 9600).l, 4800);
    e->reset ();
    e->setParam (kFilterFreq, 400.0);
    e->setParam (kFilterSlope, 1);
    e->noteOn (60, 1.0f);
    const double closed = rms (run (*e, 9600).l, 4800);
    CHECK (closed < open * 0.01, "closed %f open %f", closed, open);
    // envelope amount opens it again
    e->reset ();
    e->setParam (kFilterEnvAmt, 72.0);
    e->setParam (kFiltS, 1.0);
    e->noteOn (60, 1.0f);
    const double env = rms (run (*e, 9600).l, 4800);
    CHECK (env > open * 0.5, "env amount should open the filter: %f", env);
}

TEST (slicing_transients)
{
    const std::vector<double> times = {0.0, 0.25, 0.5, 0.625, 0.75, 1.0, 1.375, 1.75};
    auto s = bursts (times, 2.0);
    ParamArray p = defaultParams ();
    SliceSettings k;
    k.sliceBy = kSliceTransient;
    k.sensitivity = 0.9;
    k.regionStart = 0;
    k.regionEnd = s->length;
    SliceList sl;
    computeSlices (*s, nullptr, k, sl);
    CHECK (sl.count == (int)times.size (), "found %d slices (want %zu)", sl.count, times.size ());
    for (int i = 0; i < std::min (sl.count, (int)times.size ()); ++i)
    {
        const double err = std::fabs (sl.pos[i] / s->sampleRate - times[(size_t)i]);
        CHECK (err < 0.004, "slice %d at %f s, want %f", i, sl.pos[i] / s->sampleRate, times[(size_t)i]);
    }
    // lower sensitivity finds fewer or equal slices
    k.sensitivity = 0.0;
    SliceList few;
    computeSlices (*s, nullptr, k, few);
    CHECK (few.count <= sl.count && few.count >= 1, "sens 0 -> %d", few.count);

    // Region / Beat
    k.sliceBy = kSliceRegion;
    k.regions = 3; // 16
    computeSlices (*s, nullptr, k, sl);
    CHECK (sl.count == 16, "regions %d", sl.count);
    k.sliceBy = kSliceBeat;
    k.warpBeats = 4;
    k.division = 2; // 1/4 -> one per beat
    computeSlices (*s, nullptr, k, sl);
    CHECK (sl.count == 4, "beat slices %d", sl.count);

    // Manual edits: add one, suppress one
    SliceEdits ed;
    ed.manual.push_back (0.1);
    ed.suppressed.push_back (0.5);
    k.sliceBy = kSliceRegion;
    k.regions = 2; // 8 regions at 0,1/8,...
    computeSlices (*s, &ed, k, sl);
    bool hasManual = false, hasSuppressed = false;
    for (int i = 0; i < sl.count; ++i)
    {
        hasManual |= sl.manual[i] && std::abs (sl.pos[i] - (int)(0.1 * s->length)) < 2;
        hasSuppressed |= std::abs (sl.pos[i] - s->length / 2) < 2;
    }
    CHECK (sl.count == 8 && hasManual && !hasSuppressed, "edits count %d manual %d suppressed %d", sl.count,
           hasManual, hasSuppressed);
    for (int i = 1; i < sl.count; ++i)
        CHECK (sl.pos[i] > sl.pos[i - 1], "slices not sorted");
}

TEST (slicing_playback)
{
    const std::vector<double> times = {0.0, 0.5, 1.0, 1.5};
    auto s = bursts (times, 2.0);
    std::unique_ptr<Engine> e (makeEngine (s));
    e->setParam (kFilterOn, 0);
    e->setParam (kMode, kModeSlicing);
    e->setParam (kSliceBy, kSliceRegion);
    e->setParam (kRegions, 0); // 2 regions? index 0 -> 2
    e->setParam (kRegions, 1); // 4 regions
    CHECK (e->currentSlices ().count == 4, "slices %d", e->currentSlices ().count);
    e->noteOn (kSliceBaseNote + 2, 1.0f);
    auto o = run (*e, 48000);
    const double end = soundEnd (o.l, 1e-4f) / kHostSr;
    CHECK (end > 0.08 && end < 0.52, "slice should stop at next slice, ended %f", end);
    CHECK (e->activeVoices () == 0, "voices %d", e->activeVoices ());
    // Thru continues to the end of the region
    e->setParam (kSlicePlayback, kSliceThru);
    e->noteOn (kSliceBaseNote + 2, 1.0f);
    o = run (*e, 48000);
    CHECK (rms (o.l, 26000, 30000) > 0.01, "thru should reach the next slice's burst");
    // notes outside the slice range are ignored
    e->reset ();
    e->noteOn (kSliceBaseNote + 10, 1.0f);
    e->noteOn (kSliceBaseNote - 1, 1.0f);
    CHECK (e->activeVoices () == 0, "out-of-range notes %d", e->activeVoices ());
    // Poly allows overlapping slices
    e->setParam (kSlicePlayback, kSlicePoly);
    e->noteOn (kSliceBaseNote, 1.0f);
    e->noteOn (kSliceBaseNote + 1, 1.0f);
    run (*e, 64);
    CHECK (e->activeVoices () == 2, "poly slices %d", e->activeVoices ());
}

// Warp: a 2 s sample declared as 4 beats (120 BPM) must follow the host tempo.
TEST (warp_tempo_sync_all_modes)
{
    auto s = sine (440.0, 2.0, 44100.0, true);
    const char* names[] = {"Beats", "Tones", "Texture", "Re-Pitch", "Complex", "Complex Pro"};
    for (int mode = 0; mode < 6; ++mode)
        for (double bpm : {60.0, 240.0})
        {
            std::unique_ptr<Engine> e (makeEngine (s));
            e->setParam (kFilterOn, 0);
            e->setParam (kMode, kModeOneShot);
            e->setParam (kFadeOut, 0.0);
            e->setParam (kWarp, 1);
            e->setParam (kWarpMode, mode);
            e->setParam (kWarpBeats, 4);
            HostInfo h;
            h.bpm = bpm;
            e->noteOn (60, 1.0f);
            auto o = run (*e, (int)(kHostSr * 5), h);
            const double want = 4.0 * 60.0 / bpm;
            const double end = (soundEnd (o.l, 0.01f) + 1) / kHostSr;
            CHECK (std::fabs (end - want) < 0.03 * want + 0.03, "%s @%g BPM: %f s (want %f)", names[mode], bpm, end,
                   want);
            CHECK (finite (o.l) && peak (o.l) < 1.5, "%s peak %f", names[mode], peak (o.l));
            const double f = freqOf (o.l, (size_t)(0.1 * kHostSr), (size_t)(0.8 * want * kHostSr));
            const double wantF = mode == kWarpRePitch ? 440.0 * bpm / 120.0 : 440.0;
            CHECK (std::fabs (f / wantF - 1.0) < 0.02, "%s @%g BPM pitch %f (want %f)", names[mode], bpm, f, wantF);
            if (mode != kWarpRePitch && mode != kWarpBeatsMode && mode != kWarpTexture)
            {
                // steady level (no big grain/frame amplitude modulation)
                const double a = rms (o.l, (size_t)(0.3 * want * kHostSr), (size_t)(0.5 * want * kHostSr));
                const double b = rms (o.l, (size_t)(0.5 * want * kHostSr), (size_t)(0.7 * want * kHostSr));
                CHECK (std::fabs (a / b - 1.0) < 0.1 && a > 0.25, "%s level %f / %f", names[mode], a, b);
            }
        }
}

TEST (warp_transpose_keeps_tempo)
{
    auto s = sine (440.0, 2.0, 44100.0, false);
    for (int mode : {kWarpTones, kWarpTexture, kWarpComplex, kWarpComplexPro, kWarpBeatsMode})
    {
        std::unique_ptr<Engine> e (makeEngine (s));
        e->setParam (kFilterOn, 0);
        e->setParam (kMode, kModeOneShot);
        e->setParam (kWarp, 1);
        e->setParam (kWarpMode, mode);
        e->setParam (kWarpBeats, 4);
        e->setParam (kTextureFlux, 0.0);
        HostInfo h;
        h.bpm = 90.0;
        e->noteOn (67, 1.0f); // +7 semitones
        auto o = run (*e, (int)(kHostSr * 4), h);
        const double want = 4.0 * 60.0 / 90.0;
        const double end = (soundEnd (o.l, 0.01f) + 1) / kHostSr;
        CHECK (std::fabs (end - want) < 0.03 * want + 0.03, "mode %d length %f want %f", mode, end, want);
        if (mode != kWarpBeatsMode)
        {
            const double f = freqOf (o.l, (size_t)(0.2 * kHostSr), (size_t)(2.0 * kHostSr));
            const double wantF = 440.0 * std::pow (2.0, 7.0 / 12.0);
            CHECK (std::fabs (f / wantF - 1.0) < 0.02, "mode %d pitch %f want %f", mode, f, wantF);
        }
    }
}

TEST (warp_beats_preserves_transients)
{
    // 4 bursts over 2 s = 1 per beat at 120 BPM. At 60 BPM each burst must start at its beat.
    auto s = bursts ({0.0, 0.5, 1.0, 1.5}, 2.0);
    for (int loopMode : {0, 1, 2})
    {
        std::unique_ptr<Engine> e (makeEngine (s));
        e->setParam (kFilterOn, 0);
        e->setParam (kMode, kModeOneShot);
        e->setParam (kWarp, 1);
        e->setParam (kWarpMode, kWarpBeatsMode);
        e->setParam (kBeatsLoop, loopMode);
        e->setParam (kWarpBeats, 4);
        HostInfo h;
        h.bpm = 60.0;
        e->noteOn (60, 1.0f);
        auto o = run (*e, (int)(kHostSr * 4.2), h);
        for (int b = 1; b < 4; ++b)
        {
            // energy right after the beat should be far above the energy just before it (for loop off)
            const size_t at = (size_t)(b * kHostSr);
            const double after = rms (o.l, at, at + 2400);
            CHECK (after > 0.1, "loop %d beat %d missing transient (%f)", loopMode, b, after);
            if (loopMode == 0)
            {
                const double before = rms (o.l, at - 4800, at - 100);
                CHECK (before < after * 0.2, "loop off: gap expected before beat %d (%f vs %f)", b, before, after);
            }
        }
    }
}

TEST (warp_classic_loop)
{
    auto s = sine (440.0, 2.0);
    for (int mode : {kWarpBeatsMode, kWarpComplex, kWarpTones})
    {
        std::unique_ptr<Engine> e (makeEngine (s));
        e->setParam (kFilterOn, 0);
        e->setParam (kWarp, 1);
        e->setParam (kWarpMode, mode);
        e->setParam (kWarpBeats, 4);
        e->setParam (kLoopOn, 1);
        e->setParam (kLength, 0.5);
        HostInfo h;
        h.bpm = 120.0;
        e->noteOn (60, 1.0f);
        auto o = run (*e, (int)(kHostSr * 6), h);
        CHECK (rms (o.l, (size_t)(5.0 * kHostSr), (size_t)(6.0 * kHostSr)) > 0.2, "mode %d loop should sustain",
               mode);
        CHECK (finite (o.l), "mode %d non-finite", mode);
    }
}

TEST (file_decode_and_ops)
{
    namespace fs = std::filesystem;
    const fs::path tmp = fs::temp_directory_path () / ("smempler_test_" + std::to_string (std::rand ()));
    fs::create_directories (tmp);
    const std::string dir = tmp.string ();
    const std::string wav = std::string (dir) + "/ramp.wav";
    drwav_data_format fmt {};
    fmt.container = drwav_container_riff;
    fmt.format = DR_WAVE_FORMAT_IEEE_FLOAT;
    fmt.channels = 2;
    fmt.sampleRate = 22050;
    fmt.bitsPerSample = 32;
    drwav w;
    CHECK (drwav_init_file_write (&w, wav.c_str (), &fmt, nullptr), "open for write");
    std::vector<float> inter (2 * 1000);
    for (int i = 0; i < 1000; ++i)
    {
        inter[(size_t)i * 2] = 0.25f * i / 1000.0f;
        inter[(size_t)i * 2 + 1] = -0.25f * i / 1000.0f;
    }
    drwav_write_pcm_frames (&w, 1000, inter.data ());
    drwav_uninit (&w);

    std::string err;
    auto a = SampleData::load (wav, {}, err);
    CHECK (a && a->length == 1000 && a->numChannels == 2 && a->sampleRate == 22050, "load: %s", err.c_str ());
    if (a)
        CHECK (std::fabs (a->ch[1][500] + 0.125f) < 1e-4, "right channel %f", a->ch[1][500]);

    SampleOps ops;
    ops.cropStart = 0.5;
    ops.cropEnd = 1.0;
    ops.reverse = true;
    ops.normalize = true;
    auto b = SampleData::load (wav, ops, err);
    CHECK (b && b->length == 500, "crop length %d", b ? b->length : -1);
    if (b)
    {
        CHECK (std::fabs (b->ch[0][0] - 1.0f) < 0.01f, "reverse+normalize first %f", b->ch[0][0]);
        CHECK (b->ch[0][0] > b->ch[0][499], "reversed ramp should fall");
    }

    // AIFF via afconvert when available (macOS)
    const std::string aif = std::string (dir) + "/ramp.aif";
    const std::string cmd = "afconvert -f AIFF -d BEI16 '" + wav + "' '" + aif + "' 2>/dev/null";
#if __APPLE__
    if (std::system (cmd.c_str ()) == 0)
#else
    if (false)
#endif
    {
        auto c = SampleData::load (aif, {}, err);
        CHECK (c && c->length == 1000 && c->numChannels == 2, "aiff load: %s", err.c_str ());
    }
    // non-ASCII file names (UTF-8 paths must also work on Windows)
    const std::string uni = dir + "/r\xC3\xBC" "ckw\xC3\xA4" "rts \xE2\x99\xAA.wav";
    std::error_code cec;
    fs::copy_file (pathFromUtf8 (wav), pathFromUtf8 (uni), cec);
    auto u = SampleData::load (uni, {}, err);
    CHECK (!cec && u && u->length == 1000, "unicode path load: %s", err.c_str ());
    auto bad = SampleData::load (std::string (dir) + "/missing.wav", {}, err);
    CHECK (!bad && !err.empty (), "missing file should fail cleanly");
    std::error_code ec;
    fs::remove_all (tmp, ec);
}

TEST (snap_to_zero)
{
    auto s = sine (100.0, 0.1);
    const int z = s->snapToZero (230);
    CHECK (std::fabs (s->ch[0][(size_t)z]) < 0.02f, "snap value %f at %d", s->ch[0][(size_t)z], z);
}

TEST (fuzz_random_params_with_sample)
{
    // Random parameter sets across every mode/warp/filter combination with notes, pitch bend
    // and sustain flying around: the output must stay finite and bounded, and nothing may crash.
    auto drums = bursts ({0.0, 0.25, 0.5, 0.75, 1.0, 1.25, 1.5, 1.75}, 2.0);
    auto tone = sine (330.0, 1.5, 96000.0, true);
    uint32_t seed = 12345;
    auto r01 = [&] { return 0.5f + 0.5f * randomBipolar (seed); };
    int worst = 0;
    double worstPeak = 0.0;
    for (int iter = 0; iter < 400; ++iter)
    {
        std::unique_ptr<Engine> e (makeEngine (iter % 2 ? drums : tone));
        for (uint32_t id = 0; id < kNumParams; ++id)
            if (r01 () < 0.6f)
                e->setParam (id, toPlain (id, r01 ()));
        e->setParam (kVolume, 0.0);
        e->setParam (kTailBase + pk::kTailOn, 0.0);
        for (int slot = 0; slot < kRackSlots; ++slot)
            e->setParam (slotParam (slot, kSlotType), (double)kFxEmpty);
        HostInfo h;
        h.bpm = 40.0 + 200.0 * r01 ();
        h.playing = r01 () < 0.5f;
        h.ppqValid = true;
        Out total;
        for (int step = 0; step < 12; ++step)
        {
            const int note = 24 + (int)(r01 () * 80);
            if (r01 () < 0.6f)
                e->noteOn (note, r01 ());
            if (r01 () < 0.4f)
                e->noteOff (24 + (int)(r01 () * 80));
            if (r01 () < 0.2f)
                e->setPitchBend (randomBipolar (seed));
            if (r01 () < 0.1f)
                e->setSustain (r01 () < 0.5f);
            if (r01 () < 0.15f) // automate something mid-note
            {
                const uint32_t id = (uint32_t)(r01 () * (kFxParaOn - 1)); // the sampler (the effects have their own fuzz)
                e->setParam (id, toPlain (id, r01 ()));
            }
            auto o = run (*e, 1024 + (int)(r01 () * 3000), h, 1 + (int)(r01 () * 700));
            total.l.insert (total.l.end (), o.l.begin (), o.l.end ());
        }
        const double pk = peak (total.l);
        if (std::getenv ("SIMPLR_FUZZ_DEBUG") && pk > 8.0)
            std::printf ("    iter %d peak %.1f gain %.0f dB res %.2f drive %.0f circuit %d type %d voices %d spread %.2f mode %d warp %d\n",
                         iter, pk, e->param (kGain), e->param (kFilterRes), e->param (kFilterDrive),
                         (int)e->param (kFilterCircuit), (int)e->param (kFilterType), voicesFromIndex ((int)e->param (kVoices)),
                         e->param (kSpread), (int)e->param (kMode), (int)e->param (kWarp));
        if (pk > worstPeak)
        {
            worstPeak = pk;
            worst = iter;
        }
        CHECK (finite (total.l), "iteration %d produced non-finite output", iter);
    }
    std::printf ("    worst peak %.2f (iteration %d)\n", worstPeak, worst);
    CHECK (worstPeak < 64.0, "runaway level %f at iteration %d", worstPeak, worst);
}

TEST (performance)
{
    auto s = sine (220.0, 4.0, 44100.0, true);
    auto timeIt = [&] (int voices, int warpMode, bool warp) {
        std::unique_ptr<Engine> e (makeEngine (s));
        e->setParam (kVoices, 14); // 32
        e->setParam (kFilterFreq, 3000.0);
        e->setParam (kFilterSlope, 1);
        e->setParam (kLoopOn, 1);
        e->setParam (kWarp, warp ? 1 : 0);
        e->setParam (kWarpMode, warpMode);
        for (int i = 0; i < voices; ++i)
            e->noteOn (40 + i, 0.8f);
        const auto t0 = std::chrono::steady_clock::now ();
        run (*e, (int)kHostSr * 4);
        const double secs = std::chrono::duration<double> (std::chrono::steady_clock::now () - t0).count ();
        return 100.0 * secs / 4.0; // % of one core in real time
    };
    const double classic = timeIt (32, 0, false);
    const double complex8 = timeIt (8, kWarpComplex, true);
    const double beats16 = timeIt (16, kWarpBeatsMode, true);
    std::printf ("    CPU: 32 classic voices %.1f%%, 8 complex voices %.1f%%, 16 beats voices %.1f%%\n", classic,
                 complex8, beats16);
    CHECK (classic < 25.0, "classic too slow %f%%", classic);
    CHECK (complex8 < 40.0, "complex too slow %f%%", complex8);
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
