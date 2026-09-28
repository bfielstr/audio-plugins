// Headless tests for the Multidyn DSP. Run: ./multidyn_tests [filter]
#include "Engine.h"
#include "smacheratr/src/core/Engine.h"
#include "Params.h"

#include <chrono>
#include <cmath>
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
    // Attack 50 ms = time to cover ~95 % of a level change (time constant 16.7 ms). The level
    // envelope rising from -40 to -6 dB only crosses the -20 dB threshold after ~15 ms, so there
    // is no reduction at 10 ms, most of it by 30 ms and essentially all of it by 50 ms - the
    // gain follows the transfer curve rather than the attack time literally.
    CHECK (std::fabs (g[0]) < 0.3, "no reduction before the envelope reaches the threshold: %f", g[0]);
    const double at30 = g[2] / -12.6;
    CHECK (at30 > 0.45 && at30 < 0.75, "most of the reduction at 30 ms: %f", at30);
    CHECK (g[4] / -12.6 > 0.85, "nearly all of it at 50 ms: %f", g[4] / -12.6);
    CHECK (std::fabs (g.back () + 12.6) < 0.8, "settles at -12.6: %f", g.back ());
    const double at100 = at30;
    // Time 200 % slows everything down
    auto e2 = engine ();
    e2->setParam (bandParam (kOnly, kAboveThresh), -20.0);
    e2->setParam (bandParam (kOnly, kAboveRatio), 10.0);
    e2->setParam (bandParam (kOnly, kAttack), 50.0);
    e2->setParam (kTime, 2.0);
    run (*e2, quiet);
    Sig chunk;
    chunk.l.assign (loud.l.begin (), loud.l.begin () + 1440);
    chunk.r = chunk.l;
    run (*e2, chunk, nullptr, 480);
    CHECK (e2->meter (kOnly).gainDb / -12.6 < at100 * 0.5, "Time 200%% slows the attack: %f at 30 ms", e2->meter (kOnly).gainDb);
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

TEST (preset_defaults)
{
    // A fresh engine is the four-band upward-compression preset (OTT pushed further).
    Engine e;
    e.prepare (kSr, 512);
    CHECK (std::lround (e.param (kBands)) == 2 && std::fabs (e.param (kXover1) - 88.3) < 1e-9 &&
               std::fabs (e.param (kXover2) - 2500.0) < 1e-9 && std::fabs (e.param (kXover3) - 8000.0) < 1e-9,
           "3 bands at 88.3 Hz / 2.5 kHz (8 kHz for a fourth)");
    CHECK (std::lround (e.param (kMode)) == kCharacter && e.param (kPreLimit) < 0.5 && e.param (kPreLimitCeiling) == 0.0,
           "Character mode, pre-limit off");
    CHECK (e.param (kSatOn) < 0.5 && e.param (kSatDrive) == 0.0, "end-of-chain saturator off, Drive 0 dB");
    CHECK (e.param (bandParam (2, kAboveRatio)) == kRatioInf && std::fabs (e.param (bandParam (1, kAboveRatio)) - 66.7) < 1e-9 &&
               e.param (bandParam (0, kBelowRatio)) == kRatioInf && std::fabs (e.param (bandParam (3, kBelowRatio)) - 4.17) < 1e-9,
           "ratios");
    CHECK (e.param (bandParam (0, kBandOutput)) == 0.0 && e.param (bandParam (1, kBandInput)) == 0.0 && e.param (kOutput) == 0.0 &&
               std::fabs (e.param (bandParam (2, kAttack)) - 13.5) < 1e-9 && kBakedOutputDb[0] == 24.0 && kBakedMasterDb == -7.0,
           "gains and times");
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
    CHECK (lat == 48 + satAlone.latency (), "1 ms look-ahead + the saturator's oversampling at 48 kHz: %d", lat);
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

TEST (character_mode_is_slower_and_smoother)
{
    // a -6 dB tone after 0.5 s of -40 dB: Character eases into the compression (50 ms RMS window,
    // rounded onset) and lands at the same amount of gain reduction
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
    const double baseEarly = levelAfter (kBase, 0.01), charEarly = levelAfter (kCharacter, 0.01);
    CHECK (charEarly > baseEarly + 1.0, "Character eases in: %f vs Base %f", charEarly, baseEarly);
    const double baseLate = levelAfter (kBase, 0.4), charLate = levelAfter (kCharacter, 0.4);
    CHECK (std::fabs (charLate - baseLate) < 1.5, "same steady state: %f vs %f", charLate, baseLate);
    CHECK (baseLate < -6.0 - 15.0, "compressing: %f", baseLate);
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
    // bound: +24 dB band input, +36 dB maximum upward gain, +24 dB band output on a full-scale input
    CHECK (worst < std::pow (10.0, 84.0 / 20.0) * 1.05, "runaway gain %f", worst);
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
    const auto t0 = std::chrono::steady_clock::now ();
    run (*e, in, &in);
    const double secs = std::chrono::duration<double> (std::chrono::steady_clock::now () - t0).count ();
    std::printf ("    CPU: %.2f%% of one core (3 bands + side-chain, stereo)\n", 100.0 * secs / 10.0);
    CHECK (secs / 10.0 < 0.08, "too slow"); // 4 bands plus the 4x oversampled saturator
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
