// Headless tests for the Widr DSP. Run: ./widr_tests [filter]
#include "Engine.h"
#include "Mix.h"
#include "Params.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <vector>

using namespace widr;

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

static uint32_t gSeed = 12345;
static float white ()
{
    gSeed = gSeed * 1664525u + 1013904223u;
    return (float)((gSeed >> 8) & 0xFFFFFF) / 8388608.0f - 1.0f;
}

// Pink noise (Paul Kellet's filter), mono (L = R) or two independent channels.
static Sig pink (double secs, bool stereo, float level = 0.1f)
{
    Sig s;
    const size_t n = (size_t)(secs * kSr);
    s.l.resize (n);
    s.r.resize (n);
    for (int c = 0; c < (stereo ? 2 : 1); ++c)
    {
        double b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0;
        auto& x = c == 0 ? s.l : s.r;
        for (size_t i = 0; i < n; ++i)
        {
            const double w = white ();
            b0 = 0.99886 * b0 + w * 0.0555179;
            b1 = 0.99332 * b1 + w * 0.0750759;
            b2 = 0.96900 * b2 + w * 0.1538520;
            b3 = 0.86650 * b3 + w * 0.3104856;
            b4 = 0.55000 * b4 + w * 0.5329522;
            b5 = -0.7616 * b5 - w * 0.0168980;
            x[i] = (float)((b0 + b1 + b2 + b3 + b4 + b5 + b6 + w * 0.5362) * 0.11 * level * 10.0);
            b6 = w * 0.115926;
        }
    }
    if (!stereo)
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

static std::unique_ptr<Engine> engine ()
{
    auto e = std::make_unique<Engine> ();
    e->prepare (kSr, 512);
    return e;
}

static std::vector<float> sum (const Sig& s, float sign)
{
    std::vector<float> x (s.l.size ());
    for (size_t i = 0; i < x.size (); ++i)
        x[i] = s.l[i] + sign * s.r[i];
    return x;
}

// Level of a single frequency in [a, b) (fit of sin/cos under a Hann window, so energy at nearby
// frequencies does not leak in).
static double toneDb (const std::vector<float>& x, double f, size_t a, size_t b)
{
    double s = 0, c = 0, wsum = 0;
    for (size_t i = a; i < b; ++i)
    {
        const double w = 0.5 - 0.5 * std::cos (2.0 * M_PI * (double)(i - a) / (double)(b - a));
        s += w * x[i] * std::sin (2.0 * M_PI * f * (double)i / kSr);
        c += w * x[i] * std::cos (2.0 * M_PI * f * (double)i / kSr);
        wsum += w;
    }
    const double amp = 2.0 * std::sqrt (s * s + c * c) / wsum;
    return 20.0 * std::log10 (std::max (1e-15, amp));
}

// Energy per band (dB) of x[a, b), averaged over Hann frames.
static std::array<double, kBands> bandDb (const std::vector<float>& x, size_t a, size_t b)
{
    constexpr int n = 8192;
    locus::Fft fft (n);
    std::vector<float> frame (n);
    std::vector<locus::Fft::cf> spec (n / 2 + 1);
    std::array<double, kBands> e {};
    int frames = 0;
    for (size_t pos = a; pos + n <= b; pos += n / 2, ++frames)
    {
        for (int i = 0; i < n; ++i)
            frame[(size_t)i] = x[pos + (size_t)i] * (0.5f - 0.5f * (float)std::cos (2.0 * M_PI * i / n));
        fft.forward (frame.data (), spec.data ());
        for (int k = 0; k < kBands; ++k)
        {
            const int b0 = (int)std::ceil (bandLowHz (k) / (kSr / n)), b1 = (int)std::floor (bandHighHz (k) / (kSr / n));
            for (int bin = b0; bin <= b1 && bin <= n / 2; ++bin)
                e[(size_t)k] += std::norm (spec[(size_t)bin]);
        }
    }
    std::array<double, kBands> db {};
    for (int k = 0; k < kBands; ++k)
        db[(size_t)k] = 10.0 * std::log10 (std::max (1e-20, e[(size_t)k] / std::max (1, frames)));
    return db;
}

static double correlation (const Sig& s, size_t a, size_t b)
{
    double lr = 0, ll = 0, rr = 0;
    for (size_t i = a; i < b; ++i)
    {
        lr += (double)s.l[i] * s.r[i];
        ll += (double)s.l[i] * s.l[i];
        rr += (double)s.r[i] * s.r[i];
    }
    return lr / std::sqrt (std::max (1e-30, ll * rr));
}

static double energy (const std::vector<float>& x, size_t a, size_t b)
{
    double e = 0;
    for (size_t i = a; i < b; ++i)
        e += (double)x[i] * x[i];
    return e / (double)(b - a);
}

// ---------------------------------------------------------------------------
TEST (params)
{
    const auto& t = paramTable ();
    CHECK (t.size () == kNumParams, "size");
    // the plug-in's table: 0.19's entry for entry (what Smemplr's rack hosts), then the cinema stage
    const auto& all = pluginParamTable ();
    CHECK (all.size () == kNumPluginParams && kNumParams == 62 && kCinema == kNumParams, "the plug-in's size");
    bool same = true;
    for (uint32_t id = 0; id < kNumParams; ++id)
        same &= std::string (all.info (id).name) == t.info (id).name && all.info (id).def == t.info (id).def &&
                all.info (id).min == t.info (id).min && all.info (id).max == t.info (id).max;
    CHECK (same, "0.19's parameters as they were");
    CHECK (std::string (all.info (kCinema).name) == "Cinema" && all.info (kCinema).def == 0.0 &&
               std::string (all.info (lanePosition (kLaneAmbience)).name) == "Ambience Position" &&
               all.info (lanePosition (kLaneAmbience)).def == (double)kPosBeyond &&
               std::string (all.info (laneWidth (kLaneVoice)).name) == "Voice Width",
           "the cinema stage: Cinema off, the lanes after it");
    for (uint32_t id = kNumParams; id < kNumPluginParams; ++id)
    {
        double parsed = 0.0;
        CHECK (all.fromText (id, all.toText (id, all.info (id).def), parsed) || all.info (id).disp == pk::Disp::Choice, "%s parses",
               all.info (id).name);
    }
    CHECK (kTailExt3Base == kTailExt2Base + pk::kTailExt2Fields && kTailExt4Base == kTailExt3Base + pk::kTailExt3Fields && kNumParams == kTailExt4Base + pk::kTailExt4Fields && kNumPluginParams == kLaneBase + 2 * kNumLanes &&
               std::string (t.info (kTailExt4Base + pk::kTailExt4Glue12).name) == "Saturator Gentlr Glue 1 / 2" &&
               t.info (kTailExt4Base + pk::kTailExt4Glue2High).def == 0.0 &&
               std::string (t.info (kTailExt3Base + pk::kTailExt3High).name) == "Saturator Gentlr High (unused)" &&
               std::string (t.info (kTailExt2Base + pk::kTailExt2Advanced).name) == "Saturator Gentlr Advanced" &&
               t.info (kTailExt2Base + pk::kTailExt2Threshold).def == -18.0 && t.info (kTailExt2Base + pk::kTailExt2Advanced).def == 0.0,
           "Gentlr's Advanced block (the end saturator's), its High band block, then its glue block last (off)");
    for (uint32_t id = 0; id < kNumParams; ++id)
    {
        const auto& info = t.info (id);
        const double back = t.toPlain (id, t.toNormalized (id, info.def));
        CHECK (std::fabs (back - info.def) < 1e-9 * std::max (1.0, std::fabs (info.max - info.min)), "%s default %f -> %f",
               info.name, info.def, back);
        double parsed = 0.0;
        CHECK (t.fromText (id, t.toText (id, info.def), parsed) || info.disp == pk::Disp::Choice ||
                   info.disp == pk::Disp::OnOff,
               "%s parses its own text", info.name);
    }
    CHECK (t.toText (kWidth, 1.0) == "100 %" && t.toText (kDecay, 1200.0) == "1.20 s", "%s / %s",
           t.toText (kWidth, 1.0).c_str (), t.toText (kDecay, 1200.0).c_str ());
    CHECK (t.info (kCharacter).def == (double)kWide && t.info (kRole).def == (double)kSupport, "Wide, Support");
    CHECK (t.info ((uint32_t)kTailBase + pk::kTailOn).def == 0.0 && t.info ((uint32_t)kTailBase + pk::kTailDrive).def == 0.0, "saturator off, 0 dB");
}

TEST (width_zero_is_bit_exact)
{
    // everything that would act is turned up; Width 0 must still pass the input untouched (delayed
    // by the tail's constant latency)
    auto in = pink (1.0, true, 0.3f);
    auto e = engine ();
    e->setParam (kWidth, 0.0);
    e->setParam (kAir, 6.0);
    e->setParam (kBeyond, 1.0);
    e->setParam (kSpace, 1.0);
    e->setParam (kGuard, 0.0);
    e->reset ();
    auto out = run (*e, in);
    const size_t lat = (size_t)e->latency ();
    CHECK (lat > 0 && lat < 200, "latency %zu", lat);
    size_t diff = 0;
    for (size_t i = lat; i < in.l.size (); ++i)
        diff += (out.l[i] != in.l[i - lat]) + (out.r[i] != in.r[i - lat]);
    CHECK (diff == 0, "%zu samples differ", diff);
}

TEST (widens_a_mono_source)
{
    auto in = pink (3.0, false);
    auto widthCorr = [&] (double w) {
        auto e = engine ();
        e->setParam (kWidth, w);
        e->setParam (kGuard, 0.0);
        e->reset ();
        return correlation (run (*e, in), 48000, in.l.size ());
    };
    const double c1 = widthCorr (1.0), c2 = widthCorr (2.0);
    CHECK (c1 < 0.85, "Width 100 %%: correlation %f", c1);
    CHECK (c2 < c1 - 0.1, "Width 200 %% is wider: %f vs %f", c2, c1);
    for (int c = 0; c < kNumCharacters; ++c)
    {
        auto e = engine ();
        e->setParam (kCharacter, c);
        e->reset ();
        auto out = run (*e, in);
        const double side = energy (sum (out, -1.0f), 48000, in.l.size ()), mid = energy (sum (out, 1.0f), 48000, in.l.size ());
        CHECK (side > mid * 0.05 && side < mid * 2.0, "Character %d: side / mid %f", c, side / mid);
    }
}

TEST (characters_sound_different)
{
    // every pair of Characters differs clearly in at least one of: side level, how long the side
    // rings after the source stops (room and reverb), and how the side is spread over the spectrum
    auto in = pink (2.0, false);
    in.l.resize (in.l.size () + 48000, 0.0f);
    in.r.resize (in.l.size (), 0.0f);
    struct Profile
    {
        double level, ring;
        std::array<double, kBands> spectrum;
    };
    std::array<Profile, kNumCharacters> prof {};
    for (int c = 0; c < kNumCharacters; ++c)
    {
        auto e = engine ();
        e->setParam (kCharacter, c);
        e->reset ();
        auto out = run (*e, in);
        auto side = sum (out, -1.0f), mid = sum (out, 1.0f);
        prof[(size_t)c].level = 10.0 * std::log10 (energy (side, 48000, 96000) / energy (mid, 48000, 96000));
        prof[(size_t)c].ring = 10.0 * std::log10 (std::max (1e-20, energy (side, 96000 + 2400, 96000 + 14400)) / energy (side, 48000, 96000));
        prof[(size_t)c].spectrum = bandDb (side, 48000, 96000);
        std::printf ("    Character %d: side %.1f dB, ring %.1f dB\n", c, prof[(size_t)c].level, prof[(size_t)c].ring);
    }
    for (int a = 0; a < kNumCharacters; ++a)
        for (int b = a + 1; b < kNumCharacters; ++b)
        {
            double shape = 0.0;
            const double offset = prof[(size_t)a].level - prof[(size_t)b].level;
            for (int k = 0; k < kBands; ++k)
                if (bandHz (k) >= 200.0 && bandHz (k) <= 12000.0)
                    shape = std::max (shape, std::fabs (prof[(size_t)a].spectrum[(size_t)k] - prof[(size_t)b].spectrum[(size_t)k] - offset));
            const bool differ = std::fabs (offset) > 1.5 || std::fabs (prof[(size_t)a].ring - prof[(size_t)b].ring) > 4.0 || shape > 3.0;
            CHECK (differ, "Characters %d and %d: level %.1f dB, ring %.1f dB, shape %.1f dB apart", a, b, offset,
                   prof[(size_t)a].ring - prof[(size_t)b].ring, shape);
        }
}

TEST (contrast_keeps_hits_in_the_centre)
{
    // short noise bursts in the mid every 300 ms: with contrast the added side ducks under the
    // bursts and comes back after them
    Sig in;
    const size_t n = 48000 * 4;
    in.l.assign (n, 0.0f);
    gSeed = 5;
    for (size_t i = 0; i < n; ++i)
    {
        const size_t ph = i % 14400;
        const float bed = 0.02f * white ();
        in.l[i] = bed + (ph < 1200 ? 0.8f * white () * (1.0f - (float)ph / 1200.0f) : 0.0f);
    }
    in.r = in.l;
    auto hitVsGap = [&] (double contrast) {
        auto e = engine ();
        e->setParam (kCharacter, kWide);
        e->setParam (kSpace, 0.0);
        e->setParam (kContrast, contrast);
        e->setParam (kGuard, 0.0);
        e->reset ();
        auto out = run (*e, in);
        const size_t lat = (size_t)e->latency ();
        auto side = sum (out, -1.0f), mid = sum (out, 1.0f);
        double sHit = 0, mHit = 0, sGap = 0, mGap = 0;
        for (size_t i = 48000; i + lat < n; ++i)
        {
            const size_t ph = i % 14400, j = i + lat;
            if (ph < 1200)
            {
                sHit += (double)side[j] * side[j];
                mHit += (double)mid[j] * mid[j];
            }
            else if (ph > 4800 && ph < 12000)
            {
                sGap += (double)side[j] * side[j];
                mGap += (double)mid[j] * mid[j];
            }
        }
        // side relative to mid during the hits, minus the same in the gaps
        return 10.0 * std::log10 ((sHit / mHit) / (sGap / mGap));
    };
    const double flat = hitVsGap (0.0), contrasted = hitVsGap (1.0);
    CHECK (contrasted < flat - 4.0, "the hits stay narrower than the gaps: %.1f dB (no contrast %.1f dB)", contrasted, flat);
    // spectral contrast: a strong 1 kHz tone in the mid makes the side give way there
    auto tone = pink (3.0, false, 0.05f);
    for (size_t i = 0; i < tone.l.size (); ++i)
        tone.l[i] = tone.r[i] = tone.l[i] + (float)(0.3 * std::sin (2.0 * M_PI * 1000.0 * (double)i / kSr));
    auto e = engine ();
    e->setParam (kContrast, 1.0);
    e->setParam (kGuard, 0.0);
    e->reset ();
    run (*e, tone);
    int k1 = 0;
    for (int k = 0; k < kBands; ++k)
        if (std::fabs (std::log2 (bandHz (k) / 1000.0)) < std::fabs (std::log2 (bandHz (k1) / 1000.0)))
            k1 = k;
    CHECK (e->bandGain (k1) < 0.6f * e->bandGain (k1 + 5), "the side gives way where the mid is strong: %.2f vs %.2f",
           e->bandGain (k1), e->bandGain (k1 + 5));
}

TEST (mono_below_is_mono)
{
    // a stereo low tone a quarter of the crossover down, plus stereo noise above; every Character,
    // everything up: L - R at the low tone stays 80 dB under L + R
    for (double xover : {100.0, 150.0, 400.0})
        for (int c = 0; c < kNumCharacters; ++c)
        {
            const double f = xover / 4.0;
            auto in = pink (2.0, true, 0.05f);
            for (size_t i = 0; i < in.l.size (); ++i)
            {
                const double t = (double)i / kSr;
                in.l[i] += (float)(0.5 * std::sin (2.0 * M_PI * f * t));
                in.r[i] += (float)(0.2 * std::sin (2.0 * M_PI * f * t + 1.0));
            }
            auto e = engine ();
            e->setParam (kMonoBelow, xover);
            e->setParam (kCharacter, c);
            e->setParam (kWidth, 2.0);
            e->setParam (kSpace, 1.0);
            e->setParam (kBeyond, 1.0);
            e->setParam (kAir, 6.0);
            e->setParam (kGuard, 0.0);
            e->reset ();
            auto out = run (*e, in);
            const size_t a = 48000, b = in.l.size ();
            const double rel = toneDb (sum (out, -1.0f), f, a, b) - toneDb (sum (out, 1.0f), f, a, b);
            CHECK (rel < -80.0, "Mono Below %.0f Hz, Character %d: L-R at %.1f Hz is %.1f dB", xover, c, f, rel);
        }
}

TEST (mono_fold_keeps_the_spectrum)
{
    // Width 200 %, Guard 100 %, every Character, mono and stereo pink noise: L + R per band stays
    // within 1.5 dB of the input's
    for (bool stereo : {false, true})
    {
        auto in = pink (6.0, stereo);
        for (int c = 0; c < kNumCharacters; ++c)
        {
            auto e = engine ();
            e->setParam (kCharacter, c);
            e->setParam (kWidth, 2.0);
            e->setParam (kGuard, 1.0);
            e->reset ();
            auto out = run (*e, in);
            const size_t lat = (size_t)e->latency ();
            std::vector<float> mono = sum (out, 1.0f);
            mono.erase (mono.begin (), mono.begin () + (long)lat);
            mono.resize (in.l.size (), 0.0f);
            const auto got = bandDb (mono, 0, in.l.size () - 8192);
            const auto want = bandDb (sum (in, 1.0f), 0, in.l.size () - 8192);
            double worst = 0.0;
            int worstBand = 0;
            for (int k = 0; k < kBands; ++k)
                if (bandHz (k) >= 200.0 && bandHz (k) <= 16000.0 && std::fabs (got[(size_t)k] - want[(size_t)k]) > worst)
                {
                    worst = std::fabs (got[(size_t)k] - want[(size_t)k]);
                    worstBand = k;
                }
            CHECK (worst <= 1.5, "%s, Character %d: %.2f dB off at %.0f Hz", stereo ? "stereo" : "mono", c, worst, bandHz (worstBand));
        }
    }
}

TEST (voices_are_separate_and_the_guard_holds_the_mono_fold)
{
    // the two voices are unrelated, so even at 200 % the output never goes out of phase (it never
    // cancels in mono); unguarded the mono fold gains energy, Guard 100 % holds that to ~1.2 dB a band
    auto in = pink (4.0, false);
    const auto want = bandDb (sum (in, 1.0f), 0, in.l.size () - 8192);
    auto run2 = [&] (double guard, double& corr, double& worstGain) {
        auto e = engine ();
        e->setParam (kCharacter, kEpic);
        e->setParam (kWidth, 2.0);
        e->setParam (kSpace, 0.6);
        e->setParam (kGuard, guard);
        e->reset ();
        auto out = run (*e, in);
        corr = correlation (out, 96000, in.l.size ());
        const size_t lat = (size_t)e->latency ();
        std::vector<float> mono = sum (out, 1.0f);
        mono.erase (mono.begin (), mono.begin () + (long)lat);
        mono.resize (in.l.size (), 0.0f);
        const auto got = bandDb (mono, 48000, in.l.size () - 8192);
        const auto ref = bandDb (sum (in, 1.0f), 48000, in.l.size () - 8192);
        worstGain = -100.0;
        for (int k = 0; k < kBands; ++k)
            if (bandHz (k) >= 200.0 && bandHz (k) <= 12000.0)
                worstGain = std::max (worstGain, got[(size_t)k] - ref[(size_t)k]);
    };
    (void)want;
    double cFree, gFree, cGuard, gGuard;
    run2 (0.0, cFree, gFree);
    run2 (1.0, cGuard, gGuard);
    CHECK (cFree > 0.0, "unguarded 200 %% stays in phase: correlation %f", cFree);
    CHECK (gFree > 1.5, "unguarded, the mono fold gains energy: %.2f dB", gFree);
    CHECK (gGuard <= 1.5, "Guard 100 %% holds it: %.2f dB", gGuard);
}

TEST (space_rings_and_mono_check)
{
    auto in = pink (1.0, false, 0.3f);
    in.l.resize (3 * 48000, 0.0f);
    in.r.resize (3 * 48000, 0.0f);
    auto tailEnergy = [&] (double space) {
        auto e = engine ();
        e->setParam (kSpace, space);
        e->setParam (kDecay, 2000.0);
        e->reset ();
        auto out = run (*e, in);
        return energy (sum (out, -1.0f), 48000 + 24000, 48000 + 48000); // 0.5 to 1 s after the input stops
    };
    CHECK (tailEnergy (1.0) > 1e3 * std::max (1e-12, tailEnergy (0.0)), "Space rings on: %g vs %g", tailEnergy (1.0),
           tailEnergy (0.0));
    auto e = engine ();
    e->setParam (kMonoCheck, 1.0);
    e->reset ();
    auto out = run (*e, pink (0.5, true));
    bool same = true;
    for (size_t i = 0; i < out.l.size (); ++i)
        same &= out.l[i] == out.r[i];
    CHECK (same, "Mono Check: L == R");
    e->setParam (kMonoCheck, 0.0);
    e->setParam (kWidth, 0.0);
    e->setParam (kOutput, 6.0);
    e->reset ();
    auto loud = run (*e, in);
    const size_t lat = (size_t)e->latency ();
    CHECK (std::fabs (loud.l[20000 + lat] / in.l[20000] - dbToGain (6.0)) < 1e-4, "Output +6 dB");
}

TEST (dry_and_wet_levels)
{
    // a mono source: Wet at -inf leaves no side at all; Dry at -inf leaves only what Widr adds
    auto in = pink (2.0, false);
    auto e = engine ();
    e->setParam (kWetLevel, kLevelMinDb);
    e->reset ();
    auto out = run (*e, in);
    CHECK (energy (sum (out, -1.0f), 48000, in.l.size ()) < 1e-6 * energy (sum (out, 1.0f), 48000, in.l.size ()),
           "Wet -inf: mono in, mono out");
    auto full = engine ();
    auto both = run (*full, in);
    auto d = engine ();
    d->setParam (kDryLevel, kLevelMinDb);
    d->reset ();
    auto wetOnly = run (*d, in);
    const double eBoth = energy (sum (both, 1.0f), 48000, in.l.size ()), eWet = energy (sum (wetOnly, 1.0f), 48000, in.l.size ());
    CHECK (eWet < 0.5 * eBoth && eWet > 1e-4 * eBoth, "Dry -inf: only the added voices (%.1f dB of the mix)", 10.0 * std::log10 (eWet / eBoth));
}

TEST (silence_after_a_burst)
{
    // a loud burst, then a minute of silence: no NaN, no denormals, and it dies away
    for (int c = 0; c < kNumCharacters; ++c)
    {
        auto e = engine ();
        e->setParam (kCharacter, c);
        e->setParam (kWidth, 2.0);
        e->setParam (kSpace, 1.0);
        e->setParam (kDecay, 3000.0);
        e->setParam (kDamping, 20000.0);
        e->reset ();
        auto burst = pink (1.0, true, 1.0f);
        run (*e, burst);
        Sig silence;
        silence.l.assign (48000, 0.0f);
        silence.r.assign (48000, 0.0f);
        bool finite = true, subnormal = false;
        float last = 0.0f;
        for (int s = 0; s < 60; ++s)
        {
            auto out = run (*e, silence);
            for (size_t i = 0; i < out.l.size (); ++i)
                for (float v : {out.l[i], out.r[i]})
                {
                    finite &= std::isfinite (v);
                    subnormal |= std::fpclassify (v) == FP_SUBNORMAL;
                    if (s == 59)
                        last = std::max (last, std::fabs (v));
                }
        }
        CHECK (finite && !subnormal, "Character %d: finite %d, subnormal %d", c, finite, subnormal);
        CHECK (last < 1e-6f, "Character %d: dies away (%g after a minute)", c, last);
    }
}

// --- mix awareness -----------------------------------------------------------------
TEST (registry_claim_release_and_limit)
{
    auto reg = std::make_unique<Registry> ();
    std::vector<int> slots;
    for (int i = 0; i < Registry::kSlots; ++i)
        slots.push_back (reg->claim (reg->newId ()));
    CHECK (std::all_of (slots.begin (), slots.end (), [] (int s) { return s >= 0; }) && reg->used () == 64, "64 slots claimed");
    CHECK (reg->claim (reg->newId ()) == -1, "the 65th finds no slot");
    reg->release (slots[10]);
    CHECK (reg->used () == 63 && !reg->inUse (slots[10]), "released");
    const uint64_t id = reg->newId ();
    CHECK (reg->claim (id) == slots[10] && reg->slot (slots[10]).id.load () == id, "a freed slot is claimed again");
    CHECK (reg->slot (slots[10]).data.side[3].load () == 0.0f && reg->slot (slots[10]).data.group.load () == 1u, "cleared");
}

TEST (registry_heartbeat_expiry)
{
    auto reg = std::make_unique<Registry> ();
    Liveness live;
    const int a = reg->claim (reg->newId ());
    live.update (*reg, 480, kSr);
    CHECK (live.alive (a), "a new instance is alive at once");
    for (int i = 0; i < 90; ++i) // 0.9 s of the reader's audio, beating
    {
        reg->beat (a);
        live.update (*reg, 480, kSr);
    }
    CHECK (live.alive (a), "alive while beating");
    for (int i = 0; i < 90; ++i) // 0.9 s without a beat: still within the second
        live.update (*reg, 480, kSr);
    CHECK (live.alive (a), "still alive after 0.9 s of silence");
    for (int i = 0; i < 20; ++i)
        live.update (*reg, 480, kSr);
    CHECK (!live.alive (a), "gone after 1.1 s without a heartbeat");
    reg->beat (a);
    live.update (*reg, 480, kSr);
    CHECK (live.alive (a), "back with the next beat");
    reg->release (a);
    live.update (*reg, 480, kSr);
    CHECK (!live.alive (a), "released: gone");
}

TEST (negotiation_is_order_independent)
{
    std::vector<Peer> peers (7);
    for (size_t i = 0; i < peers.size (); ++i)
    {
        peers[i].id = 100 + (i * 37) % 11;
        peers[i].role = (int)(i % kNumRoles);
        for (int k = 0; k < kBands; ++k)
            peers[i].side[(size_t)k] = (float)(1e-3 * (double)(1 + (i * 7 + (size_t)k * 3) % 13) * (1.0 + 0.1 * white ()));
    }
    Peer self;
    self.id = 105;
    self.role = kWideRole;
    for (int k = 0; k < kBands; ++k)
        self.side[(size_t)k] = 2e-3f;
    const MixOutcome ref = negotiate (self, peers.data (), (int)peers.size (), 0.7);
    bool same = true;
    for (int perm = 0; perm < 20; ++perm)
    {
        std::rotate (peers.begin (), peers.begin () + 1 + perm % 3, peers.end ());
        if (perm % 2)
            std::reverse (peers.begin (), peers.end ());
        const MixOutcome o = negotiate (self, peers.data (), (int)peers.size (), 0.7);
        same &= o.yield == ref.yield && o.roleScale == ref.roleScale && o.mirror == ref.mirror && o.peers == ref.peers;
    }
    CHECK (same, "the same outcome in any order");
    // alone: neutral; Mix Aware 0: nobody yields
    const MixOutcome alone = negotiate (self, nullptr, 0, 1.0);
    CHECK (alone.roleScale == 1.0f && alone.mirror == 1.0f && alone.yield[5] == 1.0f, "alone");
    const MixOutcome deaf = negotiate (self, peers.data (), (int)peers.size (), 0.0);
    CHECK (std::all_of (deaf.yield.begin (), deaf.yield.end (), [] (float y) { return y == 1.0f; }) && deaf.roleScale == 1.0f,
           "Mix Aware 0 ignores the others");
    // the Anchor yields to nobody; twins of one role widen in opposite directions
    Peer anchor = self;
    anchor.role = kAnchor;
    const MixOutcome a = negotiate (anchor, peers.data (), (int)peers.size (), 1.0);
    CHECK (std::all_of (a.yield.begin (), a.yield.end (), [] (float y) { return y == 1.0f; }) && a.roleScale < 0.5f,
           "Anchor: no yielding, narrower (%f)", a.roleScale);
    Peer twinA = self, twinB = self;
    twinA.id = 1;
    twinB.id = 2;
    const MixOutcome ma = negotiate (twinA, &twinB, 1, 1.0), mb = negotiate (twinB, &twinA, 1, 1.0);
    CHECK (ma.mirror == 1.0f && mb.mirror == -1.0f, "twins mirror: %f / %f", ma.mirror, mb.mirror);
}

// Two engines in one registry and group: an Anchor with side only between ~500 Hz and 2 kHz, a Wide
// with full-band material. The Wide gives way in the Anchor's bands only.
static void anchorAndWide (bool anchorFirst, std::array<float, kBands>& wideGains, int& peersSeen)
{
    auto reg = std::make_unique<Registry> ();
    Engine anchor, wide;
    anchor.prepare (kSr, 480);
    wide.prepare (kSr, 480);
    anchor.setParam (kRole, kAnchor);
    anchor.setParam (kGuard, 0.0);
    wide.setParam (kRole, kWideRole);
    wide.setParam (kGuard, 0.0);
    wide.setParam (kAware, 1.0);
    anchor.reset ();
    wide.reset ();
    MixMember ma (*reg), mw (*reg);
    ma.join ();
    mw.join ();
    // the Anchor's source: pink noise band-passed to 500 Hz .. 2 kHz
    gSeed = 99;
    auto band = pink (6.0, false, 0.3f);
    {
        const auto hp = BiquadCoeffs::highPass (600.0, 0.7, kSr), lp = BiquadCoeffs::lowPass (1600.0, 0.7, kSr);
        Biquad a1, a2, b1, b2;
        for (auto& v : band.l)
            v = (float)b2.tick (lp, b1.tick (lp, a2.tick (hp, a1.tick (hp, v))));
        band.r = band.l;
    }
    auto full = pink (6.0, false);
    std::vector<float> ol (480), outR (480);
    for (size_t pos = 0; pos + 480 <= band.l.size (); pos += 480)
    {
        auto step = [&] (Engine& e, MixMember& m, Sig& in) {
            e.process (in.l.data () + pos, in.r.data () + pos, ol.data (), outR.data (), 480);
            m.update (e, 480);
        };
        if (anchorFirst)
        {
            step (anchor, ma, band);
            step (wide, mw, full);
        }
        else
        {
            step (wide, mw, full);
            step (anchor, ma, band);
        }
    }
    for (int k = 0; k < kBands; ++k)
        wideGains[(size_t)k] = wide.bandGain (k);
    peersSeen = ma.peers () + mw.peers ();
}

TEST (anchor_and_wide_share_the_bands)
{
    std::array<float, kBands> g1 {}, g2 {};
    int seen = 0;
    anchorAndWide (true, g1, seen);
    CHECK (seen == 2, "each sees the other (%d)", seen);
    for (int k = 0; k < kBands; ++k)
    {
        const double f = bandHz (k);
        if (f >= 700.0 && f <= 1300.0)
            CHECK (g1[(size_t)k] < 0.6f, "the Wide yields at %.0f Hz: %f", f, g1[(size_t)k]);
        if (f >= 5000.0 && f <= 12000.0)
            CHECK (g1[(size_t)k] > 0.85f, "and keeps its width at %.0f Hz: %f", f, g1[(size_t)k]);
    }
    int s2 = 0;
    anchorAndWide (false, g2, s2);
    float diff = 0.0f;
    for (int k = 0; k < kBands; ++k)
        diff = std::max (diff, std::fabs (g1[(size_t)k] - g2[(size_t)k]));
    CHECK (diff < 0.02f, "processing order does not matter: %f", diff);
}

TEST (groups_are_separate)
{
    auto reg = std::make_unique<Registry> ();
    Engine a, b;
    a.prepare (kSr, 480);
    b.prepare (kSr, 480);
    b.setParam (kGroup, 2.0);
    MixMember ma (*reg), mb (*reg);
    CHECK (ma.join () && mb.join (), "joined");
    auto in = pink (0.1, true);
    std::vector<float> ol (480), outR (480);
    for (size_t pos = 0; pos + 480 <= in.l.size (); pos += 480)
    {
        a.process (in.l.data () + pos, in.r.data () + pos, ol.data (), outR.data (), 480);
        ma.update (a, 480);
        b.process (in.l.data () + pos, in.r.data () + pos, ol.data (), outR.data (), 480);
        mb.update (b, 480);
    }
    CHECK (ma.peers () == 0 && mb.peers () == 0, "different groups: alone (%d, %d)", ma.peers (), mb.peers ());
    b.setParam (kGroup, 1.0);
    for (int i = 0; i < 3; ++i)
    {
        a.process (in.l.data (), in.r.data (), ol.data (), outR.data (), 480);
        ma.update (a, 480);
        b.process (in.l.data (), in.r.data (), ol.data (), outR.data (), 480);
        mb.update (b, 480);
    }
    CHECK (ma.peers () == 1 && mb.peers () == 1, "same group: they meet");
    mb.leave ();
    ma.update (a, 480);
    CHECK (ma.peers () == 0, "left: alone again");
}

TEST (fuzz_and_automation)
{
    uint32_t seed = 7;
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
            for (uint32_t id = 0; id < kCinema; ++id) // automate everything (the cinema stage: cinema_fuzz_and_silence)
                if (rnd () < 0.3)
                    e.setParam (id, paramTable ().toPlain (id, rnd ()));
            const int n = (int)std::min<size_t> (700, in.l.size () - pos);
            e.process (in.l.data () + pos, in.r.data () + pos, out.l.data () + pos, out.r.data () + pos, n);
        }
        bool finite = true;
        double pk = 0;
        for (size_t i = 0; i < out.l.size (); ++i)
        {
            finite &= std::isfinite (out.l[i]) && std::isfinite (out.r[i]);
            pk = std::max ({pk, (double)std::fabs (out.l[i]), (double)std::fabs (out.r[i])});
        }
        CHECK (finite && pk < 50.0, "iteration %d: finite %d peak %f", iter, finite, pk);
    }
}

TEST (performance)
{
    auto e = engine ();
    e->setParam (kCharacter, kEpic);
    e->setParam (kWidth, 1.5);
    e->setParam (kSpace, 0.5);
    e->setParam (kBeyond, 0.5);
    e->reset ();
    auto in = pink (10.0, true);
    const std::clock_t t0 = std::clock (); // CPU time: other programs running do not count
    run (*e, in);
    const double secs = (double)(std::clock () - t0) / CLOCKS_PER_SEC;
    std::printf ("    CPU: %.2f%% of one core (stereo)\n", 100.0 * secs / 10.0);
    CHECK (secs / 10.0 < 0.05, "too slow");
}

// --- the cinema stage ------------------------------------------------------------------

// A speech-like voice, mono: a gliding pitch (110 .. 170 Hz) with its harmonics up to 5 kHz shaped by
// the formants of a vowel that changes every syllable, syllables at ~4 Hz with a pause every fifth.
static std::vector<float> speechLike (double secs, float rms = 0.1f)
{
    const size_t n = (size_t)(secs * kSr);
    std::vector<float> x (n);
    static const double vowels[4][3] = {{700, 1200, 2600}, {300, 2300, 3000}, {500, 1000, 2500}, {400, 1900, 2600}};
    std::vector<double> ph (40, 0.0);
    uint32_t sd = 77;
    double sylGain = 1.0;
    int lastSyl = -1;
    for (size_t i = 0; i < n; ++i)
    {
        const double t = (double)i / kSr;
        const double f0 = 140.0 + 22.0 * std::sin (2.0 * M_PI * 0.6 * t) + 8.0 * std::sin (2.0 * M_PI * 1.7 * t);
        const double sp = t * 4.2;
        const int syl = (int)sp;
        if (syl != lastSyl)
        {
            lastSyl = syl;
            sd = sd * 1664525u + 1013904223u;
            sylGain = 0.6 + 0.4 * (double)((sd >> 8) & 0xFFFF) / 65536.0;
        }
        const double fr = sp - syl;
        const double env = syl % 5 == 4 ? 0.0 : sylGain * std::pow (std::sin (M_PI * fr), 2.0);
        const double* v = vowels[syl % 4];
        double y = 0.0;
        for (int h = 1; h < 40; ++h)
        {
            const double f = h * f0;
            ph[(size_t)h] += 2.0 * M_PI * f / kSr;
            if (f > 5000.0)
                continue;
            double a = 0.02;
            for (int k = 0; k < 3; ++k)
                a += std::exp (-std::pow ((f - v[k]) / (90.0 + 50.0 * k), 2.0)) / (1.0 + k);
            y += a * std::sin (ph[(size_t)h]) / std::pow ((double)h, 0.3);
        }
        x[i] = (float)(env * y);
    }
    double e = 0.0;
    for (float v : x)
        e += (double)v * v;
    const float g = rms / (float)std::sqrt (e / (double)n);
    for (auto& v : x)
        v *= g;
    return x;
}

// Kick-like hits, mono: a pitch drop 120 -> 50 Hz under a fast decay, with a click, every 0.5 s.
static std::vector<float> kickLike (double secs, float peak = 0.5f)
{
    const size_t n = (size_t)(secs * kSr);
    std::vector<float> x (n, 0.0f);
    gSeed = 31;
    for (size_t start = (size_t)(0.13 * kSr); start < n; start += (size_t)(0.5 * kSr))
    {
        double ph = 0.0;
        for (size_t j = 0; j < (size_t)(0.4 * kSr) && start + j < n; ++j)
        {
            const double t = (double)j / kSr;
            ph += 2.0 * M_PI * (50.0 + 70.0 * std::exp (-t / 0.03)) / kSr;
            const double click = t < 0.003 ? 0.6 * white () * (1.0 - t / 0.003) : 0.0;
            x[start + j] += (float)(peak * (std::exp (-t / 0.12) * std::sin (ph) + click));
        }
    }
    return x;
}

// A sustained chord, mono: C, E, G, C with four harmonics each.
static std::vector<float> chord (double secs, float rms = 0.06f)
{
    const size_t n = (size_t)(secs * kSr);
    std::vector<float> x (n, 0.0f);
    const double notes[4] = {261.63, 329.63, 392.0, 523.25};
    for (size_t i = 0; i < n; ++i)
    {
        const double t = (double)i / kSr;
        double y = 0.0;
        for (double f : notes)
            for (int h = 1; h <= 4; ++h)
                y += std::sin (2.0 * M_PI * f * h * t + h) / h;
        x[i] = (float)(y * std::min (1.0, t / 0.05));
    }
    double e = 0.0;
    for (float v : x)
        e += (double)v * v;
    const float g = rms / (float)std::sqrt (e / (double)n);
    for (auto& v : x)
        v *= g;
    return x;
}

// The test mix's elements: centred speech, centred kicks, a centred chord, and stereo ambience
// (independent noise left and right).
enum Element { kSpeech = 0, kKick, kChord, kAmbience, kNumElements };
static const char* kElementNames[kNumElements] = {"speech", "kick", "chord", "ambience"};
static std::array<Sig, kNumElements> elements (double secs)
{
    std::array<Sig, kNumElements> e;
    e[kSpeech].l = e[kSpeech].r = speechLike (secs);
    e[kKick].l = e[kKick].r = kickLike (secs);
    e[kChord].l = e[kChord].r = chord (secs);
    gSeed = 4242;
    e[kAmbience] = pink (secs, true, 0.04f);
    // (a room or a reverb above 200 Hz: below, everything is the Bass lane's by design)
    const auto hp = BiquadCoeffs::highPass (200.0, 0.7, kSr);
    for (auto* ch : {&e[kAmbience].l, &e[kAmbience].r})
    {
        Biquad a, b;
        for (auto& v : *ch)
            v = (float)b.tick (hp, a.tick (hp, v));
    }
    return e;
}
static Sig mixOf (const std::array<Sig, kNumElements>& e, int without = -1)
{
    Sig m;
    m.l.assign (e[0].l.size (), 0.0f);
    m.r.assign (e[0].l.size (), 0.0f);
    for (int c = 0; c < kNumElements; ++c)
        if (c != without)
            for (size_t i = 0; i < m.l.size (); ++i)
            {
                m.l[i] += e[(size_t)c].l[i];
                m.r[i] += e[(size_t)c].r[i];
            }
    return m;
}

static std::unique_ptr<Engine> cinemaEngine (double cinema = 1.0)
{
    auto e = engine ();
    e->setParam (kCinema, cinema);
    e->reset ();
    return e;
}

TEST (cinema_off_is_widr_as_before)
{
    // Cinema 0 (the default): the stage does not run, no latency added; whatever its other controls say,
    // the output is the same as with them at their defaults, sample for sample
    CHECK (pluginParamTable ().info (kCinema).def == 0.0, "Cinema off by default");
    auto in = pink (1.5, true, 0.2f);
    for (int c = 0; c < kNumCharacters; ++c)
    {
        auto ref = engine ();
        ref->setParam (kCharacter, c);
        ref->reset ();
        auto a = run (*ref, in);
        auto e = engine ();
        e->setParam (kCharacter, c);
        e->setParam (kDepth, 1.0);
        e->setParam (kTheatre, 1.0);
        for (int l = 0; l < kNumLanes; ++l)
        {
            e->setParam (lanePosition (l), kPosBeyond);
            e->setParam (laneWidth (l), 0.3);
        }
        e->reset ();
        auto b = run (*e, in);
        CHECK (e->latency () == ref->latency (), "no latency added");
        CHECK (a.l == b.l && a.r == b.r, "Character %d: the same output", c);
    }
}

TEST (lanes_sum_to_the_input)
{
    // the five lanes always sum to the input, delayed by the latency (float rounding only)
    LaneSplitter split;
    split.prepare (kSr);
    split.setFullLanes (true);
    CHECK (split.latency () == 1023, "latency %d", split.latency ());
    auto el = elements (3.0);
    auto in = mixOf (el);
    double err = 0.0, ref = 0.0;
    LaneSample ls;
    for (size_t i = 0; i < in.l.size (); ++i)
    {
        split.tick (in.l[i], in.r[i], ls);
        const size_t lat = (size_t)split.latency ();
        const float wantL = i >= lat ? in.l[i - lat] : 0.0f, wantR = i >= lat ? in.r[i - lat] : 0.0f;
        float sl = 0.0f, sr = 0.0f;
        for (int j = 0; j < kNumLanes; ++j)
        {
            sl += ls.l[(size_t)j];
            sr += ls.r[(size_t)j];
        }
        err += (double)(sl - wantL) * (sl - wantL) + (double)(sr - wantR) * (sr - wantR);
        ref += (double)wantL * wantL + (double)wantR * wantR;
    }
    const double db = 10.0 * std::log10 (std::max (1e-30, err / ref));
    CHECK (db < -120.0, "the lanes' sum vs the input: %.1f dB", db);
    // the STFT itself reconstructs: a 40 Hz tone (in the Bass lane's bins, but for the window's side
    // lobes) comes back out of the Bass lane, and the rest (Ambience) is almost nothing
    split.reset ();
    double eBass = 0.0, eRest = 0.0, eIn = 0.0;
    for (size_t i = 0; i < 96000; ++i)
    {
        const float x = 0.5f * (float)std::sin (2.0 * M_PI * 40.0 * (double)i / kSr);
        split.tick (x, x, ls);
        if (i < 48000)
            continue;
        const float want = 0.5f * (float)std::sin (2.0 * M_PI * 40.0 * (double)(i - 1023) / kSr);
        eBass += (double)(ls.l[kLaneBass] - want) * (ls.l[kLaneBass] - want);
        eRest += (double)ls.l[kLaneAmbience] * ls.l[kLaneAmbience];
        eIn += (double)want * want;
    }
    CHECK (10.0 * std::log10 (eBass / eIn) < -30.0 && 10.0 * std::log10 (eRest / eIn) < -40.0,
           "a 40 Hz tone through the Bass lane: error %.1f dB, Ambience %.1f dB", 10.0 * std::log10 (eBass / eIn),
           10.0 * std::log10 (eRest / eIn));
}

TEST (elements_land_in_their_lanes)
{
    // The mix's masks applied to each element on its own (the same frames): how much of each element's
    // energy lands in each lane
    const double secs = 6.0;
    auto el = elements (secs);
    auto in = mixOf (el);
    if (const char* only = std::getenv ("WIDR_MIX")) // (experiments: which elements are in the mix)
    {
        std::array<Sig, kNumElements> sel = el;
        for (int c = 0; c < kNumElements; ++c)
            if (!std::strchr (only, '0' + c))
                for (auto* ch : {&sel[(size_t)c].l, &sel[(size_t)c].r})
                    std::fill (ch->begin (), ch->end (), 0.0f);
        in = mixOf (sel);
    }
    LaneSplitter split;
    split.prepare (kSr);
    const int n = split.fftSize (), nb = split.bins ();
    locus::Fft fft (n);
    std::vector<float> frame ((size_t)n);
    std::vector<locus::Fft::cf> xl ((size_t)nb), xr ((size_t)nb);
    std::array<std::array<double, kNumLanes>, kNumElements> share {};
    LaneSample ls;
    uint64_t seen = 0;
    for (size_t i = 0; i < in.l.size (); ++i)
    {
        split.tick (in.l[i], in.r[i], ls);
        if (split.frames () == seen)
            continue;
        seen = split.frames ();
        if (i < (size_t)kSr) // after the first second (the detectors settle)
            continue;
        const size_t start = i + 1 - (size_t)n;
        for (int c = 0; c < kNumElements; ++c)
        {
            const Sig& x = el[(size_t)c];
            for (int k = 0; k < n; ++k)
                frame[(size_t)k] = x.l[start + (size_t)k] * split.window ()[(size_t)k];
            fft.forward (frame.data (), xl.data ());
            for (int k = 0; k < n; ++k)
                frame[(size_t)k] = x.r[start + (size_t)k] * split.window ()[(size_t)k];
            fft.forward (frame.data (), xr.data ());
            for (int k = 0; k < nb; ++k)
            {
                const auto m = 0.5f * (xl[(size_t)k] + xr[(size_t)k]);
                const auto v = split.mask (kLaneVoice)[k] * m;
                const double rest = (double)std::norm (xl[(size_t)k] - v) + std::norm (xr[(size_t)k] - v);
                share[(size_t)c][kLaneVoice] += 2.0 * std::norm (v);
                for (int j = 1; j < kNumLanes; ++j)
                    share[(size_t)c][(size_t)j] += (double)split.mask (j)[k] * split.mask (j)[k] * rest;
            }
        }
    }
    const char* laneNames[kNumLanes] = {"Voice", "Bass", "Hits", "Tones", "Ambience"};
    for (int c = 0; c < kNumElements; ++c)
    {
        double total = 0.0;
        for (double v : share[(size_t)c])
            total += v;
        std::printf ("    %-8s", kElementNames[c]);
        for (int j = 0; j < kNumLanes; ++j)
        {
            share[(size_t)c][(size_t)j] /= std::max (1e-30, total);
            std::printf ("  %s %4.1f %%", laneNames[j], 100.0 * share[(size_t)c][(size_t)j]);
        }
        std::printf ("\n");
    }
    CHECK (share[kSpeech][kLaneVoice] > 0.8, "speech in Voice: %.2f", share[kSpeech][kLaneVoice]);
    CHECK (share[kKick][kLaneBass] + share[kKick][kLaneHits] > 0.8, "the kick in Bass and Hits: %.2f",
           share[kKick][kLaneBass] + share[kKick][kLaneHits]);
    // (a chord held under a voice: where the two share bins, a soft mask splits each bin between them)
    CHECK (share[kChord][kLaneTones] > 0.5 && share[kChord][kLaneTones] + share[kChord][kLaneAmbience] > 0.7,
           "the chord in Tones: %.2f (and Ambience, also wide by default: %.2f)", share[kChord][kLaneTones], share[kChord][kLaneAmbience]);
    CHECK (share[kAmbience][kLaneAmbience] > 0.75, "the ambience in Ambience: %.2f", share[kAmbience][kLaneAmbience]);
}

// Where each element of the mix goes: the mix is rendered once, its lanes' masks recorded, then each
// element on its own through the same masks (what the separation decided for it in the mix), and its
// output's side (L - R) relative to its mid (L + R), in dB, from 1.5 s on. With Cinema 0 (no lanes)
// each element is simply rendered on its own.
static std::array<double, kNumElements> sideOfEach (const std::array<Sig, kNumElements>& el, const std::function<void (Engine&)>& set,
                                                    std::array<Sig, kNumElements>* parts = nullptr)
{
    std::vector<float> masks;
    {
        auto e = engine ();
        set (*e);
        e->reset ();
        e->laneSplitter ().recordMasks (&masks);
        run (*e, mixOf (el));
        e->laneSplitter ().recordMasks (nullptr);
    }
    std::array<double, kNumElements> rel {};
    for (int c = 0; c < kNumElements; ++c)
    {
        auto e = engine ();
        set (*e);
        e->reset ();
        e->laneSplitter ().replayMasks (masks.empty () ? nullptr : &masks);
        const Sig d = run (*e, el[(size_t)c]);
        rel[(size_t)c] = 10.0 * std::log10 (std::max (1e-30, energy (sum (d, -1.0f), 72000, d.l.size ())) /
                                            std::max (1e-30, energy (sum (d, 1.0f), 72000, d.l.size ())));
        if (parts)
            (*parts)[(size_t)c] = d;
    }
    return rel;
}

TEST (centre_lanes_stay_centred_and_the_rest_goes_wide)
{
    auto el = elements (6.0);
    // (Contrast and Guard at 0 here: both act across elements by design, a hit ducking the others'
    // width and the guard sharing a band's width out, so with them on, removing one element changes the
    // others' output too and this measure would count that as the element's own)
    const auto cinemaSet = [] (Engine& e) {
        e.setParam (kCinema, 1.0);
        e.setParam (kContrast, 0.0);
        e.setParam (kGuard, 0.0);
    };
    std::array<Sig, kNumElements> parts;
    const auto rel = sideOfEach (el, cinemaSet, &parts);
    const auto before = sideOfEach (el, [] (Engine& e) {
        e.setParam (kContrast, 0.0);
        e.setParam (kGuard, 0.0);
    });
    for (int c = 0; c < kNumElements; ++c)
        std::printf ("    %-8s side vs mid: %6.1f dB (Cinema 0: %6.1f dB)\n", kElementNames[c], rel[(size_t)c], before[(size_t)c]);
    CHECK (rel[kSpeech] < -25.0, "the speech stays in the centre: side %.1f dB", rel[kSpeech]);
    CHECK (rel[kKick] < -25.0, "the kick stays in the centre: side %.1f dB", rel[kKick]);
    CHECK (rel[kChord] > -8.0, "the chord goes wide: side %.1f dB", rel[kChord]);
    CHECK (rel[kAmbience] > 2.0, "the ambience goes beyond: side %.1f dB (the input's 0 dB)", rel[kAmbience]);
    CHECK (rel[kSpeech] < before[kSpeech] - 15.0 && rel[kKick] < before[kKick], "against Cinema 0: the speech and the kick narrower");
    // the speech's own sound in the centre: its spectrum in the output's mid, per band, close to the input's
    auto e = engine ();
    const size_t lat = (size_t)(e->latency () + 1023);
    std::vector<float> mid = sum (parts[kSpeech], 1.0f);
    mid.erase (mid.begin (), mid.begin () + (long)lat);
    const auto got = bandDb (mid, 72000, mid.size ());
    const auto want = bandDb (sum (el[kSpeech], 1.0f), 72000, mid.size ());
    double worst = 0.0;
    for (int k = 0; k < kBands; ++k)
        if (bandHz (k) >= 250.0 && bandHz (k) <= 4000.0)
            worst = std::max (worst, std::fabs (got[(size_t)k] - want[(size_t)k]));
    CHECK (worst < 1.5, "the speech's spectrum in the centre within %.2f dB of the input's", worst);
}

TEST (a_lane_position_moves_only_its_element)
{
    auto el = elements (6.0);
    const auto base = sideOfEach (el, [] (Engine& e) {
        e.setParam (kCinema, 1.0);
        e.setParam (kContrast, 0.0);
        e.setParam (kGuard, 0.0);
    });
    const auto moved = sideOfEach (el, [] (Engine& e) {
        e.setParam (kCinema, 1.0);
        e.setParam (kContrast, 0.0);
        e.setParam (kGuard, 0.0);
        e.setParam (lanePosition (kLaneVoice), kPosWide);
        e.setParam (laneWidth (kLaneVoice), 1.0);
    });
    for (int c = 0; c < kNumElements; ++c)
        std::printf ("    %-8s side vs mid: %6.1f dB -> %6.1f dB (Voice Wide)\n", kElementNames[c], base[(size_t)c], moved[(size_t)c]);
    CHECK (moved[kSpeech] > base[kSpeech] + 15.0, "Voice Wide: the speech goes wide (%.1f -> %.1f dB)", base[kSpeech], moved[kSpeech]);
    // (the part of the chord the separation gave the Voice lane, where the two share bins, moves with it)
    CHECK (std::fabs (moved[kChord] - base[kChord]) < 3.5 && std::fabs (moved[kAmbience] - base[kAmbience]) < 2.0,
           "the chord and the ambience stay about where they were");
    CHECK (moved[kKick] < -25.0, "the kick stays centred (%.1f dB)", moved[kKick]);
    // and the Tones lane to the Centre: the chord comes in, the rest stays
    const auto tones = sideOfEach (el, [] (Engine& e) {
        e.setParam (kCinema, 1.0);
        e.setParam (kContrast, 0.0);
        e.setParam (kGuard, 0.0);
        e.setParam (lanePosition (kLaneTones), kPosCentre);
        e.setParam (laneWidth (kLaneTones), 0.0);
    });
    for (int c = 0; c < kNumElements; ++c)
        std::printf ("    %-8s side vs mid: %6.1f dB -> %6.1f dB (Tones Centre)\n", kElementNames[c], base[(size_t)c], tones[(size_t)c]);
    CHECK (tones[kChord] < base[kChord] - 3.0, "Tones Centre: the chord comes in (%.1f -> %.1f dB)", base[kChord], tones[kChord]);
    CHECK (std::fabs (tones[kAmbience] - base[kAmbience]) < 2.0 && tones[kSpeech] < -25.0 && tones[kKick] < -25.0,
           "the speech, the kick and the ambience stay where they were");
}

TEST (cinema_mono_fold_is_guarded)
{
    // Cinema 100 %, Width 200 %, Theatre 100 %, every Wide lane at Beyond, Guard 100 %: L + R per band
    // within 1.5 dB of the input's (above 250 Hz: Depth adds its low end below)
    for (bool stereo : {false, true})
    {
        auto in = pink (6.0, stereo);
        auto e = engine ();
        e->setParam (kCinema, 1.0);
        e->setParam (kWidth, 2.0);
        e->setParam (kTheatre, 1.0);
        e->setParam (kGuard, 1.0);
        e->setParam (lanePosition (kLaneTones), kPosBeyond);
        e->setParam (laneWidth (kLaneTones), 1.0);
        e->reset ();
        auto out = run (*e, in);
        const size_t lat = (size_t)e->latency ();
        std::vector<float> mono = sum (out, 1.0f);
        mono.erase (mono.begin (), mono.begin () + (long)lat);
        mono.resize (in.l.size (), 0.0f);
        const auto got = bandDb (mono, 48000, in.l.size () - 8192);
        const auto want = bandDb (sum (in, 1.0f), 48000, in.l.size () - 8192);
        double worst = -100.0;
        int worstBand = 0;
        for (int k = 0; k < kBands; ++k)
            if (bandHz (k) >= 250.0 && bandHz (k) <= 12000.0 && got[(size_t)k] - want[(size_t)k] > worst)
            {
                worst = got[(size_t)k] - want[(size_t)k];
                worstBand = k;
            }
        const double side = energy (sum (out, -1.0f), 96000, out.l.size ()), midE = energy (sum (out, 1.0f), 96000, out.l.size ());
        std::printf ("    %s: mono fold gains at most %.2f dB (at %.0f Hz), side / mid %.1f dB\n", stereo ? "stereo" : "mono", worst,
                     bandHz (worstBand), 10.0 * std::log10 (side / midE));
        CHECK (worst <= 1.5, "%s: the mono fold gains %.2f dB at %.0f Hz", stereo ? "stereo" : "mono", worst, bandHz (worstBand));
        CHECK (correlation (out, 96000, out.l.size ()) > 0.0, "it stays in phase");
    }
}

TEST (depth_deepens_and_stays_mono)
{
    // a bass line (55 .. 82 Hz) with kicks: Depth adds energy below 80 Hz, the output stays mono below
    // Mono Below, and nothing clips even with a loud input
    Sig in;
    const size_t n = (size_t)(4.0 * kSr);
    in.l.resize (n);
    auto kicks = kickLike (4.0, 0.4f);
    gSeed = 9;
    for (size_t i = 0; i < n; ++i)
    {
        const double t = (double)i / kSr;
        const double f = ((int)(t * 2.0) % 2) ? 82.4 : 55.0;
        in.l[i] = (float)(0.3 * std::sin (2.0 * M_PI * f * t)) + kicks[i];
    }
    in.r = in.l;
    for (size_t i = 0; i < n; ++i)
        in.r[i] += 0.05f * white (); // a little stereo above
    auto render = [&] (double depth, float gain) {
        Sig x = in;
        for (size_t i = 0; i < n; ++i)
        {
            x.l[i] *= gain;
            x.r[i] *= gain;
        }
        auto e = engine ();
        e->setParam (kCinema, 1.0);
        e->setParam (kDepth, depth);
        e->setParam (kTheatre, 0.0);
        e->reset ();
        return run (*e, x);
    };
    auto lowDb = [] (const std::vector<float>& x) {
        const auto lp = BiquadCoeffs::lowPass (80.0, 0.7, kSr);
        Biquad a, b;
        double e = 0.0;
        for (size_t i = 0; i < x.size (); ++i)
        {
            const double y = b.tick (lp, a.tick (lp, x[i]));
            if (i >= 48000)
                e += y * y;
        }
        return 10.0 * std::log10 (e);
    };
    const auto flat = render (0.0, 1.0f), deep = render (1.0, 1.0f);
    const double gainDb = lowDb (sum (deep, 1.0f)) - lowDb (sum (flat, 1.0f));
    std::printf ("    Depth 100 %%: %.1f dB more below 80 Hz\n", gainDb);
    CHECK (gainDb > 2.0, "Depth adds low end: %.1f dB", gainDb);
    const double rel = toneDb (sum (deep, -1.0f), 55.0, 48000, n) - toneDb (sum (deep, 1.0f), 55.0, 48000, n);
    CHECK (rel < -80.0, "mono below Mono Below: L-R at 55 Hz %.1f dB", rel);
    const double relSub = toneDb (sum (deep, -1.0f), 27.5, 48000, n);
    CHECK (relSub < toneDb (sum (deep, 1.0f), 27.5, 48000, n) - 80.0, "the sub is mono too");
    const auto loud = render (1.0, 2.5f); // peaks of about 2 (+6 dBFS) going in
    float pk = 0.0f;
    bool finite = true;
    for (size_t i = 0; i < n; ++i)
    {
        pk = std::max ({pk, std::fabs (loud.l[i]), std::fabs (loud.r[i])});
        finite &= std::isfinite (loud.l[i]) && std::isfinite (loud.r[i]);
    }
    CHECK (finite && pk <= 1.0f, "no clipping: peak %.3f", pk);
}

TEST (theatre_never_hears_the_centre)
{
    // centred speech alone, then silence: with the Voice lane in the Centre the hall gets nothing (no
    // tail after the speech stops); move the Voice lane to Wide and the hall rings on
    Sig in;
    in.l = speechLike (3.0, 0.15f);
    in.l.resize ((size_t)(5.0 * kSr), 0.0f);
    in.r = in.l;
    auto tailDb = [&] (int voicePos) {
        auto e = engine ();
        e->setParam (kCinema, 1.0);
        e->setParam (kTheatre, 1.0);
        e->setParam (kSpace, 0.0);
        e->setParam (lanePosition (kLaneVoice), voicePos);
        e->setParam (laneWidth (kLaneVoice), 1.0);
        e->reset ();
        auto out = run (*e, in);
        const size_t lat = (size_t)e->latency ();
        auto both = [&] (size_t a, size_t b) { return energy (out.l, a + lat, b + lat) + energy (out.r, a + lat, b + lat); };
        // 0.4 to 1.4 s after the speech stops, relative to the speech
        return 10.0 * std::log10 (std::max (1e-30, both ((size_t)(3.4 * kSr), (size_t)(4.4 * kSr))) / both (48000, (size_t)(3.0 * kSr)));
    };
    const double centre = tailDb (kPosCentre), wide = tailDb (kPosWide);
    std::printf ("    the tail after the speech: %.1f dB with Voice in the Centre, %.1f dB with Voice Wide\n", centre, wide);
    CHECK (centre < -45.0, "no hall on the centred voice: %.1f dB", centre);
    CHECK (wide > centre + 20.0, "the hall rings once the voice feeds it: %.1f dB", wide);
}

TEST (cinema_latency_and_alignment)
{
    auto e = engine ();
    const int base = e->latency ();
    e->setParam (kCinema, 0.5);
    CHECK (e->latency () == base + e->cinemaLatency () && e->cinemaLatency () == 1023, "Cinema adds the lanes' latency: %d -> %d",
           base, e->latency ());
    // Width 0 with Cinema on: the input, delayed by exactly the latency reported
    auto in = pink (1.0, true, 0.1f);
    e->setParam (kWidth, 0.0);
    e->reset ();
    auto out = run (*e, in);
    const size_t lat = (size_t)e->latency ();
    size_t diff = 0;
    for (size_t i = lat; i < in.l.size (); ++i)
        diff += (out.l[i] != in.l[i - lat]) + (out.r[i] != in.r[i - lat]);
    CHECK (diff == 0, "Width 0: the input delayed by the latency (%zu samples differ)", diff);
    // nothing processed (every lane in the Centre at Width 100 %, no Depth or Theatre): the same as
    // Widr with no voices (Width near 0, Space 0), only later by the lanes' latency
    auto quiet = [&] (Engine& x) {
        x.setParam (kSpace, 0.0);
        x.setParam (kWidth, 2e-6);
    };
    auto ref = engine ();
    quiet (*ref);
    ref->reset ();
    auto a = run (*ref, in);
    auto c = engine ();
    quiet (*c);
    c->setParam (kCinema, 1.0);
    c->setParam (kDepth, 0.0);
    c->setParam (kTheatre, 0.0);
    for (int l = 0; l < kNumLanes; ++l)
    {
        c->setParam (lanePosition (l), kPosCentre);
        c->setParam (laneWidth (l), 1.0);
    }
    c->reset ();
    auto b = run (*c, in);
    double err = 0.0, ref2 = 0.0;
    for (size_t i = 1023; i < in.l.size (); ++i)
    {
        err += std::pow ((double)b.l[i] - a.l[i - 1023], 2.0) + std::pow ((double)b.r[i] - a.r[i - 1023], 2.0);
        ref2 += (double)a.l[i - 1023] * a.l[i - 1023] + (double)a.r[i - 1023] * a.r[i - 1023];
    }
    const double db = 10.0 * std::log10 (std::max (1e-30, err / ref2));
    CHECK (db < -90.0, "nothing processed: the same, 1023 samples later (%.1f dB apart)", db);
}

TEST (cinema_fuzz_and_silence)
{
    // every cinema control automated at random with loud and quiet input, then silence: finite, no
    // denormals, dies away
    uint32_t seed = 3;
    auto rnd = [&] {
        seed = seed * 1664525u + 1013904223u;
        return (double)((seed >> 8) & 0xFFFFFF) / 16777216.0;
    };
    for (double rate : {44100.0, 48000.0, 96000.0})
    {
        Engine e;
        e.prepare (rate, 512);
        e.setParam (kCinema, 1.0);
        e.reset ();
        auto in = pink (2.0, true, 1.0f);
        Sig out;
        out.l.resize (in.l.size ());
        out.r.resize (in.l.size ());
        for (size_t pos = 0; pos < in.l.size (); pos += 600)
        {
            for (uint32_t id = 0; id < kNumPluginParams; ++id) // everything, the cinema stage's too
                if (rnd () < (id >= kCinema ? 0.3 : 0.05) && (id < kTailBase || id >= kCinema))
                    e.setParam (id, pluginParamTable ().toPlain (id, rnd ()));
            if (rnd () < 0.1)
                e.setParam (kCinema, 0.0);
            const int m = (int)std::min<size_t> (600, in.l.size () - pos);
            e.process (in.l.data () + pos, in.r.data () + pos, out.l.data () + pos, out.r.data () + pos, m);
        }
        e.setParam (kCinema, 1.0);
        e.setParam (kTheatre, 1.0);
        e.setParam (kDepth, 1.0);
        run (e, in);
        bool finite = true, subnormal = false;
        float last = 0.0f;
        Sig silence;
        silence.l.assign (48000, 0.0f);
        silence.r.assign (48000, 0.0f);
        for (int s = 0; s < 30; ++s)
        {
            auto o = run (e, silence);
            for (size_t i = 0; i < o.l.size (); ++i)
                for (float v : {o.l[i], o.r[i]})
                {
                    finite &= std::isfinite (v);
                    subnormal |= std::fpclassify (v) == FP_SUBNORMAL;
                    if (s == 29)
                        last = std::max (last, std::fabs (v));
                }
        }
        CHECK (finite && !subnormal, "%.0f Hz: finite %d, subnormal %d", rate, finite, subnormal);
        CHECK (last < 1e-5f, "%.0f Hz: dies away (%g after 30 s)", rate, last);
    }
}

TEST (cinema_performance)
{
    // Cinema 100 %, Theatre 100 %, Depth 100 %, the Epic character (a heavier mode: its own budget, as the
    // other plug-ins' heaviest modes have)
    auto e = engine ();
    e->setParam (kCharacter, kEpic);
    e->setParam (kWidth, 1.5);
    e->setParam (kSpace, 0.5);
    e->setParam (kCinema, 1.0);
    e->setParam (kTheatre, 1.0);
    e->setParam (kDepth, 1.0);
    e->reset ();
    auto in = pink (10.0, true);
    const std::clock_t t0 = std::clock (); // CPU time: other programs running do not count
    run (*e, in);
    const double secs = (double)(std::clock () - t0) / CLOCKS_PER_SEC;
    std::printf ("    CPU: %.2f%% of one core (stereo, Cinema 100 %%)\n", 100.0 * secs / 10.0);
    CHECK (secs / 10.0 < 0.12, "too slow"); // (about 7 % here: the classic chain's 3.5 %, the lanes' STFT 2 %, the hall and the rest 1.5 %)
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
