// Headless tests for the Multidyn DSP. Run: ./multidyn_tests [filter]
#include "Engine.h"
#include "smacheratr/src/core/Engine.h"
#include "Params.h"

#include <algorithm>
#include <chrono>
#include <complex>
#include <cmath>
#include <ctime>
#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <vector>

using namespace multidyn;

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

static double db (double v) { return 20.0 * std::log10 (std::max (1e-12, v)); }

struct Sig
{
    std::vector<float> l, r;
};

static Sig sine (double freq, double peakDb, double secs, double phase = 0.0)
{
    Sig s;
    const double a = std::pow (10.0, peakDb / 20.0);
    const size_t n = (size_t)(secs * kSr);
    s.l.resize (n);
    s.r.resize (n);
    for (size_t i = 0; i < n; ++i)
        s.l[i] = s.r[i] = (float)(a * std::sin (2.0 * M_PI * freq * i / kSr + phase));
    return s;
}

static Sig run (Engine& e, const Sig& in, const Sig* sc = nullptr, int block = 256)
{
    Sig out;
    out.l.resize (in.l.size ());
    out.r.resize (in.r.size ());
    for (size_t pos = 0; pos < in.l.size (); pos += (size_t)block)
    {
        const int n = (int)std::min<size_t> ((size_t)block, in.l.size () - pos);
        e.process (in.l.data () + pos, in.r.data () + pos, sc ? sc->l.data () + pos : nullptr,
                   sc ? sc->r.data () + pos : nullptr, out.l.data () + pos, out.r.data () + pos, n);
    }
    return out;
}

static double peakDb (const std::vector<float>& x, size_t a, size_t b)
{
    double p = 0;
    for (size_t i = a; i < std::min (b, x.size ()); ++i)
        p = std::max (p, (double)std::fabs (x[i]));
    return db (p);
}

static double rmsDb (const std::vector<float>& x, size_t a, size_t b)
{
    double s = 0;
    b = std::min (b, x.size ());
    for (size_t i = a; i < b; ++i)
        s += (double)x[i] * x[i];
    return db (std::sqrt (s / std::max<size_t> (1, b - a)));
}

enum { kLow = 0, kMid = 1, kHigh = 2 }; // with three bands
constexpr int kOnly = 0;                  // the band in single-band mode

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

// The defaults are a four-band upward-compression preset; most tests start from a neutral device.
static void neutralize (Engine& e)
{
    e.setParam (kStyle, kStyleCharacter); // (these tests are Character's; OTT style has its own below)
    e.setParam (kOutput, -kBakedMasterDb); // cancel the baked gains
    e.setParam (kMode, kBase);
    e.setParam (kPreLimit, 0.0);
    e.setParam (kSatOn, 0.0);
    for (int b = 0; b < kMaxBands; ++b)
    {
        e.setParam (bandParam (b, kBandInput), -kBakedInputDb);
        e.setParam (bandParam (b, kBandOutput), -kBakedOutputDb[b]);
        e.setParam (bandParam (b, kAboveRatio), 1.0);
        e.setParam (bandParam (b, kBelowRatio), 1.0);
        e.setParam (bandParam (b, kAboveThresh), -12.0);
        e.setParam (bandParam (b, kBelowThresh), -40.0);
        e.setParam (bandParam (b, kAttack), 10.0);
        e.setParam (bandParam (b, kRelease), 150.0);
    }
    e.setParam (kXover1, 120.0);
    e.setParam (kXover2, 1200.0);
    e.setParam (kXover3, 6000.0);
}

static std::unique_ptr<Engine> engine (bool singleBand = true)
{
    auto e = std::make_unique<Engine> ();
    e->prepare (kSr, 512);
    neutralize (*e);
    e->setParam (kBands, singleBand ? 0 : 2); // choice index: 1 or 3 bands
    e->setParam (kSoftKnee, 0);
    e->setParam (kDetector, kPeak);
    e->reset ();
    return e;
}

// ---------------------------------------------------------------------------
TEST (params_roundtrip)
{
    const auto& t = paramTable ();
    CHECK (t.size () == kNumParams, "table size %u", t.size ());
    CHECK (kXoverSlope == kSatExt2Base + pk::kTailExt2Fields &&
               std::string (t.info (kSatExt2Base + pk::kTailExt2Advanced).name) == "Saturator Gently Advanced" &&
               t.info (kSatExt2Base + pk::kTailExt2Threshold).def == -18.0 && t.info (kSatExt2Base + pk::kTailExt2Advanced).def == 0.0,
           "Gently's Advanced block (the end saturator's), then the Slope");
    CHECK (kSoftenColor == kXoverSlope + 1 && t.info (kXoverSlope).choices.size () == kNumXoverSlopes &&
               t.info (kXoverSlope).def == (double)kXover24 && t.toText (kXoverSlope, kXoverBrickwall) == "Brickwall" &&
               t.toText (kXoverSlope, kXover6) == "6 dB" && t.toText (kXoverSlope, kXover18) == "18 dB" &&
               t.toText (kXoverSlope, kXover24) == "24 dB" && t.toText (kXoverSlope, kXover96) == "96 dB",
           "Slope: 6 dB .. Brickwall, 24 dB by default; then Soften Color");
    CHECK (kSubOn == kSoftenColor + 1 && kStyle == kSubOutput + 1 && kSubInput == kStyle + 1 && kNumParams == kSubInput + 1 && t.info (kSubInput).def == 0.0 && t.info (kStyle).def == kStyleOtt && t.info (kSubOn).def == 0.0 && t.info (kSubFreq).def == 40.0 &&
               t.info (kSubFreq).min == 20.0 && t.info (kSubFreq).max == 100.0 && t.info (kSubRatio).curve == pk::Curve::Ratio,
           "the Sub band last: off, 40 Hz in 20 .. 100 Hz");
    for (uint32_t id = 0; id < kNumParams; ++id)
    {
        const auto& p = t.info (id);
        CHECK (p.id == id, "order mismatch at %u (%s)", id, p.name);
        for (double n : {0.0, 0.3, 0.5, 0.7, 1.0})
        {
            const double v = t.toPlain (id, n);
            CHECK (std::fabs (t.toPlain (id, t.toNormalized (id, v)) - v) < 1e-6 * std::max (1.0, std::fabs (v)),
                   "%s roundtrip", p.name);
        }
        double parsed;
        CHECK (t.fromText (id, t.toText (id, p.def), parsed), "%s parse '%s'", p.name, t.toText (id, p.def).c_str ());
    }
    // ratio mapping: 1:1 sits in the middle, extremes 0.5 and inf
    CHECK (std::fabs (t.toNormalized (bandParam (0, kAboveRatio), 1.0) - 0.5) < 1e-9, "1:1 centred");
    CHECK (t.toText (bandParam (0, kAboveRatio), 4.17) == "1 : 4.17", "%s", t.toText (bandParam (0, kAboveRatio), 4.17).c_str ());
    CHECK (t.toText (bandParam (0, kAboveRatio), kRatioInf) == "1 : inf", "inf");
    double parsed = 0;
    CHECK (t.fromText (bandParam (0, kBelowRatio), "1 : 66.7", parsed) && std::fabs (parsed - 66.7) < 1e-9, "parse 1 : 66.7");
    CHECK (t.fromText (bandParam (0, kBelowRatio), "1:inf", parsed) && parsed == kRatioInf, "parse 1:inf");
}

TEST (crossover_sums_flat)
{
    // With no processing the bands must sum to an allpass (flat magnitude) for every band count.
    for (int bands = 1; bands <= 4; ++bands)
    {
        auto e = engine (false);
        e->setParam (kBands, bands - 1);
        for (double f : {40.0, 120.0, 400.0, 1200.0, 2500.0, 6000.0, 15000.0})
        {
            e->reset ();
            auto in = sine (f, -12.0, 0.5);
            auto out = run (*e, in);
            const double gain = rmsDb (out.l, 12000, 24000) - rmsDb (in.l, 12000, 24000);
            CHECK (std::fabs (gain) < 0.05, "%d bands, %.0f Hz: %.3f dB", bands, f, gain);
        }
        CHECK (e->bandCount () == bands, "band count %d", e->bandCount ());
    }
}

TEST (four_bands_isolate)
{
    auto e = engine (false);
    e->setParam (kBands, 3); // 4 bands: 120 / 1200 / 6000 Hz
    auto soloGain = [&] (int band, double freq) {
        for (int b = 0; b < kMaxBands; ++b)
            e->setParam (bandParam (b, kBandSolo), b == band ? 1 : 0);
        e->reset ();
        auto in = sine (freq, -12.0, 0.4);
        auto out = run (*e, in);
        return rmsDb (out.l, 9600, 19200) - rmsDb (in.l, 9600, 19200);
    };
    const double freqs[] = {50.0, 400.0, 2800.0, 14000.0};
    for (int b = 0; b < 4; ++b)
        for (int k = 0; k < 4; ++k)
        {
            const double g = soloGain (b, freqs[k]);
            if (b == k)
                CHECK (g > -1.0, "band %d passes %.0f Hz: %f", b, freqs[k], g); // LR4 skirts: ~0.6 dB in a 2-octave band
            else
                CHECK (g < -24.0, "band %d rejects %.0f Hz: %f", b, freqs[k], g);
        }
}

TEST (bands_isolate)
{
    auto e = engine (false);
    auto soloOnly = [&] (int band, double freq) {
        for (int b = 0; b < kNumBands; ++b)
            e->setParam (bandParam (b, kBandSolo), b == band ? 1 : 0);
        e->reset ();
        auto in = sine (freq, -12.0, 0.4);
        auto out = run (*e, in);
        return rmsDb (out.l, 9600, 19200) - rmsDb (in.l, 9600, 19200);
    };
    CHECK (soloOnly (kLow, 40.0) > -0.5, "40 Hz in the low band: %f", soloOnly (kLow, 40.0));
    CHECK (soloOnly (kHigh, 40.0) < -60.0, "40 Hz out of the high band: %f", soloOnly (kHigh, 40.0));
    CHECK (soloOnly (kMid, 400.0) > -0.5, "400 Hz in mid: %f", soloOnly (kMid, 400.0));
    CHECK (soloOnly (kLow, 8000.0) < -60.0, "8 kHz out of low: %f", soloOnly (kLow, 8000.0));
    CHECK (soloOnly (kHigh, 8000.0) > -0.5, "8 kHz in high: %f", soloOnly (kHigh, 8000.0));
    // crossover points are -6 dB per LR4 band
    const double atX = soloOnly (kLow, 120.0);
    CHECK (std::fabs (atX + 6.0) < 0.6, "low band at its crossover: %f", atX);
}

TEST (four_kinds_of_dynamics)
{
    struct Case
    {
        const char* name;
        double inDb;
        uint32_t tId, rId;
        double thresh, ratio, expectDb;
    };
    const Case cases[] = {
        {"downward compression", -6.0, bandParam (kOnly, kAboveThresh), bandParam (kOnly, kAboveRatio), -20.0, 4.0,
         -20.0 + 14.0 / 4.0},
        {"upward expansion", -26.0, bandParam (kOnly, kAboveThresh), bandParam (kOnly, kAboveRatio), -30.0, 0.5,
         -30.0 + 4.0 * 2.0},
        {"downward expansion", -40.0, bandParam (kOnly, kBelowThresh), bandParam (kOnly, kBelowRatio), -30.0, 0.5,
         -30.0 - 10.0 * 2.0},
        {"upward compression", -40.0, bandParam (kOnly, kBelowThresh), bandParam (kOnly, kBelowRatio), -30.0, 2.0,
         -30.0 - 10.0 * 0.5},
    };
    for (const auto& c : cases)
    {
        auto e = engine ();
        e->setParam (bandParam (kOnly, kAboveThresh), 6.0); // park the other threshold out of the way
        e->setParam (bandParam (kOnly, kBelowThresh), -70.0);
        e->setParam (c.tId, c.thresh);
        e->setParam (c.rId, c.ratio);
        // Above: a long release holds the gain steady. Below: its envelope rises from silence
        // with the release time, so keep it short enough to settle within the test.
        const bool below = c.rId == bandParam (kOnly, kBelowRatio);
        e->setParam (bandParam (kOnly, kRelease), below ? 50.0 : 500.0);
        auto out = run (*e, sine (1000.0, c.inDb, 2.0));
        const double got = peakDb (out.l, (size_t)(1.5 * kSr), (size_t)(2.0 * kSr));
        CHECK (std::fabs (got - c.expectDb) < 0.6, "%s: %.2f dB (want %.2f)", c.name, got, c.expectDb);
    }
}

TEST (low_frequency_gain_ripple)
{
    // A 50 Hz tone into heavy compression: the gain must not wobble within each cycle
    // (that would be heard as distortion).
    for (int mode : {kPeak, kRms})
    {
        auto e = engine ();
        e->setParam (kDetector, mode);
        e->setParam (bandParam (kOnly, kAboveThresh), -20.0);
        e->setParam (bandParam (kOnly, kAboveRatio), 8.0);
        e->setParam (bandParam (kOnly, kAttack), 5.0);
        e->setParam (bandParam (kOnly, kRelease), 100.0);
        auto in = sine (50.0, -6.0, 2.0);
        Engine& eng = *e;
        float lo = 0, hi = -100;
        for (size_t pos = 0; pos < in.l.size (); pos += 48)
        {
            Sig c;
            c.l.assign (in.l.begin () + pos, in.l.begin () + pos + 48);
            c.r = c.l;
            run (eng, c, nullptr, 48);
            if (pos > 48000)
            {
                lo = std::min (lo, eng.meter (kOnly).gainDb);
                hi = std::max (hi, eng.meter (kOnly).gainDb);
            }
        }
        CHECK (hi - lo < 1.5f, "mode %d: gain ripple %.2f dB at 50 Hz", mode, hi - lo);
    }
}

TEST (amount_scales_and_zero_is_bypass)
{
    for (double amount : {0.0, 0.5, 1.0})
    {
        auto e = engine ();
        e->setParam (kAmount, amount);
        e->setParam (bandParam (kOnly, kAboveThresh), -20.0);
        e->setParam (bandParam (kOnly, kAboveRatio), 4.0);
        e->setParam (bandParam (kOnly, kRelease), 500.0);
        auto out = run (*e, sine (1000.0, -6.0, 1.5));
        const double got = peakDb (out.l, (size_t)(1.0 * kSr), (size_t)(1.5 * kSr));
        const double want = -6.0 + amount * (14.0 * (0.25 - 1.0));
        CHECK (std::fabs (got - want) < 0.6, "amount %.1f: %.2f (want %.2f)", amount, got, want);
    }
}

TEST (soft_knee_is_gradual)
{
    CHECK (aboveGainDb (-20.0, -20.0, 4.0, false) == 0.0, "hard knee at threshold");
    const double soft = aboveGainDb (-20.0, -20.0, 4.0, true);
    CHECK (soft < -0.1 && soft > -1.0, "soft knee acts at the threshold: %f", soft);
    CHECK (std::fabs (aboveGainDb (-10.0, -20.0, 4.0, true) - aboveGainDb (-10.0, -20.0, 4.0, false)) < 1e-9,
           "soft knee converges above the knee");
    // continuity across the knee
    double prev = aboveGainDb (-30.0, -20.0, 4.0, true), worst = 0;
    for (double x = -29.9; x < -10.0; x += 0.1)
    {
        const double g = aboveGainDb (x, -20.0, 4.0, true);
        worst = std::max (worst, std::fabs (g - prev));
        prev = g;
    }
    CHECK (worst < 0.1, "soft knee jump %f", worst);
    CHECK (belowGainDb (-50.0, -40.0, 0.5, false) == -10.0 && belowGainDb (-30.0, -40.0, 0.5, false) == 0.0,
           "below: 1:0.5 doubles the distance under the threshold");
    CHECK (std::fabs (belowGainDb (-50.0, -40.0, 2.0, false) - 5.0) < 1e-9, "below: 1:2 halves it (upward compression)");
}

TEST (attack_and_release_timing)
{
    // Level steps from -40 to -6 dB into ratio 10 above -20: the full cut is 14*(0.1-1) = -12.6 dB.
    auto e = engine ();
    e->setParam (bandParam (kOnly, kAboveThresh), -20.0);
    e->setParam (bandParam (kOnly, kAboveRatio), 10.0);
    e->setParam (bandParam (kOnly, kAttack), 50.0);
    e->setParam (bandParam (kOnly, kRelease), 200.0);
    auto quiet = sine (1000.0, -40.0, 0.2);
    auto loud = sine (1000.0, -6.0, 1.0);
    Sig in = quiet;
    in.l.insert (in.l.end (), loud.l.begin (), loud.l.end ());
    in.r.insert (in.r.end (), loud.r.begin (), loud.r.end ());
    Engine& eng = *e;
    run (eng, quiet);
    std::vector<float> g;
    for (int blk = 0; blk < 40; ++blk) // 40 x 10 ms of loud signal, sample the gain after each block
    {
        Sig chunk;
        chunk.l.assign (loud.l.begin () + blk * 480, loud.l.begin () + (blk + 1) * 480);
        chunk.r = chunk.l;
        run (eng, chunk, nullptr, 480);
        g.push_back (eng.meter (kOnly).gainDb);
    }
    // Attack 50 ms = time to cover ~95 % of a level change (time constant 16.7 ms), through two
    // envelope stages (a rounded onset): the level only reaches the -20 dB threshold after a while,
    // so there is no reduction at 10 ms; it builds up, is mostly there by 100 ms and settles at
    // -12.6 dB - the gain follows the transfer curve rather than the attack time literally.
    std::printf ("    gain every 10 ms: %.1f %.1f %.1f %.1f %.1f %.1f %.1f %.1f %.1f %.1f\n", g[0], g[1], g[2], g[3], g[4], g[5],
                 g[6], g[7], g[8], g[9]);
    CHECK (std::fabs (g[0]) < 0.3, "no reduction before the envelope reaches the threshold: %f", g[0]);
    for (int i = 1; i < 10; ++i)
        CHECK (g[(size_t)i] <= g[(size_t)i - 1] + 0.01, "the reduction only builds up: %f then %f", g[(size_t)i - 1], g[(size_t)i]);
    CHECK (g[4] / -12.6 > 0.2 && g[4] / -12.6 < 0.9, "part of it at 50 ms: %f", g[4] / -12.6);
    CHECK (g[9] / -12.6 > 0.8, "most of it at 100 ms: %f", g[9] / -12.6);
    CHECK (std::fabs (g.back () + 12.6) < 0.8, "settles at -12.6: %f", g.back ());
    const double at50 = g[4] / -12.6;
    // Time 200 % slows everything down
    auto e2 = engine ();
    e2->setParam (bandParam (kOnly, kAboveThresh), -20.0);
    e2->setParam (bandParam (kOnly, kAboveRatio), 10.0);
    e2->setParam (bandParam (kOnly, kAttack), 50.0);
    e2->setParam (kTime, 2.0);
    run (*e2, quiet);
    Sig chunk;
    chunk.l.assign (loud.l.begin (), loud.l.begin () + 2400);
    chunk.r = chunk.l;
    run (*e2, chunk, nullptr, 480);
    CHECK (e2->meter (kOnly).gainDb / -12.6 < at50 * 0.6, "Time 200%% slows the attack: %f at 50 ms", e2->meter (kOnly).gainDb);
}

TEST (upward_compression_does_not_explode_after_silence)
{
    // Heavy upward compression, slow release: a loud burst right after silence must not be
    // lifted above where it would sit anyway (the boost is capped at the Below threshold).
    auto e = engine ();
    e->setParam (kDetector, kRms);
    e->setParam (bandParam (kOnly, kBelowThresh), -40.0);
    e->setParam (bandParam (kOnly, kBelowRatio), kRatioInf);
    e->setParam (bandParam (kOnly, kRelease), 2000.0);
    Sig silence = sine (1000.0, -200.0, 1.0);
    auto burst = sine (1000.0, -6.0, 0.5);
    Sig in = silence;
    in.l.insert (in.l.end (), burst.l.begin (), burst.l.end ());
    in.r = in.l;
    auto out = run (*e, in);
    const double pk = peakDb (out.l, 48000, out.l.size ());
    CHECK (pk < -5.5, "burst after silence peaks at %.2f dB (input -6)", pk);
}

TEST (transient_guard_after_a_quiet_passage)
{
    // the preset (heavy upward compression): a quiet pad, then a drum-like hit. The hit's first
    // milliseconds used to go out with the pad's boost (about 5 dB over the rest of the hit); the
    // guard sees it coming through the look-ahead, so it starts where it goes on
    Engine e;
    e.prepare (kSr, 512);
    e.setParam (kStyle, kStyleCharacter); // (the guard is Character's)
    e.setParam (kSatOn, 0.0);
    const size_t n = (size_t)(kSr * 2.0), hitAt = (size_t)kSr;
    Sig in;
    in.l.resize (n);
    uint32_t seed = 1;
    for (size_t i = 0; i < n; ++i)
    {
        double x = 0.003 * std::sin (2.0 * M_PI * 220.0 * i / kSr); // -50 dB pad
        if (i >= hitAt)
        {
            const double t = (i - hitAt) / kSr;
            seed = seed * 1664525u + 1013904223u;
            const double noise = ((seed >> 8) & 0xFFFF) / 32768.0 - 1.0;
            x += std::exp (-t / 0.08) * (0.3 * std::sin (2.0 * M_PI * 180.0 * t) + 0.2 * noise);
        }
        in.l[i] = (float)x;
    }
    in.r = in.l;
    auto out = run (e, in);
    const size_t ms = (size_t)(kSr / 1000);
    const double first = peakDb (out.l, hitAt, hitAt + 5 * ms), after = peakDb (out.l, hitAt + 20 * ms, hitAt + 40 * ms);
    std::printf ("    hit: first 5 ms %.1f dB, 20-40 ms %.1f dB\n", first, after);
    CHECK (first < after + 1.0, "no spike at the start of the hit: %.1f vs %.1f dB", first, after);
    // and the quiet pad before it is still lifted as much as ever
    const double pad = rmsDb (out.l, hitAt - 200 * ms, hitAt - 10 * ms);
    CHECK (pad > -35.0, "the pad is lifted: %.1f dB rms", pad);
}

TEST (preset_defaults)
{
    // A fresh engine is Live's OTT preset: 3 bands, the thresholds, ratios and times of OTT, its band
    // Outputs baked in so every gain control reads 0 dB.
    Engine e;
    e.prepare (kSr, 512);
    CHECK (std::lround (e.param (kBands)) == 2 && std::fabs (e.param (kXover1) - 88.3) < 1e-9 &&
               std::fabs (e.param (kXover2) - 2500.0) < 1e-9 && std::fabs (e.param (kXover3) - 8000.0) < 1e-9,
           "3 bands at 88.3 Hz / 2.5 kHz (8 kHz for a fourth)");
    CHECK (std::lround (e.param (kMode)) == kCharacter && e.param (kPreLimit) < 0.5 && e.param (kPreLimitCeiling) == 0.0,
           "Character mode, pre-limit off");
    CHECK (e.param (kSatOn) < 0.5 && e.param (kSatDrive) == 0.0, "end-of-chain saturator off, Drive 0 dB");
    for (int b = 0; b < kMaxBands; ++b)
        CHECK (std::fabs (e.param (bandParam (b, kAboveRatio)) - 66.7) < 1e-9 && std::fabs (e.param (bandParam (b, kBelowRatio)) - 4.17) < 1e-9,
               "band %d ratios: Above 1:66.7, Below 1:4.17", b + 1);
    CHECK (e.param (bandParam (0, kBandOutput)) == 0.0 && e.param (bandParam (1, kBandInput)) == 0.0 && e.param (kOutput) == 0.0 &&
               e.param (kAmount) == 1.0 && e.param (kTime) == 1.0,
           "gain controls at 0 dB, Amount and Time 100 %%");
    CHECK (kBakedInputDb == 0.0 && kBakedMasterDb == 0.0 && kBakedOutputDb[0] == 10.3 && kBakedOutputDb[1] == 5.7 &&
               kBakedOutputDb[2] == 10.3 && kBakedOutputDb[3] == 10.3,
           "baked: band Outputs +10.3 / +5.7 / +10.3 dB (+10.3 for a fourth), no Input or Output gain");
    struct Want
    {
        double above, below, attack, release;
    };
    const Want want[kMaxBands] = {{-33.8, -40.8, 47.8, 282.0}, {-30.2, -41.8, 22.4, 282.0}, {-35.5, -40.8, 13.5, 132.0}, {-35.5, -40.8, 13.5, 132.0}};
    for (int b = 0; b < kMaxBands; ++b)
        CHECK (std::fabs (e.param (bandParam (b, kAboveThresh)) - want[b].above) < 1e-9 &&
                   std::fabs (e.param (bandParam (b, kBelowThresh)) - want[b].below) < 1e-9 &&
                   std::fabs (e.param (bandParam (b, kAttack)) - want[b].attack) < 1e-9 &&
                   std::fabs (e.param (bandParam (b, kRelease)) - want[b].release) < 1e-9,
               "band %d: Above %.1f, Below %.1f dB, %.1f / %.0f ms", b + 1, want[b].above, want[b].below, want[b].attack, want[b].release);
    CHECK (std::lround (e.param (kXoverSlope)) == kXover24 && e.param (kSoftenColor) < 0.5 && e.param (kSubOn) < 0.5,
           "24 dB crossovers, Soften Color and the Sub band off");
    // It squashes dynamics hard: a 44 dB level difference at 1 kHz comes out within ~15 dB.
    auto level = [] (double inDb) {
        Engine x;
        x.prepare (kSr, 512);
        auto out = run (x, sine (1000.0, inDb, 3.0));
        return rmsDb (out.l, 96000, 144000);
    };
    const double quiet = level (-50.0), loud = level (-6.0);
    std::printf ("    preset: -50 dB in -> %.1f dB rms, -6 dB in -> %.1f dB rms\n", quiet, loud);
    CHECK (loud - quiet < 16.0, "preset output range %.1f dB for 44 dB in", loud - quiet);
    CHECK (quiet > -45.0, "quiet material is lifted: %.1f", quiet);
}

TEST (pre_limiter_rounds_transients)
{
    auto e = engine ();
    const int lat = e->latency ();
    smacheratr::Engine satAlone;
    satAlone.prepare (kSr, 512);
    CHECK (lat == 48 + 2 * satAlone.latency (), "1 ms look-ahead + Soften Color's and the saturator's oversampling at 48 kHz: %d", lat);
    // under the ceiling (the Above threshold, -12 dB here, + 0) nothing but the delay happens
    e->setParam (kPreLimit, 1.0);
    e->setParam (kPreLimitCeiling, 0.0);
    e->reset ();
    auto in = sine (1000.0, -20.0, 0.5);
    auto out = run (*e, in);
    double err = 0;
    for (size_t i = (size_t)lat + 1000; i < out.l.size (); ++i)
        err = std::max (err, (double)std::fabs (out.l[i] - in.l[i - (size_t)lat]));
    CHECK (err < 1e-4, "delayed only: %g", err);
    // a 0 dB burst is held near the ceiling from its first cycle on, not squared
    auto loud = sine (1000.0, 0.0, 0.5);
    e->reset ();
    out = run (*e, loud);
    const double body = peakDb (out.l, 4800, 24000), first = peakDb (out.l, 0, (size_t)lat + 96);
    CHECK (body < -10.5 && body > -13.5, "held at the ceiling: %f", body);
    CHECK (first < -9.0, "the transient is rounded: %f", first);
    e->setParam (kPreLimit, 0.0);
    e->reset ();
    out = run (*e, loud);
    CHECK (std::fabs (peakDb (out.l, 4800, 24000)) < 0.1, "off: untouched %f", peakDb (out.l, 4800, 24000));
}

TEST (compression_eases_in)
{
    // a -6 dB tone after 0.5 s of -40 dB: the compression eases in (50 ms RMS window, rounded onset);
    // the unused Mode parameter changes nothing
    auto levelAfter = [] (int mode, double secs) {
        auto e = engine ();
        e->setParam (kMode, mode);
        e->setParam (kDetector, kRms);
        e->setParam (bandParam (kOnly, kAboveThresh), -30.0);
        e->setParam (bandParam (kOnly, kAboveRatio), 4.0);
        e->setParam (bandParam (kOnly, kAttack), 10.0);
        e->setParam (bandParam (kOnly, kRelease), 100.0);
        e->reset ();
        Sig in = sine (1000.0, -40.0, 0.5);
        Sig loud = sine (1000.0, -6.0, 0.5);
        in.l.insert (in.l.end (), loud.l.begin (), loud.l.end ());
        in.r = in.l;
        auto out = run (*e, in);
        const size_t start = (size_t)((0.5 + secs) * kSr) + 48;
        return peakDb (out.l, start, start + 480);
    };
    const double early = levelAfter (kCharacter, 0.01), late = levelAfter (kCharacter, 0.4);
    CHECK (early > late + 6.0, "eases in: %f early, %f later", early, late);
    CHECK (late < -6.0 - 15.0, "compressing: %f", late);
    CHECK (std::fabs (levelAfter (kBase, 0.01) - early) < 1e-6 && std::fabs (levelAfter (kBase, 0.4) - late) < 1e-6,
           "Mode is unused");
}

TEST (built_in_saturator)
{
    // after the Output gain: off = clean, on = harmonics, and Dry/Wet blends
    auto e = engine ();
    auto in = sine (1000.0, -6.0, 0.5);
    auto out = run (*e, in);
    const size_t a = 12000, b = 24000;
    auto h3 = [&] (const Sig& s) { return toneDb (s.l, 3000.0, a, b) - toneDb (s.l, 1000.0, a, b); };
    CHECK (h3 (out) < -80.0, "off: clean %f", h3 (out));
    e->setParam (kSatOn, 1.0);
    e->setParam (kSatDrive, 14.0);
    e->reset ();
    out = run (*e, in);
    CHECK (h3 (out) > -30.0, "on: third harmonic %f dB", h3 (out));
    CHECK (peakDb (out.l, a, b) <= 0.01, "analog clip holds 0 dB: %f", peakDb (out.l, a, b));
    e->setParam (kSatMix, 0.0);
    e->reset ();
    out = run (*e, in);
    CHECK (h3 (out) < -80.0, "dry/wet 0: clean %f", h3 (out));
}

TEST (rms_ignores_short_peaks_more_than_peak)
{
    auto measure = [] (int mode) {
        auto e = engine ();
        e->setParam (kDetector, mode);
        e->setParam (bandParam (kOnly, kAboveThresh), -20.0);
        e->setParam (bandParam (kOnly, kAboveRatio), 10.0);
        e->setParam (bandParam (kOnly, kAttack), 0.1);
        // quiet tone with a 1 ms click every 100 ms
        Sig in = sine (1000.0, -30.0, 1.0);
        for (size_t s = 0; s < in.l.size (); s += 4800)
            for (size_t i = 0; i < 48 && s + i < in.l.size (); ++i)
                in.l[s + i] = in.r[s + i] = (i % 2 ? 0.9f : -0.9f);
        float worst = 0;
        Engine& eng = *e;
        for (size_t pos = 0; pos < in.l.size (); pos += 96)
        {
            Sig c;
            c.l.assign (in.l.begin () + pos, in.l.begin () + pos + 96);
            c.r = c.l;
            run (eng, c, nullptr, 96);
            worst = std::min (worst, eng.meter (kOnly).gainDb);
        }
        return worst;
    };
    const float peak = measure (kPeak), rms = measure (kRms);
    CHECK (peak < rms - 3.0f, "peak mode should react more to clicks: peak %f rms %f", peak, rms);
}

TEST (rms_window_sets_the_detector_speed)
{
    // a level step into downward compression: a short RMS window catches it sooner than a long one
    auto gainAfter = [] (double windowMs, double ms) {
        auto e = engine ();
        e->setParam (kDetector, kRms);
        e->setParam (kRmsWindow, windowMs);
        e->setParam (bandParam (kOnly, kAboveRatio), 4.0);
        e->setParam (bandParam (kOnly, kAboveThresh), -30.0);
        e->setParam (bandParam (kOnly, kAttack), 1.0);
        Sig in = sine (1000.0, -50.0, 0.5);
        Sig loud = sine (1000.0, -6.0, 0.5);
        in.l.insert (in.l.end (), loud.l.begin (), loud.l.end ());
        in.r.insert (in.r.end (), loud.r.begin (), loud.r.end ());
        auto out = run (*e, in);
        const size_t at = (size_t)((0.5 + ms * 0.001) * kSr);
        return rmsDb (out.l, at, at + 96) - rmsDb (in.l, at, at + 96);
    };
    const double fast = gainAfter (5.0, 15.0), slow = gainAfter (200.0, 15.0);
    std::printf ("    15 ms after the step: %.1f dB (5 ms window), %.1f dB (200 ms window)\n", fast, slow);
    CHECK (fast < slow - 3.0, "a short window compresses sooner: %.1f vs %.1f dB", fast, slow);
    const double fastLate = gainAfter (5.0, 400.0), slowLate = gainAfter (200.0, 400.0);
    CHECK (std::fabs (fastLate - slowLate) < 1.5, "both settle to the same gain: %.1f vs %.1f dB", fastLate, slowLate);
    CHECK (paramTable ().info (kRmsWindow).def == 50.0, "50 ms by default");
}

static void setOldProject (Engine& e, int variant);

TEST (soften_tames_the_lifted_top_band)
{
    // a top band that lifts everything quiet up to its Below threshold (1:inf, the band driven 5.2 dB),
    // 5.3 dB under Above: Soften low-passes the lifted part, so quiet hiss at 14 kHz comes out much lower
    // while 3.5 kHz barely moves
    auto measure = [] (double soften, double f) {
        Engine e;
        e.prepare (kSr, 512);
        setOldProject (e, 0); // the preset before OTT's (Below 1:inf), the bands driven 5.2 dB as it did
        for (int b = 0; b < kMaxBands; ++b)
            e.setParam (bandParam (b, kBandInput), kOldBakedInputDb);
        e.setParam (kSoften, soften);
        Sig in = sine (3500.0, -60.0, 2.0);
        Sig hi = sine (14000.0, -60.0, 2.0);
        for (size_t i = 0; i < in.l.size (); ++i)
        {
            in.l[i] += hi.l[i];
            in.r[i] += hi.r[i];
        }
        auto out = run (e, in);
        return toneDb (out.l, f, 48000, 96000);
    };
    const double hiOff = measure (0.0, 14000.0), hiOn = measure (1.0, 14000.0);
    const double loOff = measure (0.0, 3500.0), loOn = measure (1.0, 3500.0);
    std::printf ("    14 kHz: %.1f -> %.1f dB, 3.5 kHz: %.1f -> %.1f dB\n", hiOff, hiOn, loOff, loOn);
    CHECK (hiOn < hiOff - 6.0, "the lifted air is softened: %.1f vs %.1f dB", hiOn, hiOff);
    CHECK (hiOn < hiOff - 12.0, "strongly at 100 %%: %.1f vs %.1f dB", hiOn, hiOff);
    CHECK (loOn > loOff - 4.5, "the top band's body mostly stays: %.1f vs %.1f dB", loOn, loOff);
    // far-apart thresholds: Soften does nothing
    auto apart = [] (double soften) {
        Engine e;
        e.prepare (kSr, 512);
        e.setParam (kSoften, soften);
        e.setParam (bandParam (2, kBelowThresh), -60.0);
        e.setParam (bandParam (2, kAboveThresh), -20.0);
        auto out = run (e, sine (14000.0, -50.0, 1.0));
        return toneDb (out.l, 14000.0, 24000, 48000);
    };
    CHECK (std::fabs (apart (1.0) - apart (0.0)) < 0.05, "no effect 40 dB apart: %.2f vs %.2f dB", apart (1.0), apart (0.0));
    CHECK (std::fabs (paramTable ().info (kSoften).def - 0.5) < 1e-9, "50 %% by default");
}

TEST (band_active_solo_and_gains)
{
    auto e = engine (false);
    // mid band: +6 dB input, -3 dB output, and heavy compression that would otherwise act
    e->setParam (bandParam (kMid, kBandInput), -kBakedInputDb + 6.0);
    e->setParam (bandParam (kMid, kBandOutput), -kBakedOutputDb[kMid] - 3.0);
    e->reset ();
    auto in = sine (350.0, -20.0, 0.5);
    auto out = run (*e, in);
    CHECK (std::fabs (rmsDb (out.l, 12000, 24000) - rmsDb (in.l, 12000, 24000) - 3.0) < 0.3, "band gains +3 dB: %f",
           rmsDb (out.l, 12000, 24000) - rmsDb (in.l, 12000, 24000));
    e->setParam (bandParam (kMid, kBandActive), 0);
    e->reset ();
    out = run (*e, in);
    CHECK (std::fabs (rmsDb (out.l, 12000, 24000) - rmsDb (in.l, 12000, 24000)) < 0.1, "inactive band bypasses gains");
    // global output
    e->setParam (kOutput, -kBakedMasterDb - 6.0);
    e->reset ();
    out = run (*e, in);
    CHECK (std::fabs (rmsDb (out.l, 12000, 24000) - rmsDb (in.l, 12000, 24000) + 6.0) < 0.1, "output -6 dB");
    // solo mutes the others
    e->setParam (kOutput, -kBakedMasterDb);
    e->setParam (bandParam (kHigh, kBandSolo), 1);
    e->reset ();
    out = run (*e, in);
    CHECK (rmsDb (out.l, 12000, 24000) < -60.0, "350 Hz with only the high band soloed: %f", rmsDb (out.l, 12000, 24000));
}

TEST (sidechain_triggers_and_listen)
{
    auto e = engine ();
    e->setParam (bandParam (kOnly, kAboveThresh), -20.0);
    e->setParam (bandParam (kOnly, kAboveRatio), 10.0);
    e->setParam (bandParam (kOnly, kRelease), 500.0);
    auto mainSig = sine (1000.0, -30.0, 1.5);
    auto key = sine (1000.0, -6.0, 1.5, 1.0);
    auto out = run (*e, mainSig, &key);
    CHECK (std::fabs (peakDb (out.l, 48000, 72000) + 30.0) < 0.3, "side-chain off: untouched (%f)", peakDb (out.l, 48000, 72000));
    e->setParam (kScOn, 1);
    e->reset ();
    out = run (*e, mainSig, &key);
    CHECK (std::fabs (peakDb (out.l, 48000, 72000) - (-30.0 - 12.6)) < 0.8, "keyed by the side-chain: %f",
           peakDb (out.l, 48000, 72000));
    e->setParam (kScMix, 0.0);
    e->reset ();
    out = run (*e, mainSig, &key);
    CHECK (std::fabs (peakDb (out.l, 48000, 72000) + 30.0) < 0.3, "dry/wet 0 ignores the side-chain");
    e->setParam (kScListen, 1);
    e->setParam (kScGain, -6.0);
    e->reset ();
    out = run (*e, mainSig, &key);
    CHECK (std::fabs (peakDb (out.l, 48000, 72000) + 12.0) < 0.3, "listen outputs the side-chain: %f", peakDb (out.l, 48000, 72000));
    // no side-chain connected: listen is silent, processing unaffected
    e->setParam (kScListen, 0);
    e->reset ();
    out = run (*e, mainSig, nullptr);
    CHECK (std::fabs (peakDb (out.l, 48000, 72000) + 30.0) < 0.3, "no side-chain buffer");
}

TEST (fuzz_random_params)
{
    uint32_t seed = 99;
    auto rnd = [&] {
        seed = seed * 1664525u + 1013904223u;
        return (double)((seed >> 8) & 0xFFFFFF) / 16777216.0;
    };
    double worst = 0;
    for (int iter = 0; iter < 300; ++iter)
    {
        Engine e;
        e.prepare (kSr, 512);
        for (uint32_t id = 0; id < kNumParams; ++id)
            if (rnd () < 0.7)
                e.setParam (id, paramTable ().toPlain (id, rnd ()));
        e.setParam (kOutput, 0.0);
        e.setParam (kScListen, 0);
        e.reset ();
        Sig in;
        in.l.resize (24000);
        in.r.resize (24000);
        for (size_t i = 0; i < in.l.size (); ++i)
        {
            const double env = (i / 4000) % 2 ? 1.0 : 0.001; // loud/quiet bursts
            in.l[i] = (float)(env * (rnd () * 2 - 1));
            in.r[i] = (float)(env * (rnd () * 2 - 1));
        }
        auto sc = in;
        auto out = run (e, in, rnd () < 0.5 ? &sc : nullptr, 1 + (int)(rnd () * 600));
        // and on, with a new Slope, Soften Color and the Sub band (switched while it runs)
        e.setParam (kXoverSlope, std::floor (rnd () * (double)kNumXoverSlopes));
        e.setParam (kSoftenColor, rnd () < 0.5 ? 1.0 : 0.0);
        e.setParam (kSubOn, e.param (kSubOn) < 0.5 ? 1.0 : 0.0);
        e.setParam (kSubFreq, paramTable ().toPlain (kSubFreq, rnd ()));
        const auto more = run (e, in, nullptr, 1 + (int)(rnd () * 600));
        out.l.insert (out.l.end (), more.l.begin (), more.l.end ());
        bool finite = true;
        double pk = 0;
        for (float v : out.l)
        {
            finite &= std::isfinite (v);
            pk = std::max (pk, (double)std::fabs (v));
        }
        CHECK (finite, "iteration %d non-finite", iter);
        worst = std::max (worst, pk);
    }
    std::printf ("    worst peak %.1f\n", worst);
    // bound: +24 dB band input, +36 dB maximum upward gain, +24 dB band output (and its baked gain) on a
    // full-scale input
    CHECK (worst < std::pow (10.0, (84.0 + kBakedOutputDb[0] + kBakedMasterDb) / 20.0) * 1.05, "runaway gain %f", worst);
}

// ---- an old project (before the OTT defaults): its settings and a signal ------------------------------
static Sig oldProjectSignal ()
{
    Sig s;
    const size_t n = (size_t)(1.5 * kSr);
    s.l.resize (n);
    s.r.resize (n);
    uint32_t state = 12345u;
    auto rnd = [&] {
        state = state * 1664525u + 1013904223u;
        return (double)(state >> 8) / (double)(1u << 24);
    };
    for (size_t i = 0; i < n; ++i)
    {
        const double env = (i / 6000) % 3 == 0 ? 0.5 : ((i / 6000) % 3 == 1 ? 0.02 : 0.002);
        const double sw = std::sin (2.0 * M_PI * (60.0 + 4000.0 * i / n) * i / kSr);
        s.l[i] = (float)(env * (0.6 * (rnd () * 2 - 1) + 0.4 * sw));
        s.r[i] = (float)(env * (0.5 * (rnd () * 2 - 1) + 0.5 * sw));
    }
    return s;
}

// The old defaults (every value set here, whatever the table's defaults are now) and, for variant 1,
// four bands with trims on every gain control. The gains are the old plain values.
struct OldGains
{
    double input[4], output[4], master;
};
static OldGains oldGains (int variant)
{
    if (variant == 0)
        return {{0, 0, 0, 0}, {0, 0, 0, 0}, 0.0};
    return {{-3.0, 2.0, 0.0, 4.0}, {-10.0, 1.5, -2.0, 0.5}, 3.0};
}
static void setOldProject (Engine& e, int variant)
{
    e.setParam (kStyle, kStyleCharacter); // an old project opens in Character
    struct D
    {
        double below, belowRatio, above, aboveRatio, attack, release;
    };
    const D defs[4] = {{-40.8, kRatioInf, -35.5, 66.7, 47.8, 282.0},
                       {-40.8, kRatioInf, -35.5, 66.7, 22.4, 282.0},
                       {-40.8, kRatioInf, -35.5, kRatioInf, 13.5, 132.0},
                       {-40.8, 4.17, -35.5, kRatioInf, 13.5, 132.0}};
    e.setParam (kAmount, 1.0);
    e.setParam (kTime, 1.0);
    e.setParam (kSoftKnee, 1.0);
    e.setParam (kDetector, kRms);
    e.setParam (kBands, variant == 0 ? 2 : 3);
    e.setParam (kXover1, 88.3);
    e.setParam (kXover2, 2500.0);
    e.setParam (kXover3, variant == 0 ? 8000.0 : 6000.0);
    e.setParam (kPreLimit, 0.0);
    e.setParam (kSatOn, 0.0);
    e.setParam (kRmsWindow, 50.0);
    e.setParam (kSoften, 0.5);
    for (int b = 0; b < 4; ++b)
    {
        e.setParam (bandParam (b, kBandActive), 1.0);
        e.setParam (bandParam (b, kBandSolo), 0.0);
        e.setParam (bandParam (b, kBelowThresh), defs[b].below);
        e.setParam (bandParam (b, kBelowRatio), defs[b].belowRatio);
        e.setParam (bandParam (b, kAboveThresh), defs[b].above);
        e.setParam (bandParam (b, kAboveRatio), defs[b].aboveRatio);
        e.setParam (bandParam (b, kAttack), defs[b].attack);
        e.setParam (bandParam (b, kRelease), defs[b].release);
    }
}

TEST (old_project_sounds_the_same)
{
    // The output of Multidyn before the OTT defaults (the old baked gains, LR4 crossovers, no Soften
    // Color) for the old projects above, recorded from that build: sample index, left, right.
    struct Golden
    {
        size_t i;
        float l, r;
    };
    const Golden golden[2][18] = {
        {{1000, 0.381332904f, 1.0761857f},           {5099, -0.0316724814f, -0.12785092f},
         {9198, 0.00127461669f, -0.000594994577f},   {13297, -0.000232170365f, -0.000194133798f},
         {17396, -0.00143299077f, -0.000861765409f}, {21495, -0.125467613f, 0.182145566f},
         {25594, 0.00140703307f, 0.00162688293f},    {29693, -0.00560349226f, -0.00422452763f},
         {33792, 8.81905871e-05f, 0.000483153795f},  {37891, 0.0366339125f, 0.0408094451f},
         {41990, 0.175184816f, 0.129271314f},        {46089, -0.00405096402f, 0.0030721915f},
         {50188, -0.000471975189f, 0.000501761911f}, {54287, 0.0943354368f, 0.066881679f},
         {58386, 0.0205902662f, -0.00277820788f},    {62485, -0.00332832173f, 0.00378302834f},
         {66584, 0.000379308563f, -0.00194204552f},  {70683, 0.0026423852f, 0.00237732846f}},
        {{1000, 0.436306924f, 2.5000701f},           {5099, -0.148172066f, -0.103828855f},
         {9198, 0.00181253057f, 0.00151033862f},     {13297, -0.000238263761f, -0.000334446959f},
         {17396, -0.00117262255f, -0.000453240616f}, {21495, 0.0649309158f, 0.183762535f},
         {25594, 0.00418279227f, 0.00625108229f},    {29693, 0.00355307502f, -0.00157938106f},
         {33792, 0.000339788152f, 0.000556624494f},  {37891, 0.122998357f, -0.0153187877f},
         {41990, 0.137278318f, 0.0790859833f},       {46089, -0.00580739928f, -0.00101749599f},
         {50188, 3.44758564e-06f, 0.000869573909f},  {54287, 0.0345171094f, 0.0305288732f},
         {58386, -0.0314433314f, -0.0461570099f},    {62485, 0.00110437023f, 0.00429637311f},
         {66584, -0.000194986409f, -0.000911167008f}, {70683, 0.0013168942f, 0.00117437821f}}};
    const int oldLatency = 133; // 1 ms look-ahead + the end saturator, at 48 kHz
    const auto& t = paramTable ();
    for (int variant = 0; variant < 2; ++variant)
    {
        // the old state's values (normalized, as a project stores them), moved as a state from before loads
        Engine e;
        e.prepare (kSr, 512);
        setOldProject (e, variant);
        const OldGains g = oldGains (variant);
        auto setMigrated = [&] (uint32_t id, double oldPlain) {
            e.setParam (id, t.toPlain (id, migrateOldBakedNorm (id, t.toNormalized (id, oldPlain))));
        };
        for (int b = 0; b < 4; ++b)
        {
            setMigrated (bandParam (b, kBandInput), g.input[b]);
            setMigrated (bandParam (b, kBandOutput), g.output[b]);
        }
        setMigrated (kOutput, g.master);
        e.reset ();
        CHECK (e.latency () == oldLatency + e.colorLatency (), "the latency grew by Soften Color's only: %d", e.latency ());
        const int shift = e.colorLatency ();
        const Sig out = run (e, oldProjectSignal ());
        double worst = 0.0;
        for (const Golden& gd : golden[variant])
        {
            const size_t at = gd.i + (size_t)shift;
            worst = std::max ({worst, (double)std::fabs (out.l[at] - gd.l) / (1e-4 + std::fabs (gd.l)),
                               (double)std::fabs (out.r[at] - gd.r) / (1e-4 + std::fabs (gd.r))});
        }
        std::printf ("    variant %d: worst relative difference %.2g\n", variant, worst);
        CHECK (worst < 1e-3, "variant %d sounds as before: %g", variant, worst);
    }
    // the moves: Input +5.2, band Outputs +13.7 / +3.4 / +1.0 / +1.4, Output -7 dB; others untouched
    CHECK (std::fabs (oldBakedShiftDb (bandParam (2, kBandInput)) - 5.2) < 1e-9 &&
               std::fabs (oldBakedShiftDb (bandParam (0, kBandOutput)) - 13.7) < 1e-9 &&
               std::fabs (oldBakedShiftDb (bandParam (1, kBandOutput)) - 3.4) < 1e-9 &&
               std::fabs (oldBakedShiftDb (bandParam (2, kBandOutput)) - 1.0) < 1e-9 &&
               std::fabs (oldBakedShiftDb (bandParam (3, kBandOutput)) - 1.4) < 1e-9 && std::fabs (oldBakedShiftDb (kOutput) + 7.0) < 1e-9 &&
               oldBakedShiftDb (bandParam (0, kAboveThresh)) == 0.0 && oldBakedShiftDb (kScGain) == 0.0,
           "shifts");
    const double n = t.toNormalized (bandParam (0, kAboveThresh), -20.0);
    CHECK (migrateOldBakedNorm (bandParam (0, kAboveThresh), n) == n, "a threshold stays");
    // past the range: held at its end
    const uint32_t lowOut = bandParam (0, kBandOutput);
    CHECK (std::fabs (t.toPlain (lowOut, migrateOldBakedNorm (lowOut, t.toNormalized (lowOut, 15.0))) - 24.0) < 1e-9, "clamped at +24 dB");
    CHECK (std::fabs (t.toPlain (kOutput, migrateOldBakedNorm (kOutput, t.toNormalized (kOutput, -20.0))) + 24.0) < 1e-9, "clamped at -24 dB");
}

// ---- the crossover slopes ---------------------------------------------------------------------------------

// A neutral engine at a sample rate (no dynamics, the gains cancelling the baked ones), 3 or 4 bands.
static std::unique_ptr<Engine> neutralEngine (double sr, int bands, int slope)
{
    auto e = std::make_unique<Engine> ();
    e->prepare (sr, 512);
    neutralize (*e);
    e->setParam (kBands, bands - 1);
    e->setParam (kXoverSlope, slope);
    e->setParam (kSoftKnee, 0);
    e->setParam (kDetector, kPeak);
    e->reset ();
    return e;
}

static Sig sineAt (double sr, double freq, double peakDb, double secs)
{
    Sig s;
    const double a = std::pow (10.0, peakDb / 20.0);
    const size_t n = (size_t)(secs * sr);
    s.l.resize (n);
    s.r.resize (n);
    for (size_t i = 0; i < n; ++i)
        s.l[i] = s.r[i] = (float)(a * std::sin (2.0 * M_PI * freq * (double)i / sr));
    return s;
}

// the complex amplitude of frequency f in x[a, b) at rate sr
static std::complex<double> tone (const std::vector<float>& x, double f, double sr, size_t a, size_t b)
{
    std::complex<double> acc (0.0, 0.0);
    for (size_t i = a; i < b; ++i)
        acc += (double)x[i] * std::exp (std::complex<double> (0.0, -2.0 * M_PI * f * (double)i / sr));
    return acc * (2.0 / (double)(b - a));
}

TEST (slope_responses_sum_to_allpass)
{
    // the analog prototypes: low + high is the all-pass, at every slope; its magnitude is 1
    for (int s = 0; s < kNumXoverSlopes; ++s)
        for (double f : {20.0, 100.0, 700.0, 1000.0, 1400.0, 5000.0, 19000.0})
        {
            const auto r = xoverResponse (1000.0, s, f, 48000.0);
            CHECK (std::abs (r.low + r.high - r.allpass) < 1e-6 && std::fabs (std::abs (r.allpass) - 1.0) < 1e-9,
                   "slope %d at %.0f Hz: |low + high - allpass| %g", s, f, std::abs (r.low + r.high - r.allpass));
        }
    // the slopes: two to three octaves above the corner the low side falls 6 dB per octave per order
    for (int s = 0; s < kNumXoverSlopes; ++s)
    {
        const double at4k = 20.0 * std::log10 (std::abs (xoverResponse (1000.0, s, 4000.0, 192000.0).low));
        const double at8k = 20.0 * std::log10 (std::abs (xoverResponse (1000.0, s, 8000.0, 192000.0).low));
        const double perOctave[kNumXoverSlopes] = {6, 12, 18, 24, 36, 48, 60, 72, 84, 96, 192};
        const double expect = perOctave[s];
        CHECK (std::fabs ((at4k - at8k) - expect) < 0.5 + 0.02 * expect, "slope %d: %.1f dB per octave", s, at4k - at8k);
    }
}

TEST (every_slope_sums_flat)
{
    // With no processing the bands sum to an all-pass (flat level, the phase turned) at every slope,
    // band count and sample rate
    for (double sr : {44100.0, 48000.0, 96000.0})
        for (int s = 0; s < kNumXoverSlopes; ++s)
            for (int bands : {3, 4})
            {
                auto e = neutralEngine (sr, bands, s);
                e->setParam (kXover1, 88.3);
                e->setParam (kXover2, 2500.0);
                e->setParam (kXover3, 8000.0);
                double worst = 0.0, turn = 0.0, apart = 0.0;
                for (double f : {35.0, 88.3, 250.0, 1000.0, 2500.0, 5000.0, 8000.0, 14000.0})
                {
                    e->reset ();
                    auto in = sineAt (sr, f, -12.0, 0.6);
                    auto out = run (*e, in);
                    const size_t a = (size_t)(0.35 * sr), b = (size_t)(0.6 * sr);
                    const double gain = rmsDb (out.l, a, b) - rmsDb (in.l, a, b);
                    worst = std::max (worst, std::fabs (gain));
                    // the phase against the input delayed by the latency
                    const auto ratio = tone (out.l, f, sr, a, b) / tone (in.l, f, sr, a, b) *
                                       std::exp (std::complex<double> (0.0, 2.0 * M_PI * f * e->latency () / sr));
                    turn = std::max (turn, std::fabs (std::arg (ratio)));
                    const auto lat = (size_t)e->latency ();
                    for (size_t i = a; i < b; ++i)
                        apart = std::max (apart, (double)std::fabs (out.l[i] - in.l[i - lat]));
                }
                CHECK (worst < 0.1, "%.0f Hz, slope %d, %d bands: %.3f dB off flat", sr, s, bands, worst);
                if (s == kXover6)
                    CHECK (apart < 1e-5, "6 dB: the bands add up to the input itself (off by %g)", apart);
                else
                    CHECK (turn > 1.0, "slope %d turns the phase (%.2f rad)", s, turn);
            }
}

TEST (brickwall_is_stable_in_float)
{
    // the steepest slope at the extremes: the lowest crossover at 192 kHz, and the top ones at 44.1 kHz
    for (double sr : {44100.0, 192000.0})
    {
        auto e = neutralEngine (sr, 4, kXoverBrickwall);
        e->setParam (kXover1, 20.0);
        e->setParam (kXover2, sr > 100000.0 ? 40.0 : 12000.0);
        e->setParam (kXover3, 16000.0);
        e->reset ();
        for (double f : {15.0, 20.0, 30.0, 1000.0, 15000.0})
        {
            e->reset ();
            auto in = sineAt (sr, f, -6.0, 1.5);
            auto out = run (*e, in);
            bool finite = true;
            for (float v : out.l)
                finite &= std::isfinite (v);
            const size_t a = (size_t)(1.0 * sr), b = (size_t)(1.5 * sr);
            const double gain = rmsDb (out.l, a, b) - rmsDb (in.l, a, b);
            CHECK (finite && std::fabs (gain) < 0.1, "%.0f Hz rate, %.0f Hz: %.3f dB", sr, f, gain);
        }
        // noise: no drift, no blow-up
        Sig in;
        in.l.resize ((size_t)(2 * sr));
        in.r.resize (in.l.size ());
        uint32_t st = 7;
        for (size_t i = 0; i < in.l.size (); ++i)
        {
            st = st * 1664525u + 1013904223u;
            in.l[i] = in.r[i] = (float)(0.5 * ((double)(st >> 8) / 8388608.0 - 1.0));
        }
        auto out = run (*e, in);
        const double gain = rmsDb (out.l, in.l.size () / 2, in.l.size ()) - rmsDb (in.l, in.l.size () / 2, in.l.size ());
        CHECK (std::fabs (gain) < 0.2, "noise at %.0f Hz keeps its level: %.3f dB", sr, gain);
    }
}

TEST (steeper_slopes_separate_bands_more)
{
    // the mid band (120 - 1200 Hz) turned up 12 dB: its middle gets it at every slope, the other bands
    // get less of it the steeper the slope
    double prevLeak = 1e9;
    for (int s = 0; s < kNumXoverSlopes; ++s)
    {
        auto e = neutralEngine (kSr, 3, s);
        e->setParam (bandParam (kMid, kBandOutput), -kBakedOutputDb[kMid] + 12.0);
        e->reset ();
        auto gainAt = [&] (double f) {
            e->reset ();
            auto in = sine (f, -24.0, 0.5);
            auto out = run (*e, in);
            return rmsDb (out.l, 12000, 24000) - rmsDb (in.l, 12000, 24000);
        };
        const double mid = gainAt (380.0), leak = std::max (std::fabs (gainAt (40.0)), std::fabs (gainAt (4000.0)));
        std::printf ("    slope %d: middle %+.1f dB, 40 Hz / 4 kHz up to %+.2f dB\n", s, mid, leak);
        CHECK (mid > (s <= kXover18 ? 10.0 : 11.5), "slope %d: the band's middle gets its gain: %.1f dB", s, mid);
        CHECK (leak < prevLeak + 0.01, "slope %d: steeper leaks less (%.2f vs %.2f dB)", s, leak, prevLeak);
        if (s >= kXover24)
            CHECK (leak < (s == kXover24 ? 0.5 : 0.1), "slope %d: the other bands keep their level: %.2f dB", s, leak);
        // 18 dB (a Butterworth, not a Linkwitz-Riley) leaks less than 12 dB; the Linkwitz-Riley slopes
        // are compared with each other
        if (s != kXover18)
            prevLeak = leak;
    }
}

// the largest second difference in x[a, b) (a click stands out as a jump in it)
static double maxJump (const std::vector<float>& x, size_t a, size_t b)
{
    double m = 0.0;
    for (size_t i = std::max<size_t> (a, 2); i < b; ++i)
        m = std::max (m, (double)std::fabs (x[i] - 2.0f * x[i - 1] + x[i - 2]));
    return m;
}

TEST (slope_change_does_not_click)
{
    // steady tones in every band; the slope switched mid-stream to the steepest, the gentlest and back
    auto e = neutralEngine (kSr, 3, kXover24);
    e->setParam (kXover1, 200.0);
    e->setParam (kXover2, 2000.0);
    e->reset ();
    Sig in = sine (90.0, -14.0, 3.0);
    for (double f : {700.0, 5000.0})
    {
        const Sig s = sine (f, -14.0, 3.0);
        for (size_t i = 0; i < in.l.size (); ++i)
        {
            in.l[i] += s.l[i];
            in.r[i] += s.r[i];
        }
    }
    Sig out;
    out.l.resize (in.l.size ());
    out.r.resize (in.r.size ());
    const int switches[3] = {kXoverBrickwall, kXover6, kXover48};
    for (size_t pos = 0; pos < in.l.size (); pos += 256)
    {
        const size_t k = pos / 24000; // every half second a new slope
        if (pos % 24000 < 256 && k >= 1 && k <= 3)
            e->setParam (kXoverSlope, switches[k - 1]);
        const int n = (int)std::min<size_t> (256, in.l.size () - pos);
        e->process (in.l.data () + pos, in.r.data () + pos, nullptr, nullptr, out.l.data () + pos, out.r.data () + pos, n);
    }
    const double steady = maxJump (out.l, 12000, 24000);
    const double during = maxJump (out.l, 24000, 120000);
    std::printf ("    largest second difference: %.4f steady, %.4f across the switches\n", steady, during);
    CHECK (during < 1.3 * steady, "no clicks: %.4f vs %.4f", during, steady);
    CHECK (std::lround (e->param (kXoverSlope)) == kXover48, "ends on 48 dB");
    // and the level through it stays close (a crossfade, not a drop-out): 10 ms windows
    for (size_t a = 24000; a + 480 < 120000; a += 480)
        CHECK (rmsDb (out.l, a, a + 480) > rmsDb (in.l, a, a + 480) - 6.0, "window at %zu: %.1f dB", a,
               rmsDb (out.l, a, a + 480) - rmsDb (in.l, a, a + 480));
}

// ---- Soften's Color ---------------------------------------------------------------------------------------

TEST (soften_color_off_is_a_plain_delay)
{
    // one neutral band, Color off: the output is the input, delayed by the (constant) latency, to the bit
    auto e = engine ();
    e->setParam (kSoftenColor, 0.0);
    e->reset ();
    auto in = sine (3000.0, -3.0, 0.5);
    auto out = run (*e, in);
    const auto lat = (size_t)e->latency ();
    size_t diff = 0;
    for (size_t i = lat; i < out.l.size (); ++i)
        diff += out.l[i] != in.l[i - lat] || out.r[i] != in.r[i - lat];
    CHECK (diff == 0, "%zu samples differ", diff);
    const int before = e->latency ();
    e->setParam (kSoftenColor, 1.0);
    e->reset ();
    CHECK (e->latency () == before, "the same latency with Color on: %d vs %d", e->latency (), before);
    smacheratr::Engine satAlone;
    satAlone.prepare (kSr, 512);
    CHECK (e->colorLatency () == satAlone.latency (), "Color's latency is Smacheratr's: %d", e->colorLatency ());
    Engine full;
    full.prepare (kSr, 512);
    std::printf ("    latency at 48 kHz: %d samples (Color's %d)\n", full.latency (), full.colorLatency ());
}

// a bright, loud signal: a sawtooth with noise, pushed through the OTT defaults
static Sig brightSignal (double secs)
{
    Sig s;
    const size_t n = (size_t)(secs * kSr);
    s.l.resize (n);
    s.r.resize (n);
    uint32_t st = 3;
    for (size_t i = 0; i < n; ++i)
    {
        st = st * 1664525u + 1013904223u;
        const double saw = 2.0 * std::fmod (220.0 * (double)i / kSr, 1.0) - 1.0;
        const double noise = (double)(st >> 8) / 8388608.0 - 1.0;
        s.l[i] = s.r[i] = (float)(0.25 * saw + 0.05 * noise);
    }
    return s;
}

// the part of x above ~5 kHz (a fourth-order high-pass): its RMS (dB) and its crest factor (dB)
static void highs (const std::vector<float>& x, size_t a, size_t b, double& rms, double& crest)
{
    const double w = std::tan (M_PI * 5000.0 / kSr), k = std::sqrt (2.0);
    double ic1[2] {}, ic2[2] {};
    const double a1 = 1.0 / (1.0 + w * (w + k)), a2 = w * a1, a3 = w * a2;
    double s = 0.0, pk = 0.0;
    for (size_t i = 0; i < b; ++i)
    {
        double v = x[i];
        for (int st = 0; st < 2; ++st)
        {
            const double v3 = v - ic2[st], v1 = a1 * ic1[st] + a2 * v3, v2 = ic2[st] + a2 * ic1[st] + a3 * v3;
            ic1[st] = 2.0 * v1 - ic1[st];
            ic2[st] = 2.0 * v2 - ic2[st];
            v = v - k * v1 - v2;
        }
        if (i >= a)
        {
            s += v * v;
            pk = std::max (pk, std::fabs (v));
        }
    }
    const double r = std::sqrt (s / (double)(b - a));
    rms = db (r);
    crest = db (pk) - db (r);
}

TEST (soften_color_softens_the_highs)
{
    auto measure = [] (bool colorOn, double soften, double& rms, double& crest, double& all) {
        Engine e;
        e.prepare (kSr, 512);
        e.setParam (kOutput, 10.0); // hot, as OTT usually leaves it: into the curve
        e.setParam (kSoftenColor, colorOn ? 1.0 : 0.0);
        e.setParam (kSoften, soften);
        e.reset ();
        auto out = run (e, brightSignal (2.0));
        highs (out.l, 48000, 96000, rms, crest);
        all = rmsDb (out.l, 48000, 96000);
    };
    double offRms, offCrest, offAll, onRms, onCrest, onAll, maxRms, maxCrest, maxAll;
    measure (false, 0.5, offRms, offCrest, offAll);
    measure (true, 0.0, onRms, onCrest, onAll);
    measure (true, 1.0, maxRms, maxCrest, maxAll);
    std::printf ("    above 5 kHz: off %.1f dB (crest %.1f), Soften 0 %.1f dB (crest %.1f), Soften 100 %% %.1f dB (crest %.1f); "
                 "overall %.1f / %.1f / %.1f dB\n",
                 offRms, offCrest, onRms, onCrest, maxRms, maxCrest, offAll, onAll, maxAll);
    CHECK (onRms < offRms - 0.3 && onCrest < offCrest + 0.1, "Color softens the highs: %.2f vs %.2f dB", onRms, offRms);
    CHECK (maxRms < onRms - 0.3, "more with more Soften: %.2f vs %.2f dB", maxRms, onRms);
    CHECK (std::fabs (onAll - offAll) < 1.5, "the overall level stays near: %.1f vs %.1f dB", onAll, offAll);
    CHECK (std::fabs (softenColorAmount (0.0) - 0.15) < 1e-12 && std::fabs (softenColorAmount (1.0) - 0.35) < 1e-12 &&
               std::fabs (softenColorAmount (0.5) - 0.25) < 1e-12,
           "Amount 15 %% to 35 %%");
    CHECK (paramTable ().info (kSoftenColor).def == 0.0, "off by default");
}

TEST (soften_color_toggle_does_not_click)
{
    // switched on at 0.5 s and off at 1.25 s: quiet (the curve does nothing: on and off must line up to
    // the sample) and loud (bent by the curve)
    for (double level : {-30.0, -2.0})
    {
        auto e = engine ();
        e->reset ();
        auto in = sine (1000.0, level, 2.0);
        Sig out;
        out.l.resize (in.l.size ());
        out.r.resize (in.r.size ());
        for (size_t pos = 0; pos < in.l.size (); pos += 256)
        {
            if (pos == 24064)
                e->setParam (kSoftenColor, 1.0);
            if (pos == 60160)
                e->setParam (kSoftenColor, 0.0);
            const int n = (int)std::min<size_t> (256, in.l.size () - pos);
            e->process (in.l.data () + pos, in.r.data () + pos, nullptr, nullptr, out.l.data () + pos, out.r.data () + pos, n);
        }
        const double off = maxJump (out.l, 12000, 24000), on = maxJump (out.l, 45000, 60000);
        const double across = std::max (maxJump (out.l, 24000, 45000), maxJump (out.l, 60000, 80000));
        std::printf ("    %.0f dB: largest second difference %.5f off, %.5f on, %.5f across the toggles\n", level, off, on, across);
        CHECK (across < 1.3 * std::max (off, on), "no clicks at %.0f dB: %.5f vs %.5f / %.5f", level, across, off, on);
    }
}

// ---- the brickwall -----------------------------------------------------------------------------------------

TEST (brickwall_is_steep)
{
    // two bands split at 1 kHz, only the high band heard (soloed): a third of an octave below the
    // crossover the brickwall has taken it down more than 50 dB, a third above it passes (and the
    // 96 dB slope is far gentler there)
    auto highOnly = [] (int slope, double f) {
        auto e = neutralEngine (kSr, 2, slope);
        e->setParam (kXover1, 1000.0);
        e->setParam (bandParam (1, kBandSolo), 1.0);
        e->reset ();
        auto in = sine (f, -12.0, 1.0);
        auto out = run (*e, in);
        return toneDb (out.l, f, 24000, 48000) - toneDb (in.l, f, 24000, 48000);
    };
    const double below = highOnly (kXoverBrickwall, 1000.0 / 1.25), above = highOnly (kXoverBrickwall, 1000.0 * 1.25);
    const double below96 = highOnly (kXover96, 1000.0 / 1.25);
    std::printf ("    a third of an octave below: %.1f dB (96 dB: %.1f), above: %.2f dB\n", below, below96, above);
    CHECK (below < -50.0 && above > -1.0 && below < below96 - 20.0, "brickwall: %.1f / %.2f dB", below, above);
    // the prototype: -6 dB at the corner on both sides (the Linkwitz-Riley point)
    const auto at = xoverResponse (1000.0, kXoverBrickwall, 1000.0, 192000.0);
    CHECK (std::fabs (20.0 * std::log10 (std::abs (at.low)) + 6.02) < 0.05 && std::fabs (20.0 * std::log10 (std::abs (at.high)) + 6.02) < 0.05,
           "-6 dB at the corner on both sides");
}

// ---- the Sub band -------------------------------------------------------------------------------------------

// a signal with everything in it: noise and a low sweep, loud and quiet
static Sig mixedSignal (double secs)
{
    Sig s;
    const size_t n = (size_t)(secs * kSr);
    s.l.resize (n);
    s.r.resize (n);
    uint32_t state = 4242u;
    auto rnd = [&] {
        state = state * 1664525u + 1013904223u;
        return (double)(state >> 8) / (double)(1u << 24);
    };
    for (size_t i = 0; i < n; ++i)
    {
        const double env = (i / 7000) % 2 ? 0.6 : 0.03;
        const double sw = std::sin (2.0 * M_PI * (25.0 + 200.0 * i / n) * i / kSr);
        s.l[i] = (float)(env * (0.4 * (rnd () * 2 - 1) + 0.6 * sw));
        s.r[i] = (float)(env * (0.3 * (rnd () * 2 - 1) + 0.7 * sw));
    }
    return s;
}

TEST (sub_band_off_is_identical)
{
    // off, the Sub band's settings change nothing, to the bit (the OTT defaults, 3 and 4 bands, the
    // side-chain on)
    for (int bands : {2, 3})
    {
        Engine a, b;
        for (Engine* e : {&a, &b})
        {
            e->prepare (kSr, 512);
            e->setParam (kBands, bands);
            e->setParam (kScOn, 1.0);
        }
        b.setParam (kSubFreq, 90.0);
        b.setParam (kSubThresh, -50.0);
        b.setParam (kSubRatio, 20.0);
        b.setParam (kSubAttack, 1.0);
        b.setParam (kSubOutput, 12.0);
        b.setParam (kSubInput, -12.0);
        a.reset ();
        b.reset ();
        const Sig in = mixedSignal (1.0);
        const Sig sc = sine (50.0, -10.0, 1.0);
        const Sig oa = run (a, in, &sc), ob = run (b, in, &sc);
        size_t diff = 0;
        for (size_t i = 0; i < oa.l.size (); ++i)
            diff += oa.l[i] != ob.l[i] || oa.r[i] != ob.r[i];
        CHECK (diff == 0 && !b.subRunning (), "%d bands: %zu samples differ", bands + 1, diff);
        CHECK (a.latency () == b.latency (), "the same latency");
    }
    // switched on and off again it stops (after its fade)
    Engine e;
    e.prepare (kSr, 512);
    e.setParam (kSubOn, 1.0);
    e.reset ();
    CHECK (e.subRunning (), "on from the start");
    run (e, mixedSignal (0.2));
    e.setParam (kSubOn, 0.0);
    run (e, mixedSignal (0.2));
    CHECK (!e.subRunning (), "stopped after fading out");
}

TEST (sub_band_sums_flat)
{
    // with nothing processed the Sub band and the bands still add up to an all-pass, at every slope, for
    // 1, 3 and 4 bands, with the corner anywhere in its range
    for (int s = 0; s < kNumXoverSlopes; ++s)
        for (int bands : {1, 3, 4})
            for (double corner : {20.0, 40.0, 100.0})
            {
                auto e = neutralEngine (kSr, bands, s);
                e->setParam (kXover1, 150.0);
                e->setParam (kSubOn, 1.0);
                e->setParam (kSubFreq, corner);
                e->setParam (kSubRatio, 1.0);
                double worst = 0.0;
                for (double f : {15.0, 30.0, 40.0, 70.0, 100.0, 300.0, 3000.0})
                {
                    e->reset ();
                    auto in = sine (f, -12.0, 1.2);
                    auto out = run (*e, in);
                    const double gain = rmsDb (out.l, 38400, 57600) - rmsDb (in.l, 38400, 57600);
                    worst = std::max (worst, std::fabs (gain));
                }
                CHECK (worst < 0.1, "slope %d, %d bands, Sub at %.0f Hz: %.3f dB off flat", s, bands, corner, worst);
            }
    // the Slope changed while the Sub band runs: after the crossfade it is flat again at the new slope
    auto e = neutralEngine (kSr, 3, kXover24);
    e->setParam (kSubOn, 1.0);
    e->setParam (kSubRatio, 1.0);
    e->reset ();
    const auto in = sine (40.0, -12.0, 2.0);
    Sig out;
    out.l.resize (in.l.size ());
    out.r.resize (in.r.size ());
    for (size_t pos = 0; pos < in.l.size (); pos += 256)
    {
        if (pos == 24064)
            e->setParam (kXoverSlope, kXover96);
        const int n = (int)std::min<size_t> (256, in.l.size () - pos);
        e->process (in.l.data () + pos, in.r.data () + pos, nullptr, nullptr, out.l.data () + pos, out.r.data () + pos, n);
    }
    const double after = rmsDb (out.l, 72000, 96000) - rmsDb (in.l, 72000, 96000);
    CHECK (std::fabs (after) < 0.1, "flat after the Slope changed with the Sub band on: %.3f dB", after);
}

TEST (sub_band_compresses_the_sub)
{
    // three neutral bands; the Sub band at 40 Hz, 4:1 above -30 dB: a loud 30 Hz tone is compressed,
    // a loud 200 Hz tone is not, and a quiet 30 Hz one (under the threshold) is not either
    double corner = 40.0;
    auto gainAt = [&corner] (double f, double levelDb, bool sub) {
        auto e = neutralEngine (kSr, 3, kXover24);
        e->setParam (kSubOn, sub ? 1.0 : 0.0);
        e->setParam (kSubFreq, corner);
        e->setParam (kSubThresh, -30.0);
        e->setParam (kSubRatio, 4.0);
        e->setParam (kSubAttack, 30.0);
        e->setParam (kSubRelease, 200.0);
        e->reset ();
        auto in = sine (f, levelDb, 2.0);
        auto out = run (*e, in);
        return toneDb (out.l, f, 48000, 96000) - toneDb (in.l, f, 48000, 96000);
    };
    const double loud30 = gainAt (30.0, -6.0, true), loud200 = gainAt (200.0, -6.0, true), quiet30 = gainAt (30.0, -40.0, true);
    const double off30 = gainAt (30.0, -6.0, false);
    std::printf ("    30 Hz at -6 dB: %+.1f dB (off %+.2f), 200 Hz at -6 dB: %+.2f dB, 30 Hz at -40 dB: %+.2f dB\n", loud30, off30, loud200,
                 quiet30);
    // at 40 Hz the tone sits inside the corner's taper (a quarter of it, -6 dB, still goes to band 1),
    // so it gets part of the 18 dB 4:1 takes from 24 dB above the threshold; with the corner at 80 Hz,
    // most of it
    CHECK (loud30 < -6.0 && std::fabs (off30) < 0.1, "a loud 30 Hz tone compressed: %.1f dB", loud30);
    CHECK (std::fabs (loud200) < 0.2, "200 Hz untouched: %.2f dB", loud200);
    CHECK (std::fabs (quiet30) < 0.2, "a quiet 30 Hz tone untouched: %.2f dB", quiet30);
    corner = 80.0;
    const double loud30at80 = gainAt (30.0, -6.0, true), loud200at80 = gainAt (200.0, -6.0, true);
    std::printf ("    corner at 80 Hz: 30 Hz %+.1f dB, 200 Hz %+.2f dB\n", loud30at80, loud200at80);
    CHECK (loud30at80 < -14.0 && std::fabs (loud200at80) < 0.3, "corner at 80 Hz: %.1f / %.2f dB", loud30at80, loud200at80);
    // the Sub band's Output, and its meter
    auto e = neutralEngine (kSr, 3, kXover24);
    e->setParam (kSubOn, 1.0);
    e->setParam (kSubRatio, 1.0);
    e->setParam (kSubOutput, 6.0);
    e->reset ();
    auto in = sine (25.0, -20.0, 1.5);
    auto out = run (*e, in);
    const double g = toneDb (out.l, 25.0, 48000, 72000) - toneDb (in.l, 25.0, 48000, 72000);
    CHECK (g > 3.0 && g < 6.5, "Sub Output +6 dB: %.2f dB at 25 Hz", g);
    CHECK (e->meter (kSubBand).outputDb > -20.0 && e->meter (kSubBand).outputDb < -8.0, "the Sub band's meter: %.1f dB",
           e->meter (kSubBand).outputDb);
    // its Input: a level before the compression, as a band's (uncompressed, +6 dB as the Output's)
    auto subGain = [&] (double inDb, double outDb, double thresh, double ratio) {
        auto s = neutralEngine (kSr, 3, kXover24);
        s->setParam (kSubOn, 1.0);
        s->setParam (kSubThresh, thresh);
        s->setParam (kSubRatio, ratio);
        s->setParam (kSubInput, inDb);
        s->setParam (kSubOutput, outDb);
        s->reset ();
        auto o = run (*s, in);
        return toneDb (o.l, 25.0, 48000, 72000) - toneDb (in.l, 25.0, 48000, 72000);
    };
    const double gIn = subGain (6.0, 0.0, 0.0, 1.0);
    CHECK (std::fabs (gIn - g) < 0.3, "Sub Input +6 dB: %.2f dB (Output +6 dB: %.2f dB)", gIn, g);
    // ... so it drives the threshold harder: +12 in, -12 out compresses more than 0 / 0
    const double flat = subGain (0.0, 0.0, -30.0, 4.0), pushed = subGain (12.0, -12.0, -30.0, 4.0);
    std::printf ("    Sub 4:1 at -30 dB: Input 0 dB %+.2f dB, Input +12 / Output -12 dB %+.2f dB\n", flat, pushed);
    CHECK (pushed < flat - 3.0, "Sub Input drives the compression: %.2f vs %.2f dB", pushed, flat);
}

TEST (sub_band_toggle_does_not_click)
{
    // a 45 Hz and a 400 Hz tone, the Sub band compressing, switched on at 0.5 s and off at 1.5 s: the
    // latency stays, no clicks, no drop-out
    auto e = neutralEngine (kSr, 3, kXover24);
    e->setParam (kSubThresh, -30.0);
    e->setParam (kSubRatio, 4.0);
    e->reset ();
    const int latency = e->latency ();
    Sig in = sine (45.0, -8.0, 2.5);
    const Sig hi = sine (400.0, -14.0, 2.5);
    for (size_t i = 0; i < in.l.size (); ++i)
    {
        in.l[i] += hi.l[i];
        in.r[i] += hi.r[i];
    }
    Sig out;
    out.l.resize (in.l.size ());
    out.r.resize (in.r.size ());
    bool sameLatency = true;
    for (size_t pos = 0; pos < in.l.size (); pos += 256)
    {
        if (pos == 24064)
            e->setParam (kSubOn, 1.0);
        if (pos == 72192)
            e->setParam (kSubOn, 0.0);
        const int n = (int)std::min<size_t> (256, in.l.size () - pos);
        e->process (in.l.data () + pos, in.r.data () + pos, nullptr, nullptr, out.l.data () + pos, out.r.data () + pos, n);
        sameLatency &= e->latency () == latency;
    }
    CHECK (sameLatency, "the latency never changes");
    const double off = maxJump (out.l, 12000, 24000), on = maxJump (out.l, 50000, 72000);
    const double across = std::max (maxJump (out.l, 24000, 50000), maxJump (out.l, 72000, 100000));
    std::printf ("    largest second difference %.5f off, %.5f on, %.5f across the switches\n", off, on, across);
    CHECK (across < 1.3 * std::max (off, on), "no clicks: %.5f vs %.5f / %.5f", across, off, on);
    for (size_t a = 24000; a + 480 < 100000; a += 480)
        CHECK (rmsDb (out.l, a, a + 480) > rmsDb (in.l, a, a + 480) - 12.0, "window at %zu: %.1f dB", a,
               rmsDb (out.l, a, a + 480) - rmsDb (in.l, a, a + 480));
    CHECK (!e->subRunning (), "off again at the end");
}

TEST (ott_migration_round_trip)
{
    // every gain an old state holds: the old baked gain plus the old trim is the new baked gain plus the
    // migrated trim (unless held at the range's end), and migrating a whole state is the same as
    // migrating each value
    const auto& t = paramTable ();
    uint32_t seed = 5;
    auto rnd = [&] {
        seed = seed * 1664525u + 1013904223u;
        return (double)(seed >> 8) / (double)(1u << 24);
    };
    std::vector<double> old (kNumParams), whole (kNumParams);
    for (uint32_t id = 0; id < kNumParams; ++id)
        old[id] = whole[id] = rnd ();
    migrateOldBaked (whole.data ());
    int moved = 0;
    for (uint32_t id = 0; id < kNumParams; ++id)
    {
        CHECK (whole[id] == migrateOldBakedNorm (id, old[id]), "%s: whole state vs one value", t.info (id).name);
        const double shift = oldBakedShiftDb (id);
        if (shift == 0.0)
        {
            CHECK (whole[id] == old[id], "%s untouched", t.info (id).name);
            continue;
        }
        ++moved;
        const double oldPlain = t.toPlain (id, old[id]), newPlain = t.toPlain (id, whole[id]);
        const auto& info = t.info (id);
        const double want = std::clamp (oldPlain + shift, info.min, info.max);
        CHECK (std::fabs (newPlain - want) < 1e-9, "%s: %.2f -> %.2f dB (want %.2f)", info.name, oldPlain, newPlain, want);
    }
    CHECK (moved == 2 * kMaxBands + 1, "the band Inputs and Outputs and the Output move (%d)", moved);
    // the new parameters are where an old project was: 24 dB crossovers, no Color, no Sub band
    CHECK (t.info (kXoverSlope).def == (double)kXover24 && t.info (kSoftenColor).def == 0.0 && t.info (kSubOn).def == 0.0,
           "an old state (without them) gets the old sound");
}

TEST (performance)
{
    auto e = engine (false);
    e->setParam (kScOn, 1);
    for (int b = 0; b < kNumBands; ++b)
    {
        e->setParam (bandParam (b, kAboveRatio), 4.0);
        e->setParam (bandParam (b, kBelowRatio), 0.7);
    }
    auto in = sine (440.0, -12.0, 10.0);
    const std::clock_t t0 = std::clock (); // CPU time: other programs running do not count
    run (*e, in, &in);
    const double secs = (double)(std::clock () - t0) / CLOCKS_PER_SEC;
    std::printf ("    CPU: %.2f%% of one core (3 bands + side-chain, stereo)\n", 100.0 * secs / 10.0);
    CHECK (secs / 10.0 < 0.08, "too slow"); // 4 bands plus the 4x oversampled saturator
    // four bands with the side-chain, the new parts one by one, up to the heaviest: the brickwall with
    // the Sub band and Soften's Color
    struct Setup
    {
        int slope;
        bool sub, color;
        const char* name;
    };
    const Setup setups[] = {{kXover24, false, false, "24 dB"},
                            {kXover24, true, false, "24 dB, Sub band"},
                            {kXover24, false, true, "24 dB, Color"},
                            {kXoverBrickwall, false, false, "brickwall"},
                            {kXoverBrickwall, true, true, "brickwall, Sub band, Color"}};
    double heaviest = 0.0;
    for (const Setup& s : setups)
    {
        e->setParam (kBands, 3);
        e->setParam (kXoverSlope, s.slope);
        e->setParam (kSubOn, s.sub ? 1.0 : 0.0);
        e->setParam (kSoftenColor, s.color ? 1.0 : 0.0);
        heaviest = 1e9; // the best of three runs (a busy machine only makes a run slower)
        for (int k = 0; k < 3; ++k)
        {
            e->reset ();
            const std::clock_t t1 = std::clock ();
            run (*e, in, &in);
            heaviest = std::min (heaviest, (double)(std::clock () - t1) / CLOCKS_PER_SEC / 10.0);
        }
        std::printf ("    CPU: %.2f%% of one core (4 bands + side-chain: %s)\n", 100.0 * heaviest, s.name);
    }
    CHECK (heaviest < 0.3, "too slow"); // (about 20 % on CI's macOS machines, 16 % here)
}

// ---------------------------------------------------------------------------
// OTT style (Ott.h): the default, a model of Xfer's OTT

// solo: only that band (of the three) is heard. (With all bands working, a
// tone's crossover leakage into a quiet neighbour gets that band's full upward lift, as in OTT: the
// model was measured one band at a time, so a band's own curve is measured that way too.)
static std::unique_ptr<Engine> ottEngine (int solo = -1)
{
    auto e = std::make_unique<Engine> ();
    e->prepare (kSr, 512);
    e->setParam (kSatOn, 0.0);
    if (solo >= 0)
        e->setParam (bandParam (solo, kBandSolo), 1.0);
    e->reset ();
    return e;
}

// the level OTT's detector settles at on a steady sine (its one-pole on the mean square, attack faster
// than release, rides a little above the mean): dB
static double ottDetectorDb (int kind, double hz, double peakDb)
{
    const double a = std::pow (10.0, peakDb / 20.0);
    const double ca = 1.0 - std::exp (-1.0 / (ott::kAttack[kind] * kSr)), cr = 1.0 - std::exp (-1.0 / (ott::releaseSec (kind, 100.0) * kSr));
    double env = 0.0, sum = 0.0;
    const int n = (int)kSr, from = n / 2;
    for (int i = 0; i < n; ++i)
    {
        const double x = a * std::sin (2.0 * M_PI * hz * i / kSr), x2 = x * x;
        env += (x2 > env ? ca : cr) * (x2 - env);
        if (i >= from)
            sum += 10.0 * std::log10 (std::max (env, 1e-20));
    }
    return sum / (n - from);
}

TEST (ott_static_curve)
{
    // a steady tone in each band settles at OTT's gain for its mean square (a sine's: its peak level
    // - 3 dB), with every control at its default; from silence up to full scale
    struct Probe
    {
        double hz;
        int kind;
    };
    for (Probe pr : {Probe {45.0, 0}, Probe {700.0, 1}, Probe {8000.0, 2}})
        for (double level : {-80.0, -60.0, -48.0, -40.0, -30.0, -20.0, -10.0, 0.0})
        {
            auto e = ottEngine (pr.kind);
            auto in = sine (pr.hz, level, 1.0);
            auto out = run (*e, in);
            const double got = rmsDb (out.l, 36000, 48000) - rmsDb (in.l, 36000, 48000);
            const double want = ott::gainDb (pr.kind, ottDetectorDb (pr.kind, pr.hz, level), 1.0, 1.0, 1.0, 0.0, 0.0);
            CHECK (std::fabs (got - want) < 0.6, "%.0f Hz at %.0f dB: %.2f dB (OTT: %.2f dB)", pr.hz, level, got, want);
        }
    // the law itself at a few points: a quiet band lifted to the cap (plus makeup), a loud one cut hard
    CHECK (std::fabs (ott::gainDb (1, -120.0, 1.0, 1.0, 1.0, 0.0, 0.0) - (ott::kMakeup[1] + ott::kUpCap[1])) < 0.01, "the upward cap");
    CHECK (ott::gainDb (1, 20.0, 1.0, 1.0, 1.0, 0.0, 0.0) == ott::kFloor[1], "far over full scale: down to the floor");
    CHECK (std::fabs (ott::gainDb (1, -40.0, 0.0, 1.0, 1.0, 0.0, 0.0)) < 1e-12, "Depth 0: nothing");
}

TEST (ott_amount_and_controls)
{
    auto gainAt = [] (uint32_t id, double v, double level) {
        auto e = ottEngine (1);
        if (id != kNumParams)
            e->setParam (id, v);
        auto in = sine (700.0, level, 1.0);
        auto out = run (*e, in);
        return rmsDb (out.l, 36000, 48000) - rmsDb (in.l, 36000, 48000);
    };
    // Amount 0: the bands (all of them) sum flat (OTT's Depth 0)
    for (double level : {-60.0, -20.0})
    {
        auto e = ottEngine ();
        e->setParam (kAmount, 0.0);
        auto in = sine (700.0, level, 1.0);
        auto out = run (*e, in);
        const double g = rmsDb (out.l, 36000, 48000) - rmsDb (in.l, 36000, 48000);
        CHECK (std::fabs (g) < 0.05, "Amount 0 at %.0f dB: %.3f dB", level, g);
    }
    // a band's Below threshold moves OTT's upward knee: 10 dB lower lifts a -60 dB tone 7.5 dB less
    const double base = gainAt (kNumParams, 0.0, -60.0);
    const double lower = gainAt (bandParam (1, kBelowThresh), -41.8 - 10.0, -60.0);
    CHECK (std::fabs ((base - lower) - 10.0 * ott::kUpSlope[1]) < 0.6, "Below 10 dB lower: %.2f dB less lift", base - lower);
    // the Above ratio at 1:1 switches the downward branch off: a loud tone is no longer pulled down
    CHECK (gainAt (bandParam (1, kAboveRatio), 1.0, -10.0) > gainAt (kNumParams, 0.0, -10.0) + 15.0, "Above 1:1: no downward compression");
    // band Output trims after OTT's makeup
    CHECK (std::fabs (gainAt (bandParam (1, kBandOutput), -6.0, -30.0) - (gainAt (kNumParams, 0.0, -30.0) - 6.0)) < 0.1, "band Output trims");
    // Character keeps Multidyn's own sound: it differs from OTT on the same tones
    double diff = 0.0;
    for (double level : {-60.0, -45.0, -30.0, -15.0})
    {
        std::printf ("    %.0f dB: OTT %.2f dB, Character %.2f dB\n", level, gainAt (kNumParams, 0.0, level), gainAt (kStyle, kStyleCharacter, level));
        diff = std::max (diff, std::fabs (gainAt (kStyle, kStyleCharacter, level) - gainAt (kNumParams, 0.0, level)));
    }
    CHECK (diff > 1.0, "Character is a different sound (up to %.2f dB apart)", diff);
}

TEST (ott_expander_is_capped)
{
    // an Above ratio under 1:1 makes OTT's downward branch an expander: its boost stops at the upward cap
    for (int k = 0; k < 3; ++k)
    {
        const double top = ott::makeupShape (k, 1.0) * ott::kMakeup[k] + ott::kUpCap[k];
        double worst = -1e9;
        for (double e = -60.0; e <= 24.0; e += 1.0)
            worst = std::max (worst, ott::gainDb (k, e, 1.0, 1.0, -3.0, 0.0, 0.0));
        CHECK (worst <= top + 1e-9 && worst > top - 1.0, "band kind %d: at most %.1f dB, %.1f dB", k, top, worst);
        // and the defaults are unchanged by it
        CHECK (ott::gainDb (k, -20.0, 1.0, 1.0, 1.0, 0.0, 0.0) < top, "band kind %d: the defaults", k);
    }
}

TEST (ott_time_constants)
{
    // a 700 Hz tone stepping from -50 dB up to -10 dB: OTT's mid band catches it within its 6 ms attack
    // (a few time constants: 30 ms); back down, its release (31 ms at Time 100 %) takes a few hundred ms over
    // 40 dB; Time 400 % releases about four times slower
    auto settle = [] (double time, bool up) {
        auto e = ottEngine (1);
        e->setParam (kTime, time);
        const size_t n = (size_t)(4.0 * kSr), step = (size_t)(1.0 * kSr);
        Sig in;
        in.l.resize (n);
        for (size_t i = 0; i < n; ++i)
        {
            const bool loud = up ? i >= step : i < step;
            in.l[i] = (float)(std::pow (10.0, (loud ? -10.0 : -50.0) / 20.0) * std::sin (2.0 * M_PI * 700.0 * i / kSr));
        }
        in.r = in.l;
        auto out = run (*e, in);
        const double target = rmsDb (out.l, n - 4800, n) - rmsDb (in.l, n - 4800, n);
        // the time until the gain is within 1 dB of where it ends
        const size_t win = 96; // 2 ms
        for (size_t a = step; a + win < n; a += win)
            if (std::fabs ((rmsDb (out.l, a, a + win) - rmsDb (in.l, a, a + win)) - target) < 1.0)
                return 1000.0 * (a - step) / kSr;
        return 1e9;
    };
    const double attackMs = settle (1.0, true), releaseMs = settle (1.0, false), slowMs = settle (4.0, false);
    std::printf ("    settles: %.0f ms up, %.0f ms down (Time 100 %%), %.0f ms down (Time 400 %%)\n", attackMs, releaseMs, slowMs);
    CHECK (attackMs < 40.0, "the attack is OTT's few ms: %.0f ms", attackMs);
    // a 40 dB drop: the mean square falls 4.3 dB per time constant (31 ms at Time 100 %), and the gain is
    // within 1 dB once it is within about 1.3 dB: about nine of them
    CHECK (releaseMs > 200.0 && releaseMs < 350.0, "the release is OTT's: %.0f ms", releaseMs);
    CHECK (slowMs > 3.0 * releaseMs, "Time 400 %% releases about four times slower: %.0f vs %.0f ms", slowMs, releaseMs);
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
